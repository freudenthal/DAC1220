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
        // outputConnected sets CALPIN: false (default) disconnects VOUT during calibration, so a
        // load on VOUT is not seen; true keeps VOUT connected (it moves during the ~0.5 s run).
        void selfcalibrate(bool outputConnected = false);
        bool Is16Bit();
        bool GetVerbose();
        void SetVerbose(bool VerboseToSet);

        // Calibration registers (datasheet: OCR at address 8, FCR at address 12, 24 bits each).
        // Writes need only SDIO as an input, so they work on a write-only board.
        void clearCalibration();                      // CRST: OCR = 000000h, FCR = 800000h (uncalibrated)
        void writeOffsetCalibration(uint32_t ocr);    // 24-bit two's complement, added to the DIR
        void writeFullScaleCalibration(uint32_t fcr); // 24-bit gain constant, multiplies the DIR

        // Register read-back. Needs SDIO wired to the SPI bus's RX (MISO) pin as well as its TX
        // (MOSI) pin. Set the TX pin with SetReadbackTXPin() so the library releases it while the
        // DAC drives SDIO; otherwise TX needs a series resistor (about 1 kOhm) to SDIO.
        void SetReadbackTXPin(uint8_t TXPin);
        // RP2040 only: read the data bytes by bit-banging SCLK and sampling the TX pin, for boards
        // where SDIO reaches only the TX pin (no RX wire). Needs SetReadbackTXPin(). The DAC then
        // drives SDIO (5 V logic) back toward the TX pin, so the board needs a clamp on that pin.
        void SetReadbackBitBang(bool enable);
        uint32_t readRegister(uint8_t address, uint8_t numberOfBytes); // 1 to 3 bytes, MSB first
        uint16_t readCommandRegister();
        uint32_t readOffsetCalibration();
        uint32_t readFullScaleCalibration();
        uint32_t readCode();          // the DIR, right-aligned like writeCode()'s argument
        bool ReadbackWorks();         // CMR read-back sanity check (fixed bits and the format just set)
    private:
        SPIClass* SPIConnection;
        bool Use16Bits;
        SPISettings ConnectionSettings;
        uint8_t Resolution;
        uint8_t CSPin;
        uint8_t SCLKPin;
        float ClockPerioduS;
        bool Verbose;
        int16_t ReadbackTXPin;        // -1: not set, TX is left driving during reads
        bool ReadbackBitBang;         // true: read data bytes by bit-banging SCLK, sampling TX
        void writeCommandRegister(uint8_t Byte1, uint8_t Byte0);
        void writeRegister24(uint8_t command, uint32_t value);
        uint8_t FormatByte();
        void releaseTX();
        void restoreTX();
        uint8_t bitBangReadByte();
};

#endif