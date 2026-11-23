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
*/

#include <Arduino.h>
#include "SPI.h"
#include "dac1220.h"

// Command register (CMR) values, datasheet Tables 12 and 13.
// Byte 1 (address 4): ADPT 0 (adaptive filter on), CALPIN 0 (output disconnected during calibration),
// bit 13 written 1 (required on first-revision parts, read-only 1 on current parts).
static const uint8_t CMRByte1 = 0x20;
// Byte 0 (address 5): RES (bit 7, 1 = 20-bit), CLR 0, DF (bit 5, 1 = straight binary), DISF 0,
// BD 0 (address increments: MSB first), MSB 0 (bits MSB first), MD (bits 1-0).
static const uint8_t CMRByte0Res20 = 0x80;
static const uint8_t CMRByte0StraightBinary = 0x20;
static const uint8_t CMRModeNormal = 0x00;
static const uint8_t CMRModeSelfCalibration = 0x01;
// Command bytes (Table 6): R/W 0 (write), MB = number of bytes - 1, ADR = start address.
static const uint8_t CommandWriteCMR2Bytes = 0x24;  // write, 2 bytes, address 4 (CMR MSB)
static const uint8_t CommandWriteDIR3Bytes = 0x40;  // write, 3 bytes, address 0 (DIR MSB)
// Delays, generous for a 2.5 MHz XIN (tXIN 400 ns), Table 4.
static const uint32_t CSToSCLKMicros = 12;        // t10: CS falling to first SCLK rising, >= 11 tXIN
static const uint32_t CommandToDataMicros = 15;   // t9: command byte to first data byte, >= 13 tXIN
static const uint32_t CSHighMicros = 20;          // t15: CS high between commands, >= 22 tXIN
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
void DAC1220::selfcalibrate()
{
    uint8_t Format = CMRByte0StraightBinary | (Use16Bits ? 0x00 : CMRByte0Res20);
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
