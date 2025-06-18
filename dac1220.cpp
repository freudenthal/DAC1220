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

DAC1220::DAC1220(bool uselowresolution, uint8_t cs, uint8_t sclk, float clockperiodmicroseconds)
{
    Use16Bits = uselowresolution;
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
    SPI.beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(12);
    //Start Self-Calibration
    SPI.transfer(0x05);
    delayMicroseconds(15);
    if (Use16Bits)
    {
        SPI.transfer(0x21); //16-bit resolution
    }
    else
    {
        SPI.transfer(0xA1); //20-bit resolution
    }
    delayMicroseconds(12);
    digitalWrite(CSPin, HIGH);
    SPI.endTransaction();
    //Wait 500ms for Self-Calibration to complete
    delay(500);
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


void DAC1220::writeCode(uint32_t code)
{
    SPI.beginTransaction(ConnectionSettings);
    digitalWrite(CSPin, LOW);
    delayMicroseconds(12);
    SPI.transfer(0x40);
    delayMicroseconds(15);
    SPI.transfer( (code & 0x00FF0000) >> 16 );
    SPI.transfer( (code & 0x0000FF00) >> 8 );
    SPI.transfer( (code & 0x000000FF) );
    digitalWrite(CSPin, HIGH);
    SPI.endTransaction();
}
