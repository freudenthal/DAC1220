# DAC1220 output-range compression: test report

**Date:** 2026-11-24
**Branch:** `debug/dac-scaling` (commits `38802ae` and `a0976f8`, on top of `82239dd`)
**Hardware:** PEM/PEMCSC controller rev01, RP2040, DAC1220E
**Status:** The cause is not found yet, but it is almost certainly in the hardware. The firmware, the library and the start-up timing are ruled out. A load on VOUT is unlikely. A hand-set gain value works around the fault to within about 0.7 mV between 10% and 90%. See [Workaround: hand-set gain value](#workaround-hand-set-gain-value). VOUT also oscillates at about 12 kHz: about 1 mV RMS without C2, and about 65 mV RMS (175 mV peak-to-peak) with C2 fitted. See [Noise and oscillation at VOUT](#noise-and-oscillation-at-vout).

> **Added after testing:** the output filter capacitor C2 (pin 10 to VOUT) was not fitted during any of these tests. The datasheet treats it as a required part of the output filter. Anecdotally, though, the fault first appeared with C2 fitted (3300 pF), and removing it made no noticeable difference. So the missing C2 probably isn't the cause, but this hasn't been confirmed by measurement. See [Conclusions](#conclusions).
>
> **Later:** C2 was refitted for the noise tests, with the 0 Ω resistor still out. With C2 fitted, the output oscillates strongly. The gain with C2 fitted hasn't been measured yet.

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
- **Removed parts:** C2 (3.3 nF, pin 10 to VOUT) was **not fitted** for any test. A 0 Ω resistor had also been removed, and stayed out for every test. C1's value and type weren't checked.
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
| Leakage or wrong parts at C1 | Open, most likely | Large uncalibrated gain error that calibration can't remove |
| A damaged or non-genuine DAC1220 | Open | Same evidence |
| C2 not fitted | Probably not the cause, unconfirmed | Missing for every gain test, but anecdotally the fault was present with C2 fitted and unchanged by removing it |
| A fault in the DAC's output stage, or a board that doesn't match the datasheet circuit | Open | VOUT oscillates at about 12 kHz, and fitting C2 makes it about 60 times larger (see [Noise and oscillation at VOUT](#noise-and-oscillation-at-vout)) |

**About C2:** C2 was found to be unfitted after testing. The datasheet's block diagram puts it in the "continuous time post filter" that drives VOUT, and lists 3.3 nF for 20-bit mode with no option to leave it off. So all of these results were taken outside the datasheet's operating conditions. However, the fault was reportedly first seen with C2 fitted (3300 pF), and removing it reportedly made no noticeable difference to the output. That account is anecdotal. If it holds, the missing C2 isn't the cause, though it may still add a small error of its own. Fitting C2 and repeating test 2 would confirm it.

**Why C1 is suspected:** in the datasheet's Figure 5, C1 connects between the C1 pin (pin 9) and VREF, and C2 between the C2 pin (pin 10) and VOUT. Leakage across C1, or from that node to the VREF guard ring, is a resistive path to 2.5 V inside the output filter. It would pull the whole range toward VREF in exactly this way. Flux residue, contamination or a lossy capacitor could all do that. This is inferred from the measurements and the datasheet; it hasn't been confirmed on the board.

## Workaround: hand-set gain value

These tests were run later the same day, with the same setup: C2 not fitted, heater disconnected, VREF 2.5003 V. Every reading in this section is precise; none is approximate.

### Baseline: start-up self-calibration

| Setting | Ideal | Measured |
|---|---|---|
| 0% | 0 V | 0.2090 V |
| 50% | 2.5003 V | 2.5003 V |
| 100% | 5.0006 V | 4.7909 V |

The span is 4.5819 V, a gain of 0.916. The swing is 2.2913 V below mid-scale and 2.2906 V above it, so the error is symmetric. Mid-scale equals VREF, so self-calibration removes the offset completely.

### Keeping the offset and overwriting only the FCR

The FCR was written straight after self-calibration, with no `clr`, so self-calibration's OCR stayed in place.

| Setting | Ideal | FCR `9D4E41h` | FCR `9D30D8h` (final) |
|---|---|---|---|
| 0% | 0 V | 0.0009 V | 0.0016 V |
| 10% (code 104858) | 0.5001 V | 0.4983 V | 0.5000 V |
| 25% (code 262144) | 1.2502 V | — | 1.2504 V |
| 50% | 2.5003 V | 2.5003 V | — |
| 75% (code 786432) | 3.7505 V | — | 3.7498 V |
| 90% (code 943718) | 4.5005 V | 4.5017 V | 4.5001 V |
| 100% | 5.0006 V | 4.9981 V | 4.9983 V |

- **The first estimate was close.** `9D4E41h` was worked out from test 2 and test 4 for a 2.5003 V swing about mid-scale. Measured between 10% and 90%, its gain was 1.00073.
- **The final value is `9D30D8h`.** It is `9D4E41h` divided by 1.00073. At this value, 10% to 90% reads within 0.5 mV of ideal, a gain error of about 100 ppm.
- **Mid-scale didn't move when the FCR changed.** It stayed at VREF, so no fixed OCR is needed. Self-calibration's offset correction is kept, and it still adjusts for offset drift at every start-up.
- **The ends are limited by the supply rails.** 0% stops at 1.6 mV and 100% at 4.9983 V. No calibration value changes this. The gain was therefore fitted at 10% and 90%, not at the ends.
- **The 75% point is 0.7 mV low.** That is more non-linearity than a healthy DAC1220 should show. It may come from the same fault, or from the missing C2.

### Limits of the workaround

- **It hides the fault instead of fixing it.**
- **The value is specific to this board.** It must not be used on another board.
- **It was measured without C2 fitted.** Measure it again once C2 is fitted.
- **It hasn't been checked over time.** Self-calibration adjusts for temperature and drift at every start-up, but this fixed value doesn't. It hasn't been checked after a power cycle, after warm-up or on another day. Recheck 10% and 90% after a power-up to see whether it drifts.

### In the controller firmware

`PEM_CSC.ino`, on the controller repo's `debug/dac-scaling` branch, now has a constant `DACFullScaleCalibration = 0x9D30D8`. `setup()` writes it with `writeFullScaleCalibration()` straight after `DigitalToAnalogue.begin()`. Setting the constant to 0 keeps the self-calibration's own value. This needs the library's `debug/dac-scaling` branch, because `master` doesn't have `writeFullScaleCalibration()`.

The updated firmware was flashed and checked just after power-up, with codes set by `:DEB:DAC`:

| Setting | Ideal | Controller firmware | Diagnostic sketch (earlier) |
|---|---|---|---|
| 10% (code 104858) | 0.5001 V | 0.5005 V | 0.5000 V |
| 50% (code 524288) | 2.5003 V | 2.5003 V | 2.5003 V |
| 90% (code 943718) | 4.5005 V | 4.4995 V | 4.5001 V |

All three readings are within 0.5 mV of ideal, and mid-scale is still at VREF. The 10% to 90% span came out at 3.9990 V, against 4.0001 V with the diagnostic sketch, a gain about 300 ppm lower with the same FCR. The board had been running for a while during the diagnostic tests but had just been power-cycled for this one, so warm-up is the likely difference. This wasn't checked further, and the result was accepted as good enough.

## Noise and oscillation at VOUT

These tests were run later the same day, first without C2 and then with C2 fitted. The 0 Ω resistor stayed out for both.

### Setup

- **Oscilloscope:** Tektronix DPO4104, controlled over USB.
- **Probe:** 1× passive probe on a 2 cm wire soldered to a VOUT pad about 8 mm from the DAC. The ground clip was on TP6.
- **Scope settings:** AC coupling, high-res acquisition, 20 MHz bandwidth limit unless the text says "full bandwidth" (1 GHz).
- **Board:** controller firmware with the gain workaround. The DAC was set by `:DEB:DAC` and held at mid-scale (code 524288) unless stated otherwise. The heater was disconnected.
- **Noise floor:** the probe tip was moved to TP6, with the ground clip left where it was. This was measured once, without C2.
- **Analysis:** RMS for each decade of frequency, and spectrum peaks, from 10,000- or 100,000-point records.

The scripts and the raw captures are outside this repo, in `~/Arduino/.llm/scratch/pemcsc-dac-scaling/`. The captures are kept only on that PC.

### Without C2

| Component | VOUT | Noise floor | Verdict |
|---|---|---|---|
| 1–100 Hz | 5–32 µV RMS | 4–22 µV, mostly 30 and 60 Hz | Near the floor |
| 100 Hz–10 kHz | 35–42 µV per decade | 9–14 µV | Real, but small |
| **Tone at 11.75 kHz, plus harmonics** | **1.09 mV RMS** (harmonics 0.49, 0.27, 0.16 mV) | 10–13 µV in that decade | **Real, from the DAC output path** |
| Spikes every 2.46 µs (406 kHz), 40–50 ns wide | ~12 mV peak | ~9 mV peak | Mostly pickup by the probe |
| 30–37 MHz | 2.7 mV RMS (15 mV at full bandwidth) | 1.4 mV (5.8 mV at full bandwidth) | Mostly pickup; the VOUT wire about doubles it |

The tone's frequency changes with the DAC code, so it comes from the DAC's output path, not from outside interference:

| DAC code | Output | Tone | Fundamental (RMS) |
|---|---|---|---|
| 104858 | 10% | 10.22 kHz | 0.63 mV |
| 524288 | 50% | 11.75 kHz | 1.09 mV |
| 528609 | 50.4% | 11.75 kHz | 1.09 mV |
| 943718 | 90% | 12.97 kHz | 1.26 mV |

A 4321-code step near mid-scale didn't change the tone, but the frequency rises steadily with the output voltage. That looks more like an oscillation in the output stage than an idle tone from the converter. It's an inference, not a confirmed cause.

### With C2 fitted (3300 pF, 0 Ω resistor still out)

Fitting C2 made the oscillation about 60 times larger. The 5 mV/div captures clipped, so these figures are from 100 mV/div:

| Quantity | Without C2 | With C2 |
|---|---|---|
| Fundamental at mid-scale | 11.75 kHz, 1.09 mV RMS | **12.05 kHz, 63 mV RMS** |
| 2nd and 3rd harmonics | 0.49, 0.27 mV | 10.2, 7.6 mV |
| Total RMS (20 MHz limit) | 1.3 mV | **65 mV** |
| Peak-to-peak | ~7 mV | **~175 mV** (177 mV by the scope's own measurement) |

| DAC code | Output | Without C2 | With C2 |
|---|---|---|---|
| 104858 | 10% | 10.22 kHz, 0.63 mV | 11.90 kHz, 62 mV |
| 524288 | 50% | 11.75 kHz, 1.09 mV | 12.05 kHz, 63 mV |
| 528609 | 50.4% | 11.75 kHz, 1.09 mV | 12.05 kHz, 64 mV |
| 943718 | 90% | 12.97 kHz, 1.26 mV | 12.21 kHz, 49 mV |

- **It's the same oscillation, much larger.** The frequency is about the same, so C2 is probably part of the loop that oscillates rather than a new cause.
- **The frequency depends much less on the output voltage.** It moves only 0.3 kHz from 10% to 90%, against 2.75 kHz without C2.
- **The waveform is clearly not a sine.** The harmonics are about 16% of the fundamental.
- **The datasheet's circuit has no such oscillation.** Either the board doesn't match that circuit while the 0 Ω resistor is out, or the DAC's output stage is faulty. Together with the 19% uncalibrated gain error, a faulty chip looks more likely than before. This is an inference.

### Not measured

- **Whether C2 changes the DC output or the gain.** The scope read the mid-scale average as 2.5407 V, but its DC accuracy at that range is a few tens of mV, so this needs the meter.
- **Low-frequency noise with C2.** At 100 mV/div the scope can't resolve noise below 10 kHz, so its readings there (50–100 µV per decade) show the scope's limit, not the board.
- **The noise floor with C2.** It doesn't depend on C2, so it wasn't repeated.
- **Probe loading.** The 1× probe's capacitance loads VOUT and may affect the oscillation. A check with a 10× probe wasn't done.

## Recommended next steps

All of these are done with the power off.

1. **Refit the 0 Ω resistor, and check how C2 and the resistor are connected.** C2 is now back in, but the resistor is still out. Check the C2 and VOUT wiring against the datasheet's Figure 5: C2 between pin 10 and VOUT. Then repeat tests 1 and 2, re-measure the workaround's gain value (10% and 90%), and repeat the noise captures. If the oscillation remains with the board built as designed, the DAC1220 itself is the main suspect.
2. **Check for leakage at the filter capacitors.** Measure the resistance across C1 (pin 9 to VREF) and C2 (pin 10 to VOUT). Each should read open or many megohms; anything lower is a leakage path. Clean the area around pins 9 and 10 with isopropyl alcohol either way, then re-test.
3. **Check the capacitor values and types.** 20-bit mode needs C1 = 10 nF and C2 = 3.3 nF, film or C0G. Check that they aren't swapped, aren't the 16-bit values (2.2 nF and 0.22 nF), and aren't a lossy dielectric.
4. **Try the chip.** If the capacitors and board check out, swap in a known-good DAC1220, or run the same firmware on another board.

After each change, re-run the diagnostic sequence: `clr` and then `pct 0`, `pct 50`, `pct 100`; then `cal` and the same three readings. The fault is fixed when the calibrated readings come out at about 0 V, 2.5 V and 5.0 V. The uncalibrated span should also come out far closer to 5.0 V than the 4.07 V measured here.

**Workaround until the fault is fixed:** run self-calibration as usual, then overwrite the FCR with a measured value (`9D30D8h` on this board). See [Workaround: hand-set gain value](#workaround-hand-set-gain-value). It hides the fault instead of fixing it.

**Read-back on future boards:** a wire from GP28 to the DAC1220's SDIO pin lets `regs` show the actual OCR and FCR that self-calibration produces, instead of values inferred from voltages.

## Other findings

These came up during the review. None of them causes the symptom.

- [DAC1220-SERIALINPUT.ino](DAC1220-SERIALINPUT.ino) calls a constructor that no longer exists, so it doesn't compile. It also calls `reset()` before every write. `reset()` clears the format setting and the calibration, and nothing restores them. Code that calls `reset()` should follow it with `begin()`.

## State after testing

- **The board is running the controller firmware with the workaround.** It was built from `PEM_CSC` on `debug/dac-scaling`, with FCR `9D30D8h`, against this library's `debug/dac-scaling`. The DAC is held at mid-scale by `:DEB:DAC`.
- **Hardware:** C2 (3300 pF) is fitted. The 0 Ω resistor is still out. A 2 cm test wire is soldered to the VOUT pad.
- **The oscilloscope is still set up for the noise tests.** Its original setup is saved in `~/Arduino/.llm/scratch/pemcsc-dac-scaling/scope-setup-original.txt`.
- **The original firmware wasn't saved.** It couldn't be read off the board before the first test. After the first round of tests, `PEM_CSC` at commit `1f3b62d` was rebuilt from source against this library's `master` (`82239dd`) and flashed; it answered `*IDN?` with `Hinds PEM controller CSC V02`. The diagnostic sketch was then flashed again for the workaround tests.
- **Settings should be intact but weren't checked.** No upload rewrote the 64 KB filesystem area, so saved settings should survive. Check them anyway.
- **All changes are on `debug/dac-scaling` branches.** That includes the library changes here and the gain value in `PEM_CSC.ino`. They haven't been merged into `master` or pushed.
