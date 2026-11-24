/*
Description:
This is a library for Texas Instruments DAC1220
http://www.ti.com/lit/ds/symlink/dac1220.pdf

Version:
V0.1

Release Date:
2016-11-16

Author:
Peng Wei          info@sandboxelectronics.com

Lisence:

Modified John Freudenthal 2025-06-18
2026-11-23: set-up checked against the datasheet (SBAS082G). The command register is written in
two steps (the format first, then self-calibration), both bytes, bit 13 set; begin() drives CS high
and waits out the serial interface's I/O recovery timeout first; writeCode() aligns 16-bit codes
and clamps before shifting; writeV() no longer shifts twice.
2026-11-24: calibration-register access for diagnosing gain errors: clearCalibration() (CRST),
writeOffsetCalibration(), writeFullScaleCalibration(), and register read-back (readRegister() and
friends, ReadbackWorks()), which needs SDIO wired to the bus's RX pin (see SetReadbackTXPin()).
*/

#include <Arduino.h>
#include "SPI.h"
#include "dac1220.h"
#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/gpio.h>
#endif

// Command register (CMR) values, datasheet Tables 12 and 13.
// Byte 1 (address 4): ADPT 0 (adaptive filter on), CALPIN 0 (output disconnected during calibration),
// bit 13 written 1 (required on first-revision parts, read-only 1 on current parts).
static const uint8_t CMRByte1 = 0x20;
// CRST (bit 9, byte 1 bit 1): in Normal mode, writing 1 sets OCR to 000000h and FCR to 800000h.
static const uint8_t CMRByte1CRST = 0x02;
// Byte 0 (address 5): RES (bit 7, 1 = 20-bit), CLR 0, DF (bit 5, 1 = straight binary), DISF 0,
// BD 0 (address increments: MSB first), MSB 0 (bits MSB first), MD (bits 1-0).
static const uint8_t CMRByte0Res20 = 0x80;
static const uint8_t CMRByte0StraightBinary = 0x20;
static const uint8_t CMRModeNormal = 0x00;
static const uint8_t CMRModeSelfCalibration = 0x01;
// Command bytes (Table 6): R/W 0 (write), MB = number of bytes - 1, ADR = start address.
static const uint8_t CommandWriteCMR2Bytes = 0x24;  // write, 2 bytes, address 4 (CMR MSB)
static const uint8_t CommandWriteDIR3Bytes = 0x40;  // write, 3 bytes, address 0 (DIR MSB)
static const uint8_t CommandWriteOCR3Bytes = 0x48;  // write, 3 bytes, address 8 (OCR MSB)
static const uint8_t CommandWriteFCR3Bytes = 0x4C;  // write, 3 bytes, address 12 (FCR MSB)
static const uint8_t CommandRead = 0x80;            // R/W bit set
// Register map addresses (Table 11).
static const uint8_t AddressDIR = 0;
static const uint8_t AddressCMR = 4;
static const uint8_t AddressOCR = 8;
static const uint8_t AddressFCR = 12;
// Delays, generous for a 2.5 MHz XIN (tXIN 400 ns), Table 4.
static const uint32_t CSToSCLKMicros = 12;        // t10: CS falling to first SCLK rising, >= 11 tXIN
static const uint32_t CommandToDataMicros = 15;   // t9: command byte to first data byte, >= 13 tXIN
static const uint32_t CSHighMicros = 20;          // t15: CS high between commands, >= 22 tXIN
// Reads: the DAC drives SDIO 8 to 10 tXIN after the command's last SCLK (t11), so TX is released
// straight after the command byte, and SDIO is released 4 to 6 tXIN after the last data SCLK (t13).
static const uint32_t SDIOReleaseMicros = 5;
// Write-only use: a command cut off by a host reset is cancelled by the DAC's I/O recovery timeout,
// about 100 ms of no SCLK or CS activity at 2.5 MHz. The datasheet advises waiting 150 to 200 ms.
static const uint32_t IORecoveryMillis = 200;
// Self-calibration takes 300 to 500 ms at 2.5 MHz; without read-back wait at least 500 ms.
static const uint32_t SelfCalibrationMillis = 550;

DAC1220::DAC1220(SPIClass* spiconnection, bool uselowresolution, uint8_t cs, uint8_t sclk, float clockperiodmicroseconds)
{
    Use16Bits = uselowresolution;
    SPIConnection = spiconnection;
    ConnectionSettings = SPISettings(100000,MSBFIRST,SPI_MODE1); // DAC latches on the falling SCLK edge
    CSPin = cs;
    SCLKPin = sclk;
    ClockPerioduS = clockperiodmicroseconds;
    Verbose = false;
    ReadbackTXPin = -1;
}

// Call after SPIConnection->begin(). Drives CS high, waits for the I/O recovery timeout (so a
// command interrupted by a host reset is cancelled), then sets the format and self-calibrates.
void DAC1220::begin()
{
    pinMode(CSPin, OUTPUT);
    digitalWrite(CSPin, HIGH);
    delay(IORecoveryMillis);
    selfcalibrate();
}

//Call SPI.end prior, and SPI.begin after to re-establish SPI connection, or prior to SPI.begin in start()
void DAC1220::reset()
{
    uint32_t Delay0 = (uint32_t)(600 * ClockPerioduS); //First high period (600 clocks 512min 800max)
    uint32_t Delay1 = (uint32_t)(1200 * ClockPerioduS); //Second high period (1200 clocks 1024min 1800max)
    uint32_t Delay2 = (uint32_t)(2200 * ClockPerioduS); //Third high period (2200 clocks 2048min 2400max)
    uint32_t LowDelay = (uint32_t)(20 * ClockPerioduS); //Low period delay (10 clocks min no max)
    pinMode(CSPin, OUTPUT);
    digitalWrite(CSPin, LOW);
    pinMode(SCLKPin, OUTPUT);
    digitalWrite(SCLKPin, LOW);
    delay(LowDelay);
    digitalWrite(SCLKPin, HIGH);
    delayMicroseconds( Delay0 );
    digitalWrite(SCLKPin, LOW);
    delayMicroseconds(LowDelay);
    digitalWrite(SCLKPin, HIGH);
    delayMicroseconds(Delay1);
    digitalWrite(SCLKPin, LOW);
    delayMicroseconds(LowDelay);
    digitalWrite(SCLKPin, HIGH);
    delayMicroseconds(Delay2);
    digitalWrite(SCLKPin, LOW);
    delay(LowDelay);
    digitalWrite(CSPin, HIGH);
}

// Both command register bytes, MSB first (the CMR is always written in increment mode).
// The write takes effect on the last SCLK falling edge.
void DAC1220::writeCommandRegister(uint8_t Byte1, uint8_t Byte0)
{
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(CSToSCLKMicros);
    SPIConnection->transfer(CommandWriteCMR2Bytes);
    delayMicroseconds(CommandToDataMicros);
    SPIConnection->transfer(Byte1);
    SPIConnection->transfer(Byte0);
    digitalWrite(CSPin, HIGH);
    SPIConnection->endTransaction();
    delayMicroseconds(CSHighMicros);
    if (Verbose)
    {
        Serial.print("[DAC1220CMR](");
        Serial.print(Byte1,HEX);
        Serial.print(",");
        Serial.print(Byte0,HEX);
        Serial.print(")\n");
    }
}

// The datasheet: "No other bits should be changed in the Command Register when setting [Self
// calibration] mode." So the resolution and straight-binary format are set first, in Normal mode,
// and self-calibration is then started with the same bits. The DAC returns to Normal mode itself.
uint8_t DAC1220::FormatByte()
{
    return CMRByte0StraightBinary | (Use16Bits ? 0x00 : CMRByte0Res20);
}

void DAC1220::selfcalibrate()
{
    uint8_t Format = FormatByte();
    writeCommandRegister(CMRByte1, Format | CMRModeNormal);
    writeCommandRegister(CMRByte1, Format | CMRModeSelfCalibration);
    delay(SelfCalibrationMillis);
}

bool DAC1220::Is16Bit()
{
    return Use16Bits;
}

// 0 to 5 V (2 x VREF with the 2.5 V reference), straight binary.
void DAC1220::writeV(float v)
{
    if (v < 0.0)
    {
        v = 0.0;
    }
    else if (v > 5.0)
    {
        v = 5.0;
    }
    uint32_t FullScale = Use16Bits ? 0xFFFF : 0xFFFFF;
    uint32_t code = (uint32_t)(v / 5.0 * FullScale + 0.5);
    writeCode(code); // writeCode() clamps and aligns
}

bool DAC1220::GetVerbose()
{
    return Verbose;
}

void DAC1220::SetVerbose(bool VerboseToSet)
{
    Verbose = VerboseToSet;
}

// code: 0..0xFFFFF in 20-bit mode, 0..0xFFFF in 16-bit mode (larger values are clamped).
// The DIR is 24 bits, left justified: the code is shifted by 4 (20-bit) or 8 (16-bit).
void DAC1220::writeCode(uint32_t code)
{
    uint32_t MaxCode = Use16Bits ? 0xFFFF : 0xFFFFF;
    if (code > MaxCode)
    {
        code = MaxCode;
    }
    uint32_t SentCode = code << (Use16Bits ? 8 : 4);
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(CSToSCLKMicros);
    SPIConnection->transfer(CommandWriteDIR3Bytes);
    delayMicroseconds(CommandToDataMicros);
    uint8_t Byte0 = (SentCode & 0x00FF0000) >> 16;
    uint8_t Byte1 = (SentCode & 0x0000FF00) >> 8;
    uint8_t Byte2 = (SentCode & 0x000000FF);
    SPIConnection->transfer( Byte0 );
    SPIConnection->transfer( Byte1 );
    SPIConnection->transfer( Byte2 );
    digitalWrite(CSPin, HIGH);
    SPIConnection->endTransaction();
    if (Verbose)
    {
        Serial.print("[DAC1220DBG](");
        Serial.print(Byte0,HEX);
        Serial.print(",");
        Serial.print(Byte1,HEX);
        Serial.print(",");
        Serial.print(Byte2,HEX);
        Serial.print(")\n");
    }
}

// Resets the calibration registers to OCR = 000000h, FCR = 800000h: the uncalibrated output.
void DAC1220::clearCalibration()
{
    writeCommandRegister(CMRByte1 | CMRByte1CRST, FormatByte() | CMRModeNormal);
}

void DAC1220::writeOffsetCalibration(uint32_t ocr)
{
    writeRegister24(CommandWriteOCR3Bytes, ocr);
}

// The FCR multiplies the DIR; changing it widens or narrows the output range about VREF.
void DAC1220::writeFullScaleCalibration(uint32_t fcr)
{
    writeRegister24(CommandWriteFCR3Bytes, fcr);
}

void DAC1220::writeRegister24(uint8_t command, uint32_t value)
{
    uint8_t Byte2 = (value >> 16) & 0xFF;
    uint8_t Byte1 = (value >> 8) & 0xFF;
    uint8_t Byte0 = value & 0xFF;
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(CSToSCLKMicros);
    SPIConnection->transfer(command);
    delayMicroseconds(CommandToDataMicros);
    SPIConnection->transfer(Byte2);
    SPIConnection->transfer(Byte1);
    SPIConnection->transfer(Byte0);
    digitalWrite(CSPin, HIGH);
    SPIConnection->endTransaction();
    delayMicroseconds(CSHighMicros);
    if (Verbose)
    {
        Serial.print("[DAC1220WR](");
        Serial.print(command,HEX);
        Serial.print(":");
        Serial.print(value & 0xFFFFFF,HEX);
        Serial.print(")\n");
    }
}

// The SPI TX pin that drives SDIO. While a read's data bytes are clocked out, the DAC drives SDIO,
// so this pin is switched to an input for that time and handed back to the SPI peripheral after.
void DAC1220::SetReadbackTXPin(uint8_t TXPin)
{
    ReadbackTXPin = TXPin;
}

void DAC1220::releaseTX()
{
    if (ReadbackTXPin < 0)
    {
        return;
    }
#if defined(ARDUINO_ARCH_RP2040)
    gpio_set_dir(ReadbackTXPin, GPIO_IN);
    gpio_set_function(ReadbackTXPin, GPIO_FUNC_SIO);
#else
    pinMode(ReadbackTXPin, INPUT); // AVR: the SPI master leaves MOSI's direction to the DDR bit
#endif
}

void DAC1220::restoreTX()
{
    if (ReadbackTXPin < 0)
    {
        return;
    }
#if defined(ARDUINO_ARCH_RP2040)
    gpio_set_function(ReadbackTXPin, GPIO_FUNC_SPI);
#else
    pinMode(ReadbackTXPin, OUTPUT);
#endif
}

// Reads 1 to 3 bytes starting at address, most significant byte first (BD = 0, MSB = 0).
// Without SDIO wired to the RX pin, this returns whatever the floating RX pin reads.
uint32_t DAC1220::readRegister(uint8_t address, uint8_t numberOfBytes)
{
    if (numberOfBytes < 1)
    {
        numberOfBytes = 1;
    }
    else if (numberOfBytes > 3)
    {
        numberOfBytes = 3;
    }
    uint8_t Command = CommandRead | ((numberOfBytes - 1) << 5) | (address & 0x0F);
    uint32_t Value = 0;
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(CSToSCLKMicros);
    SPIConnection->transfer(Command);
    releaseTX();
    delayMicroseconds(CommandToDataMicros); // t9 >= 13 tXIN; covers t11 + t12 (<= 14 tXIN)
    for (uint8_t i = 0; i < numberOfBytes; i++)
    {
        Value = (Value << 8) | SPIConnection->transfer(0xFF);
    }
    digitalWrite(CSPin, HIGH);
    delayMicroseconds(SDIOReleaseMicros);
    restoreTX();
    SPIConnection->endTransaction();
    delayMicroseconds(CSHighMicros);
    if (Verbose)
    {
        Serial.print("[DAC1220RD](");
        Serial.print(Command,HEX);
        Serial.print(":");
        Serial.print(Value,HEX);
        Serial.print(")\n");
    }
    return Value;
}

uint16_t DAC1220::readCommandRegister()
{
    return (uint16_t)readRegister(AddressCMR, 2);
}

uint32_t DAC1220::readOffsetCalibration()
{
    return readRegister(AddressOCR, 3);
}

uint32_t DAC1220::readFullScaleCalibration()
{
    return readRegister(AddressFCR, 3);
}

uint32_t DAC1220::readCode()
{
    return readRegister(AddressDIR, 3) >> (Use16Bits ? 8 : 4);
}

// True when the CMR reads back as written: bit 13 is 1, bits 12, 10 and 8 are 0 (Table 13; bit 11's
// reset value is given inconsistently, so it is not checked), ADPT and CALPIN are 0, and byte 0
// holds the format set by selfcalibrate() in Normal mode. A disconnected RX pin reads 0000h or
// FFFFh and fails.
bool DAC1220::ReadbackWorks()
{
    uint16_t CMR = readCommandRegister();
    bool FixedBits = ((CMR & 0x2000) == 0x2000) && ((CMR & 0x1500) == 0);
    bool Byte1Bits = (CMR & 0xC000) == 0;
    bool Format = (CMR & 0x00FF) == (FormatByte() | CMRModeNormal);
    return FixedBits && Byte1Bits && Format;
}
