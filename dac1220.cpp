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
*/

#include <Arduino.h>
#include "SPI.h"
#include "dac1220.h"

DAC1220::DAC1220(SPIClass* spiconnection, bool uselowresolution, uint8_t cs, uint8_t sclk, float clockperiodmicroseconds)
{
    Use16Bits = uselowresolution;
    SPIConnection = spiconnection;
    ConnectionSettings = SPISettings(100000,MSBFIRST,SPI_MODE1);
    CSPin = cs;
    SCLKPin = sclk;
    ClockPerioduS = clockperiodmicroseconds;
}

void DAC1220::begin()
{
    selfcalibrate();
}

//Call SPI.end prior, and SPI.begin after to re-establish SPI connection, or prior to SPI.begin in start()
void DAC1220::reset()
{
    uint32_t Delay0 = (uint32_t)(600 * ClockPerioduS); //First high period (600 clocks 512min 800max)
    uint32_t Delay1 = (uint32_t)(1200 * ClockPerioduS); //Second high period (1200 clocks 1024min 1800max)
    uint32_t Delay2 = (uint32_t)(2200 * ClockPerioduS); //Second high period (2400 clocks 2048min 2400max)
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

void DAC1220::selfcalibrate()
{
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(12);
    //Start Self-Calibration
    SPIConnection->transfer(0x05);
    delayMicroseconds(15);
    if (Use16Bits)
    {
        SPIConnection->transfer(0x21); //16-bit resolution
    }
    else
    {
        SPIConnection->transfer(0xA1); //20-bit resolution
    }
    delayMicroseconds(12);
    digitalWrite(CSPin, HIGH);
    SPIConnection->endTransaction();
    //Wait 500ms for Self-Calibration to complete
    delay(500);
}

bool DAC1220::Is16Bit()
{
    return Use16Bits;
}

void DAC1220::writeV(float v)
{
    uint32_t code;
    if (v < 0.0)
    {
        v = 0.0;
    }
    else if (v > 5.0)
    {
        v = 5.0;
    }
    if (Use16Bits)
    {
        code = (uint32_t)(v/5 * 0xFFFF + 0.5);
        if (code > 0xFFFF)
        {
            code = 0xFFFF;
        }
        writeCode(code << 8);
    }
    else
    {
        code = (uint32_t)(v/5 * 0xFFFFF + 0.5);
        writeCode(code << 4);
        if (code > 0xFFFFF)
        {
            code = 0xFFFFF;
        }
    }
}

bool DAC1220::GetVerbose()
{
    return Verbose;
}

void DAC1220::SetVerbose(bool VerboseToSet)
{
    Verbose = VerboseToSet;
}

void DAC1220::writeCode(uint32_t code)
{
    uint32_t SentCode = code << 4;
    if (SentCode > 16777215)
    {
        SentCode = 16777215;
    }
    SPIConnection->beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(12);
    SPIConnection->transfer(0x40);
    delayMicroseconds(15);
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
