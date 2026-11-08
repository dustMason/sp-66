# SP-66

A Roland TR-66 Rhythm Arranger recreation for the Teenage Engineering SP-1.
The C engine synthesizes the drum voices at 48 kHz and plays all 27 factory
rhythms. The SP-1 firmware sends the mono mix to the onboard headphone and speaker
codecs. The firmware uses no samples.

## Hear it on your computer

```sh
make test render
work/build/tr66-render work/sp-66-demo.wav bossa-nova rock-6 auto 120 4
```

This renders four bars of Bossa Nova layered with Rock 6, alternating between
the A and B phrases at 120 BPM, with one second for the final hits to ring out.

The [Drumbase report](outputs/tr66-recording-fit.json) covers 23 hardware
loops, including 12 held out of the gain fit. The
[Reverb report](outputs/tr66-reverb-check.json) covers 25 loops from a second
reference pack. The [voice report](outputs/tr66-voice-fit.json) covers
isolated hits. See the comparison commands below to generate level-matched
A/B audio using your local copies of the recordings.

## SP-1 controls

| Control | Action |
| --- | --- |
| Fader 1 | Tempo from 50 to 200 BPM |
| Fader 2 | Balance between bass drum and hi-hat/maracas/cymbal, centered for the normal mix |
| Fader 3 | Off, Waltz, Jazz Waltz, Slow Rock, Bossa Nova, Samba, Mambo, Cha-Cha, Beguine, Rhumba |
| Fader 4 | Six presets within the current variation bank |
| T1, T2, T3 | Select Rock, 2 Beat, or 4 Beat |
| T4 | Toggle the selected variation alongside the base rhythm |
| Back / forward rocker | Move one position along A, Auto, B |
| Play | Start or stop |
| Function | Tap for one fill on the next bar while playing, hold for about two seconds for shutdown |
| Volume buttons | Master output level |
| LEDs | Bank and variation toggle on the top row, phrase position on the side |

Each A or B phrase is two bars. Auto plays A then B and repeats.
The Function fill is an SP-1 addition and plays the last bar of B once.
Rhythm, variation and Arrange changes take effect on the next downbeat.
Slow Rock has six pulses per bar, with the quarter-note tempo on the fader.

## Build and reproduce

Host tests and renders use a C11 compiler. The capture and comparison scripts
use Python with NumPy, SciPy and SoundFile.

```sh
make test render all
python3 tools/fetch_references.py
work/build/tr66-render work/sp-66-demo.wav bossa-nova rock-6 auto 120 4 work/bossa-rock6-auto.csv
python3 tools/compare_groove.py work/sp-66-demo.wav work/bossa-rock6-auto.csv work/tr66-groove-ab.wav
python3 tools/calibrate.py --output work/fitted-patches.inc --report work/fit.json --audition work/isolated-ab.wav
```

The calibration script calls the same C hit renderer used by the engine.
Its proposed coefficient table goes in `work/` for review before it replaces
`src/voice_patches.inc`. The firmware build needs only the coefficients.

The current comparison uses [Crypton Drumbase 5000](https://archive.org/details/drumbase-5k.7z).
Extract the two TR-66 directories from Partition D into
`work/references/complete/drumbase`. The archived 7z has SHA-1
`c80b26e6a5edd5997091ea257c2ae62d298b9467`. Keep the recordings in the host
reference directory. Re-run the comparison with the checked-in coefficients.

```sh
python3 tools/compare_recordings.py --report work/recordings.json --audition work/recordings-ab.wav
```

To repeat the initial fit, run `tools/calibrate.py` with
`--drumbase 'work/references/complete/drumbase/Partition D/15. TR-66 #2'`,
build with that candidate table, then run `tools/compare_recordings.py` with
`--fit-gains work/mix-patches.inc`. The mix fit calls the C engine for each voice,
then fits one set of voice levels across the training loops. It retains the
isolated rim level because the Bossa Nova loop is held out. The final
refinement fits shared voice levels to 11 CD loops and six Reverb loops.
The [recording report](outputs/tr66-recording-fit.json) gives those gains and
the later ring-down measurements used in the current coefficient table.

The second comparison uses the Roland Rhythm Arranger pack from
[Reverb Drum Machines](https://archive.org/details/reverb-drum-machines-complete-collection).
Extract that pack to `work/references/complete/reverb` and run the comparison.

```sh
python3 tools/compare_reverb.py --report work/reverb.json --audition work/reverb-ab.wav
```

The host renderer defaults to centered Balance. Set `TR66_RENDER_BALANCE`
between −1 (bass) and +1 (treble) to compare at a fixed position.

```sh
TR66_RENDER_BALANCE=0.3198 python3 tools/compare_reverb.py --report work/reverb-balanced.json --audition work/reverb-balanced.wav
```

For SP-1, use Zephyr 4.3.1, the SDK 0.17.4 ARM toolchain, and the `cmsis`,
`cmsis_6` and `hal_nordic` modules pinned by the Zephyr manifest. From a
configured west workspace, with the Zephyr Python dependencies and DTC
available, set `tr66_project` to this project directory and run:

```sh
west build -b stem_player -d "$tr66_project/work/sp1-build" "$tr66_project/firmware" -- -DBOARD_ROOT="$tr66_project/firmware"
```

The development image is `work/sp1-build/zephyr/tr66-sp1.bin`, linked at
`0x20000` for the SP-1 application slot. Board and codec sources are credited
in [firmware/NOTICE](firmware/NOTICE). The build exposes startup and runtime
diagnostics over USB serial and in a RAM struct for SWD inspection. See the
hardware status below before flashing.
The community flashing path uses [Solderless](https://solderless.engineering/utility/).
With the SP-1 off, hold Track 1 and Track 4 while connecting USB-C in Chrome
or Edge, then release when the Track 1 light comes on. Connect in the
utility and select the SP-1 `.bin`. Flashing replaces the factory application.
Treat replacing the factory firmware as one-way. See the
[SP-1 installation notes](https://learn.adafruit.com/hacking-the-teenage-engineering-sp-1/install-circuitpython).

The host engine rendered 5,103 passes across all supported selections and
Arrange settings at 50, 120 and 200 BPM, with Balance centered and at both
ends. The loudest sample was 0.731 of digital full scale, with zero clipped samples.

## Hardware status and diagnostics

The first hardware test of commit `8c8792a` completed the bootloader's write
and finalization steps, but left Track 1 lit with no response to Function.
Follow-up hardware diagnostics reached the running audio loop, then reproduced
control scanning stuck inside an ADC read. Audio and USB continued running;
the original Function handling depended on those stalled scans.

Control reads now use the asynchronous ADC API with a 10 ms deadline. If a
conversion stalls, the firmware aborts it, waits up to another 10 ms for the
driver to release the request, discards that scan and continues. It resets
if the abort cannot complete, rather than reusing a buffer still owned by
DMA. Hardware testing has exercised this recovery repeatedly while control
scanning and audio rendering continued. The reason the peripheral sometimes
fails to complete a conversion remains under investigation; this is a
recovery workaround, not a claim that the underlying ADC fault is fixed.

The firmware now enumerates as **SP-66 diagnostics**. Open its serial port at
115200 baud with DTR asserted to receive one status line per second. It
never waits for a computer to connect. Each line reports the startup stage,
status code, control-scan attempts, last ADC result, rendered audio blocks,
peak render cycles, over-budget blocks, I2S write errors, Function state,
scan phase, six raw ADC values, and cumulative ADC timeouts. `saadc` holds
ENABLE, END, STARTED and RESULT.AMOUNT captured at the most recent timeout.
Observed stalls had `saadc=1,0,0,0`. Sending lowercase `r` on the diagnostic
console restarts the device. A fatal error records
its reason and program counter in uninitialized RAM before resetting;
retention of that record across the bootloader is not yet verified.

| Stage | Last startup milestone |
| --- | --- |
| 1 | Initializing the synthesis engine |
| 2 | Initializing controls and LEDs |
| 3 | Attempting codec power-on and I2S startup |
| 4 | I2S startup attempt finished; codec configuration may be pending |
| 5 | Codecs ready; control scanning scheduled |
| 6 | Stopping background work |
| 9 | Shutdown idle loop |

Scan phases 10–15 identify the ADC input being read (track buttons, side
buttons, then faders 1–4). Phase 20 is input decoding, 30–37 an LED update,
40 LEDs updated, 50 jack detection, and 60–61 scheduling the next scan.

A separate thread reads Function directly from its GPIO, independently of
ADC scans and audio startup. After the button has been released once since
boot, a roughly two-second hold requests shutdown; continuing to four
seconds resets the device if shutdown has stalled. Fatal errors reset
instead of halting. The build also enables stack protection, assertions,
and a four-second watchdog where the bootloader has not already started one.
ADC oversampling is applied after devicetree sequence initialization so it
is no longer overwritten.

The independent Function reset and watchdog have been exercised on hardware.
To re-enter the bootloader, shut down with Function, disconnect USB, then
hold Track 1 and Track 4 while reconnecting USB. A reset with USB continuously
connected may restart the application instead. Devices still running the
original frozen image must first regain the bootloader through a power cycle.

## Fidelity work

The [Roland player's manual](https://cdn.roland.com/assets/media/pdf/TR-66_OM.pdf)
contains all 27 scores on PDF pages 6–8. Figures 10 and 13–18 of the
[service notes](https://archive.org/details/roland-tr-66-service-notes)
show the voice circuit and timing. Hi-hat and maracas share one voice.
Bossa Nova and Rock 2/3 use a reduced-amplitude bass drum on some offbeats.
Both recording sets confirm the quiet high bongo in Beguine, plus reduced
conga and high-bongo triggers and a softer B-fill kick in Rhumba.
Both packs also have a softer kick on beat 3 of the second Slow Rock B bar.
It measures 41% of a nearby full kick on the CD and 49% in Reverb. Some CD
kick attacks clip. The service charts also helped correct Slow Rock's
timing, several Rock B fills, Habanera, Swing 1, Parade and the Mambo B fill.

The CD includes one-shots and 23 captures with sampler loop points.
Its one-shots supplied most of the voice fits. The ring-down pass compares
isolated or separated hits from both packs. Across seven Beguine hits in
each recording, the low bongo measures about 394 Hz with a 15 ms decay
time constant on the CD and 405 Hz with 47 ms in Reverb. The model uses
400 Hz and 28 ms. Its low-bongo trigger is 3.2 dB softer than the earlier
fit, closer to the attack level in both recordings. The snare has a weaker
low-bongo resonance, as described in Roland's service notes, and its attack
has a short pulse followed by a slower, opposite-polarity recovery. The
high bongo uses a fast and a slow decay at 597 Hz, based on separated Mambo
hits from both packs. Cowbell hits from Mambo and Cha-Cha measure about
893 Hz in Reverb and 897 Hz on the CD. The model uses 895 Hz.
The snare attack and tone changes improved several groove scores. In that
pass, isolated 35–540 Hz waveform correlation fell from 0.95 to 0.94 on
Drumbase and from 0.92 to 0.86 on Reverb.

Both recording sets have a brief low-frequency pulse under cymbal and
hi-hat hits. The C engine now adds two decays of opposite sign to each
voice. On cymbal-only steps in Reverb Slow Rock B, the 130–220 Hz band
measures −40.4 dB relative to 6–10.5 kHz. The C render moves from −70.3 to
−40.6 dB at the same Balance setting.

The snare's noise envelope closes as the decay crosses a threshold.
The hi-hat and maracas now use the same gate across all rhythms. Its
threshold is lower following a cymbal hit and recovers with a fitted
554 ms time constant. In the recordings, the hat falls 18 dB below its
attack at about 46 ms without a preceding cymbal and 97 ms when the last
cymbal is under 300 ms old. Across 90 hats after a cymbal, median timing
error falls from 18.2 to 5.5 ms. The fit estimates the effect of shared
trigger circuitry from the audio.

Both packs shift cymbal energy upward as the hit decays. Across 113 cymbal
hits, the mean spectrum peaks near 7.8–7.9 kHz at 5–25 ms and 8.1–8.3 kHz
at 45–95 ms. The current C engine holds the fast part of the envelope for
22 ms, then moves the first noise filter from 7.84 to 8.17 kHz as the fast
part decays. At 70–100 ms the model has 2.7 dB more cymbal energy than the
Reverb recording and 0.6 dB less than Drumbase, relative to the early
part of the hit.

Figure 10 of the service notes shows the hi-hat and cymbal trigger circuits
feeding one resonator. In six Bossa Nova, Samba, and Beguine captures, the
median combined attack was 0.32 dB quieter than hi-hat alone in the
6.2–11.8 kHz band. The previous C mix made it 3.91 dB louder. The current
C engine plays one cymbal hit with a 44 ms hold when both inputs fire on
the same tick. With the current hi-hat envelope, the combined attack
is 1.36 dB quieter than hi-hat alone.

Reverb's Bossa Nova recording confirms Roland's rim timing. The Drumbase
recording places some rim hits at different steps. Both recording sets
play the high conga in Samba where the service chart calls for a low bongo.
The firmware follows the recorded Samba and the documented Bossa Nova.
Both Habanera A captures support the score's simultaneous kick and snare.

| Level-matched spectral error | Before ring-down pass | After ring-down pass | Current |
| --- | ---: | ---: | ---: |
| Drumbase, all 23 loops with clipped windows excluded | 3.54 dB | 3.57 dB | 2.75 dB |
| Drumbase, 12 loops omitted from the gain fit | 3.53 dB | 3.49 dB | 2.78 dB |
| Reverb, all 25 loops | 5.70 dB | 4.62 dB | 4.26 dB |
| Reverb Slow Rock B | 5.70 dB | 5.17 dB | 4.16 dB |
| Reverb Mambo A | 5.82 dB | 4.41 dB | 4.09 dB |
| Reverb Mambo B | 6.37 dB | 4.66 dB | 4.40 dB |
| Reverb Rhumba A | 7.29 dB | 5.71 dB | 5.80 dB |
| Reverb Rhumba B | 7.36 dB | 5.31 dB | 5.12 dB |

The original Drumbase held-out score was 6.69 dB with clipped windows
excluded. Its Slow Rock score rose from 3.19 to 3.68 dB after the quieter
fill kick and other voice changes, then fell to 3.44 dB after the cymbal
pulse and noise envelope refinements. The latest cymbal fit brings it to
2.72 dB. The two packs cover 24 of the 27 factory rhythms between them.
Bass & Snare in each beat bank and Foxtrot 2
use the published charts and await a matching recording. The
recordings also cover a subset of the available A, Auto and B phrases.

In eight matched first-bar excerpts, the two recordings differ by a median
4.23 dB. The current C model is 2.87 dB from the CD and 3.36 dB from Reverb
on those excerpts. In the matched Slow Rock excerpt, the recordings differ
by 3.12 dB, and the model is 4.11 dB from the CD and 2.94 dB from Reverb.

A separate comparison fitted one fixed Balance position per pack using
only the gain-fit loops. The best positions were −0.19 for Drumbase and
+0.32 for Reverb, on a scale from −1 for bass to +1 for treble. Holding
those positions fixed, the noise envelope refinement reduced median errors
on loops outside the original gain fit from 3.31 to 3.03 dB for Drumbase
and from 3.76 to 3.44 dB for Reverb. The cymbal fit moves these scores to
2.90 and 3.42 dB, respectively. Slow Rock moves from 2.84 to 2.01 dB in
Drumbase, and from 3.04 to 2.53 dB for A and 3.20 to 2.59 dB for B in Reverb.
In the shared-gate comparison, Reverb Bossa Nova A moves from 3.30 to
3.16 dB at +0.32 Balance and from 5.39 to 5.77 dB at center.
The hi-hat and bongo pass brought the held-out scores to 2.76 dB
for Drumbase and 3.33 dB for Reverb at the same Balance positions.
At +0.32, Rhumba A drops from 4.52 to 4.32 dB, Rhumba B from 4.58
to 4.17 dB, and Beguine B from 4.92 to 4.73 dB. At center, Rhumba A
rises from 5.67 to 5.72 dB.
The original knob positions are unknown, and the fitted settings may also
compensate for differences in the recording chains.

The main tables start the C engine from silence. A separate check compares
the archived hardware loops after both C builds have played a complete
Arrange cycle. At the fixed Balance positions, the Reverb median falls
from 3.25 to 3.12 dB. Reverb Bossa Nova A moves from 2.71 to 2.42 dB,
Samba A from 3.26 to 2.94 dB, and Beguine B from 4.69 to 4.45 dB.
Drumbase Samba falls from 2.99 to 2.84 dB after a cycle and rises from
2.98 to 4.05 dB from silence. The archived loop starts partway through
the recovery from a cymbal. Two-cycle results are in both reports.

Trigger dynamics on dense percussion chords, 3/4 plus 4/4 layering, and the
SP-1's physical output still need a direct hardware comparison. The current
layering follows the base rhythm's meter and merges coincident hits.
