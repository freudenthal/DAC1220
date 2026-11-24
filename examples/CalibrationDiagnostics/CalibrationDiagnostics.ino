/*
DAC1220 calibration diagnostics.

Symptom this is for: the output is right at mid-scale (VREF) but the range is narrowed about it,
e.g. about 0.2 V at code 0 and 4.8 V at full scale. The DAC1220 applies its Full-Scale Calibration
Register (FCR) multiplicatively, and the datasheet says changing it "can change the range of
voltages that are output for the same digital codes, centered on VREF" -- the same shape. So the
question is whether the FCR that self-calibration produced is wrong, or the hardware is.

Serial commands (115200 baud, one per line):
  pct <p>     output p percent of full scale (0 to 100)
  code <n>    write a raw code (decimal; 0..1048575 in 20-bit mode)
  cal         run self-calibration again (about 0.5 s), VOUT disconnected during it (CALPIN = 0)
  calpin      run self-calibration with VOUT connected (CALPIN = 1), so the load on VOUT is seen
  clr         clear calibration: OCR = 000000h, FCR = 800000h (uncalibrated)
  fcr <hex>   write the FCR, e.g. fcr 800000
  ocr <hex>   write the OCR (24-bit two's complement), e.g. ocr 000000
  regs        read back CMR, OCR, FCR and DIR (needs the read-back wiring below)
  verbose     toggle the library's register traces

Test sequence (works on a write-only board; no read-back needed):
  1. pct 0, pct 50, pct 100: measure. This is the start-up calibration (begin()).
  2. clr, then pct 0, pct 50, pct 100 again. If the range opens up to roughly 0 to 5 V, the
     self-calibration result is at fault, not the analog path. If it is still about 0.2 to 4.8 V,
     calibration is not the cause: look at the output load (0.5 mA maximum), the filter
     capacitors C1/C2 (20-bit mode: 10 nF and 3.3 nF), and the reference.
  3. cal (now that everything has been powered for a while), then measure again. If the range is
     now right, the start-up calibration ran before the clock, supplies or reference had settled.
  4. calpin, then measure. If the range is now right, something loading VOUT is what the normal
     (CALPIN = 0) calibration cannot see; check what VOUT drives. VOUT moves during this run.
  5. Optional workaround: after clr, step the FCR (fcr 880000, fcr 8C0000, ...) and measure the
     slope; the relationship is linear, so two points give the FCR for the gain you want.

Read-back wiring: SDIO must reach the SPI bus's RX pin as well as its TX pin. On the PEM/PEMCSC
controller rev01, RX is GP28 (not connected): add a wire from GP28 to the DAC1220 SDIO pin.
SetReadbackTXPin() makes the library release TX (GP11) while the DAC drives SDIO.
*/

#include <SPI.h>
#include <dac1220.h>

#if defined(ARDUINO_ARCH_RP2040)
// PEM/PEMCSC controller rev01 (PEM_CSC.ino): SPI1, 2.5 MHz DAC clock.
#define DAC_SPI SPI1
static const uint8_t TXPin = 11;   // GP11 -> SDIO
static const uint8_t SCKPin = 10;  // GP10 -> SCLK
static const uint8_t RXPin = 28;   // GP28, wire to SDIO for read-back
static const uint8_t CSPin = 13;   // GP13 -> CS
// GP15 drives the heater through an inverting opto/MOSFET: LOW or undriven = heater ON. Hold it
// HIGH (heater off) for as long as this sketch runs.
static const uint8_t HeaterPin = 15;
void initVariant()
{
    gpio_init(HeaterPin);
    gpio_put(HeaterPin, 1);
    gpio_set_dir(HeaterPin, GPIO_OUT);
}
#else
// Other boards: the default SPI bus. Set TXPin to the board's MOSI and CSPin to the DAC's CS.
#define DAC_SPI SPI
static const uint8_t TXPin = MOSI;
static const uint8_t SCKPin = SCK;
static const uint8_t CSPin = 10;
#endif

DAC1220 Dac(&DAC_SPI, false, CSPin, SCKPin, 1.0 / 2.5); // 20-bit, XIN period 0.4 us

char Line[32];
uint8_t LineLength = 0;

void setup()
{
    Serial.begin(115200);
    pinMode(CSPin, OUTPUT);
    digitalWrite(CSPin, HIGH);
#if defined(ARDUINO_ARCH_RP2040)
    DAC_SPI.setTX(TXPin);
    DAC_SPI.setSCK(SCKPin);
    DAC_SPI.setRX(RXPin);
    DAC_SPI.begin(false);
#else
    DAC_SPI.begin();
#endif
    Dac.SetReadbackTXPin(TXPin);
    Dac.begin(); // waits out the I/O recovery timeout, sets the format, self-calibrates
    Dac.writeCode(0);
    delay(2000); // time to open the serial monitor
    Serial.println("DAC1220 calibration diagnostics. Commands: pct, code, cal, calpin, clr, fcr, ocr, regs, verbose");
}

void loop()
{
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (LineLength > 0)
            {
                Line[LineLength] = '\0';
                runCommand(Line);
                LineLength = 0;
            }
        }
        else if (LineLength < sizeof(Line) - 1)
        {
            Line[LineLength++] = c;
        }
    }
}

void printHex24(uint32_t value)
{
    char Text[8];
    snprintf(Text, sizeof(Text), "%06lX", (unsigned long)(value & 0xFFFFFF));
    Serial.print(Text);
}

void runCommand(char* command)
{
    char* Argument = strchr(command, ' ');
    if (Argument != NULL)
    {
        *Argument++ = '\0';
    }
    if (strcmp(command, "pct") == 0 && Argument != NULL)
    {
        float Percent = atof(Argument);
        Dac.writeV(Percent / 100.0 * 5.0);
        Serial.print("Output set to ");
        Serial.print(Percent, 3);
        Serial.println(" % of full scale");
    }
    else if (strcmp(command, "code") == 0 && Argument != NULL)
    {
        uint32_t Code = strtoul(Argument, NULL, 10);
        Dac.writeCode(Code);
        Serial.print("Code ");
        Serial.println(Code);
    }
    else if (strcmp(command, "cal") == 0)
    {
        Dac.selfcalibrate();
        Serial.println("Self-calibration done; re-send pct/code before measuring");
    }
    else if (strcmp(command, "calpin") == 0)
    {
        Dac.selfcalibrate(true);
        Serial.println("Self-calibration (CALPIN = 1, VOUT connected) done; re-send pct/code before measuring");
    }
    else if (strcmp(command, "clr") == 0)
    {
        Dac.clearCalibration();
        Serial.println("Calibration cleared: OCR = 000000, FCR = 800000");
    }
    else if (strcmp(command, "fcr") == 0 && Argument != NULL)
    {
        uint32_t Value = strtoul(Argument, NULL, 16);
        Dac.writeFullScaleCalibration(Value);
        Serial.print("FCR written: ");
        printHex24(Value);
        Serial.println();
    }
    else if (strcmp(command, "ocr") == 0 && Argument != NULL)
    {
        uint32_t Value = strtoul(Argument, NULL, 16);
        Dac.writeOffsetCalibration(Value);
        Serial.print("OCR written: ");
        printHex24(Value);
        Serial.println();
    }
    else if (strcmp(command, "regs") == 0)
    {
        bool Works = Dac.ReadbackWorks();
        Serial.print("CMR ");
        Serial.print(Dac.readCommandRegister(), HEX);
        Serial.print("  OCR ");
        printHex24(Dac.readOffsetCalibration());
        Serial.print("  FCR ");
        printHex24(Dac.readFullScaleCalibration());
        Serial.print("  code ");
        Serial.println(Dac.readCode());
        if (!Works)
        {
            Serial.println("Read-back check failed: these values are not from the DAC (is SDIO wired to RX?)");
        }
    }
    else if (strcmp(command, "verbose") == 0)
    {
        Dac.SetVerbose(!Dac.GetVerbose());
        Serial.println(Dac.GetVerbose() ? "Verbose on" : "Verbose off");
    }
    else
    {
        Serial.println("Unknown command");
    }
}
