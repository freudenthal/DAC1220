# DAC1220 output-range compression: test report

**Date:** 2026-11-24
**Branch:** `debug/dac-scaling` (commits `38802ae` and `a0976f8`, on top of `82239dd`)
**Hardware:** PEM/PEMCSC controller rev01, RP2040, DAC1220E
**Status:** The cause is not found yet, but it is almost certainly in the hardware. The firmware, the library and the start-up timing are ruled out. A load on VOUT is unlikely.

## Summary

The DAC1220 output is correct at mid-scale, but the whole range is squeezed evenly toward VREF. With the start-up self-calibration, 0% gives about 0.2 V and 100% gives about 4.8 V, where 0 V and 5 V are expected.

The tests showed the following:

- **The firmware and library are not the cause.** Every code reached the DAC. With calibration cleared, the output is a clean straight line through mid-scale, and the DAC follows hand-written gain values exactly.
- **The DAC's uncalibrated gain is about 0.81**, an error of about 19%. The datasheet's post-calibration spec is ±150 ppm, so this is far outside what the part should show.
- **Self-calibration corrects the offset but only about half of the gain error.** It leaves a gain of about 0.92, which is the reported symptom.
- **Neither start-up timing nor a load on VOUT explains it.** Re-calibrating once the board had settled gives the same result. So does calibrating with VOUT connected (`CALPIN = 1`).
- **The remaining suspects are the DAC's filter capacitors, the board around them, and the chip itself.** The checks still to be done are listed under [Recommended next steps](#recommended-next-steps).

## Symptom as reported

| Setting | Expected | Measured |
|---|---|---|
| 0% | 0 V | ~0.2 V |
| 25% | 1.25 V | ~1.36 V |
| 50% | 2.5 V | 2.5 V |
| 75% | 3.75 V | ~3.65 V |
| 100% | 5.0 V | ~4.8 V |

These readings fit `Vout = 2.5 + 0.92 × (Videal − 2.5)` to within about 10 mV. That makes it a pure gain error centered on VREF.

## Code review of the library

Before any hardware testing, the library's voltage-to-code path ([dac1220.cpp](dac1220.cpp)) was checked against the datasheet (TI SBAS082G):

- **Codes are correct.** 0% sends `000000h`, 50% sends `800000h` and 100% sends `FFFFF0h` to the left-justified 24-bit data register.
- **A 16-bit/20-bit mismatch can't cause this.** Because the data is left-justified, 16-bit `code << 8` and 20-bit `code << 4` give the same voltage.
- **A scaling bug would look different.** A wrong code-scaling factor would pivot about 0 V, not about VREF, and would usually be a power of two. A data-format mismatch, such as two's complement instead of straight binary, would put 2.5 V at 0% and wrap around.
- **The shape matches a wrong gain calibration.** The datasheet says the Full-Scale Calibration Register (FCR) "can change the range of voltages that are output for the same digital codes, centered on VREF", which is exactly this symptom.

So the investigation moved to the calibration registers and the hardware.

## Test setup

- **Board:** PEM/PEMCSC controller rev01, RP2040 (arduino-pico core 6.4.0). The DAC1220 is on `SPI1`: SDIO on GP11 (TX), SCLK on GP10, CS on GP13. The DAC clock (XIN) is 2.5 MHz and the DAC runs in 20-bit mode.
- **Read-back:** not possible on this board. GP28 (SPI1 RX) is not connected to SDIO on rev01, so the DAC1220 is write-only.
- **Heater:** disconnected in hardware for all tests. The diagnostic sketch also holds GP15 high (heater off) from start-up.
- **Reference:** VREF measured at **2.5003 V**.
- **Firmware:** [examples/CalibrationDiagnostics/CalibrationDiagnostics.ino](examples/CalibrationDiagnostics/CalibrationDiagnostics.ino), built with arduino-cli 1.6.0 and flashed by UF2. Commands were sent over USB serial, and VOUT was read with a meter.

### Library additions used by the tests

These are on `debug/dac-scaling`. They are additive, so existing calls (`begin()`, `writeCode()`, `writeV()`) behave as before.

| Function | Purpose |
|---|---|
| `clearCalibration()` | Sets the CRST bit, which resets OCR to `000000h` and FCR to `800000h` (uncalibrated). |
| `writeFullScaleCalibration()`, `writeOffsetCalibration()` | Write the FCR and OCR directly. These need no read-back. |
| `selfcalibrate(bool outputConnected = false)` | `true` sets CALPIN, so VOUT stays connected during calibration. The default is unchanged. |
| `readRegister()`, `readCommandRegister()`, `readOffsetCalibration()`, `readFullScaleCalibration()`, `readCode()` | Register read-back, for boards where SDIO also reaches the RX pin. |
| `SetReadbackTXPin()` | Releases the TX pin while the DAC drives SDIO. |
| `ReadbackWorks()` | Sanity check on the command register, so an unconnected RX pin isn't mistaken for real data. |

## Results

All voltages were measured at VOUT.

| # | Condition | 0% | 50% | 100% |
|---|---|---|---|---|
| 1 | Start-up self-calibration (`begin()`) | ~0.2 V | ~2.5 V | ~4.8 V |
| 2 | Calibration cleared (`clr`: OCR = `000000h`, FCR = `800000h`) | 0.4335 V | 2.4684 V | 4.5029 V |
| 3 | Self-calibration re-run after a power cycle (`cal`, CALPIN = 0) | — | — | ~4.8 V |
| 4 | Cleared, then FCR = `900000h` written by hand | — | — | 4.7579 V |
| 5 | Self-calibration with VOUT connected (`calpin`, CALPIN = 1) | — | — | 4.7888 V |
| 6 | Register read-back (`regs`) | — | — | — |

The 50% value in test 1 comes from the original report; it wasn't re-measured. In test 6, every register read as zero and `ReadbackWorks()` failed. That is expected, because RX isn't wired to SDIO.

### Test 1: start-up calibration

The problem reproduced with the diagnostic firmware: about 0.2 V at 0% and about 4.8 V at 100%.

### Test 2: uncalibrated output

| Quantity | Value |
|---|---|
| Span (0% to 100%) | 4.0694 V, against an ideal 2 × VREF = 5.0006 V |
| Gain | 0.814, an error of about −19% |
| Mid-scale offset | 2.4684 − 2.5003 = −31.9 mV |
| Swing below mid-scale | 2.0349 V |
| Swing above mid-scale | 2.0345 V |
| Midpoint of the two end-points | 2.4682 V, against 2.4684 V measured at 50% |

The output is linear and symmetric about mid-scale, so this is a pure gain error plus a small offset. The range got *narrower* with calibration cleared, so self-calibration is improving the gain, not causing the error.

### Test 3: re-calibration after settling

Self-calibration was re-run with the board powered and settled. It gave the same ~4.8 V at 100%, so the start-up calibration is not running too early, before the clock, supplies or reference have settled.

### Test 4: hand-written FCR

With calibration cleared and FCR set to `900000h`, the swing above mid-scale went from 2.0345 V to 2.2895 V. That is a factor of **1.1253**, against an expected `900000h / 800000h` = **1.125**. This assumes mid-scale stays at 2.4684 V, which follows from the FCR scaling about mid-scale; mid-scale wasn't re-measured.

This shows two things:

- **The SPI writes reach the DAC** and the FCR is applied exactly as the datasheet describes.
- **The full range is reachable.** By proportion, a 2.5003 V swing needs an FCR of about `9D4E00h`.

Working back from the calibrated 100% readings in the same way, the start-up self-calibration lands at about `90B000h`. The CALPIN = 1 calibration lands at about `8FFB00h`. Both estimates assume self-calibration also removes the offset, so that mid-scale sits at VREF.

### Test 5: CALPIN = 1

The normal calibration (CALPIN = 0) disconnects VOUT, so it can't see anything loading the pin. Calibrating with VOUT connected gave 4.7888 V at 100%, about the same as the normal ~4.8 V. This calibration should largely correct for a load on VOUT, so a load is unlikely to be the cause. The verbose trace confirmed the expected command-register writes: `60 A0`, then `60 A1`.

## Conclusions

| Possible cause | Verdict | Evidence |
|---|---|---|
| Library code scaling or a 16-/20-bit mismatch | Ruled out | Code review; test 2 is linear and centered near VREF |
| Data-format or command-register setup | Ruled out | The output is monotonic and doesn't wrap; the command bytes were verified in the trace |
| SPI writes not reaching the DAC | Ruled out | Test 4: the FCR change took effect exactly as predicted |
| Self-calibration running before things settle | Ruled out | Test 3: same result once settled |
| A load on VOUT | Unlikely | Test 5: calibrating with VOUT connected doesn't change the result |
| Leakage or wrong parts at the filter capacitors C1/C2 | Open, most likely | Large uncalibrated gain error that calibration can't remove |
| A damaged or non-genuine DAC1220 | Open | Same evidence |

**Why the filter capacitors are suspected:** in the datasheet's Figure 5, C1 connects between the C1 pin (pin 9) and VREF, and C2 between the C2 pin (pin 10) and VOUT. Leakage across C1, or from that node to the VREF guard ring, is a resistive path to 2.5 V inside the output filter. It would pull the whole range toward VREF in exactly this way. Flux residue, contamination or a lossy capacitor could all do that. This is inferred from the measurements and the datasheet; it hasn't been confirmed on the board.

## Recommended next steps

All of these are done with the power off.

1. **Check for leakage at the filter capacitors.** Measure the resistance across C1 (pin 9 to VREF) and C2 (pin 10 to VOUT). Each should read open or many megohms; anything lower is a leakage path. Clean the area around pins 9 and 10 with isopropyl alcohol either way, then re-test.
2. **Check the capacitor values and types.** 20-bit mode needs C1 = 10 nF and C2 = 3.3 nF, film or C0G. Check that they aren't swapped, aren't the 16-bit values (2.2 nF and 0.22 nF), and aren't a lossy dielectric.
3. **Try the chip.** If the capacitors and board check out, swap in a known-good DAC1220, or run the same firmware on another board.

After each change, re-run the diagnostic sequence: `clr` and then `pct 0`, `pct 50`, `pct 100`; then `cal` and the same three readings. The fault is fixed when the calibrated readings come out at about 0 V, 2.5 V and 5.0 V. The uncalibrated span should also come out far closer to 5.0 V than the 4.07 V measured here.

**Workaround, if one is needed before the fix:** write the FCR by hand (about `9D4E00h`) and an OCR value that cancels the −32 mV mid-scale offset. This should give close to 0–5 V, but it hides the fault instead of fixing it. The value would also need checking against temperature and over time.

**Read-back on future boards:** a wire from GP28 to the DAC1220's SDIO pin lets `regs` show the actual OCR and FCR that self-calibration produces, instead of values inferred from voltages.

## Other findings

These came up during the review. None of them causes the symptom.

- [DAC1220-SERIALINPUT.ino](DAC1220-SERIALINPUT.ino) calls a constructor that no longer exists, so it doesn't compile. It also calls `reset()` before every write. `reset()` clears the format setting and the calibration, and nothing restores them. Code that calls `reset()` should follow it with `begin()`.

## State after testing

- **Controller firmware restored.** `PEM_CSC` at commit `1f3b62d` was rebuilt against this library's `master` (`82239dd`) and flashed. It answers `*IDN?` with `Hinds PEM controller CSC V02`.
- **Not a byte-for-byte restore.** The original firmware couldn't be read off the board before testing, so this is a rebuild from source.
- **Settings should be intact but weren't checked.** The 64 KB filesystem area wasn't rewritten by any upload, so saved settings should survive. Check them anyway.
- **Library changes are on `debug/dac-scaling` only.** They haven't been merged into `master` or pushed.
