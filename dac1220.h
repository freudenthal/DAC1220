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
*/

#ifndef _DAC1220_H_
#define _DAC1220_H_
#include <Arduino.h>
#include "SPI.h"

class DAC1220
{
    public:
        DAC1220(SPIClass* spiconnection, bool uselowresolution, uint8_t cs, uint8_t sclk, float clockperiodmicroseconds);
        void begin();
        void reset();
        void writeV(float v);
        void writeCode(uint32_t code);
        void selfcalibrate();
    private:
        SPIClass* SPIConnection;
        bool Use16Bits;
        SPISettings ConnectionSettings;
        uint8_t Resolution;
        uint8_t CSPin;
        uint8_t SCLKPin;
        float ClockPerioduS;
};

#endif