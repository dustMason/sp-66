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
the A and B phrases at 120 BPM, with one second for the final hits to ring out.[^reports]

## SP-1 controls

![SP-1 front and edge controls annotated with their SP-66 functions](docs/sp1-controls.svg)

| Control | Action |
| --- | --- |
| Fader 1 | Tempo from 50 to 200 BPM |
| Fader 2 | Balance between bass drum and hi-hat/maracas/cymbal, with the TR-66's shunt response approximated and unity gain at center |
| Fader 3 | Off, Waltz, Jazz Waltz, Slow Rock, Bossa Nova, Samba, Mambo, Cha-Cha, Beguine, Rhumba |
| Fader 4 | Six presets within the current variation bank |
| T1, T2, T3 | Select Rock, 2 Beat, or 4 Beat |
| T4 | Toggle the selected variation alongside the base rhythm |
| Arrange rocker | Press down toward A or up toward B, one position at a time through A, Auto, and B |
| Play | Start or stop |
| Function | Tap for one fill on the next bar while playing, hold for about two seconds for shutdown |
| Volume buttons | Master output level |
| Track LEDs | Selected variation bank and whether the variation is on |
| Side LEDs | Arrange setting and current bar |

The original score uses two bars per A or B phrase. Auto plays A then B.
Some rhythms add a pickup on the first downbeat of the full Arrange cycle.
Jazz Waltz adds a snare every two bars in B and every four bars in Auto.
The Function fill is an SP-1 addition and plays the last bar of B once.
Rhythm, variation and Arrange changes take effect on the next downbeat.
Slow Rock is in 6/8. The tempo fader still sets the quarter-note rate.
Waltz, Jazz Waltz and Slow Rock put the shared pattern counter in triple
time. Any active variation follows that counter, as in Roland's Slow Rock
plus Rock 6 setting.

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
ends. The loudest sample was 0.528 of digital full scale, with zero clipped samples.

## USB MIDI clock

Connect the SP-1 over USB-C to a DAW or a USB MIDI host. Enable MIDI Clock
and transport send to the **SP-66** MIDI port, then start the host. SP-66
follows the host at 24 pulses per quarter note. MIDI Start plays the first
step on the next clock, Stop holds the current place, and Continue resumes
on the next clock. A Song Position Pointer sent while stopped seeks to that
sixteenth-note position in the selected rhythm on Continue.

For a host that sends only clock, press Play while clocks are arriving to
start at the next pulse. Play can also stop the rhythm locally. The tempo
fader sets the tempo for standalone play. If the clock is silent for half a
second, the rhythm stops and Play can start standalone again.

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

In a roughly three-minute SP-1 test on 2026-11-07, the recovery build completed
14,327 control scans and rendered 129,030 audio blocks. Eleven ADC stalls
recovered without a reboot; no render-budget overruns or I2S write errors
were recorded. Sound, buttons, faders and Function shutdown were checked on
the device. Shutdown reached stage 9 and extinguished the LEDs with USB
connected. This is a short functional test, not an extended stability run.

The development firmware presents MIDI and a diagnostic serial interface
under **SP-66**. Open its serial port at 115200 baud with DTR asserted for
one status line per second. The device starts independently of the host
connection. Each line reports startup and failure stages, status code,
control-scan attempts, last ADC result, audio blocks queued for I2S, peak
render cycles, over-budget blocks, I2S write errors, Function state, and
the nRF reset flags observed on app entry. The `usb` value reports the
composite device initialization result. The line also shows the scan
phase, six raw ADC values, and cumulative ADC timeouts. The `saadc` field
holds ENABLE, END, STARTED and RESULT.AMOUNT from the most recent timeout.
Observed stalls had `saadc=1,0,0,0`. Sending lowercase `r` on the diagnostic
console restarts the device. A fatal error records its reason and program
counter in uninitialized RAM before resetting. Retention of that record
across the bootloader still needs verification.

| Stage | Last startup milestone |
| --- | --- |
| 1 | Initializing the synthesis engine |
| 2 | Initializing controls and LEDs |
| 3 | Attempting codec power-on and I2S startup |
| 4 | I2S primed and waiting for codec configuration |
| 5 | Codecs ready; control scanning scheduled |
| 6 | Stopping background work |
| 8 | Startup or audio error; awaiting Function shutdown |
| 9 | Shutdown idle loop |

Scan phases 10–15 identify the ADC input being read (track buttons, side
buttons, then faders 1–4). Phase 20 is input decoding, 30–37 an LED update,
40 LEDs updated, 50 jack detection, and 60–61 scheduling the next scan.

The side LED nearest Function lights during app startup, so it is
distinguishable from the bootloader's solid Track 1 LED. The audio loop
stops waiting after 2.5 seconds for codec setup or 500 ms without a
successful I2S write after setup. Either timeout reports `-110`. On
failure the side row flashes, the codecs are powered down, charging
resumes, and the serial console and Function shutdown remain available.
The shutdown sequence disables the I2S peripheral before cutting its
external clocks, following [Wakes](https://github.com/Worldwave/Wakes).

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

## Notes from Codex on Fidelity

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
Its one-shots supplied most of the voice fits. On separated Beguine hits,
the low bongo's 30–95 ms ring measures about 394 Hz and a 19 ms decay
constant on the CD, and 405 Hz and 51 ms in Reverb. Roland provides a
separate decay trim for this voice in section 10-3-2-3 of the service notes.
The C model combines 7.5 ms and 32 ms decays at 400 Hz. Its first 3 ms
measure 1.61 times the 6–16 ms envelope, up from 1.32 and between the
two recordings' 1.49 and 1.76. The 32 ms mode gives a tail between the
two captures. After one Arrange cycle at the fitted Balance, all eight
loops with low bongo improve by 0.01–0.09 dB, including Reverb Rhumba A
from 4.18 to 4.09 dB and Drumbase Beguine from 2.82 to 2.77 dB.

The snare has a weaker low-bongo resonance, as described in Roland's
service notes, and its attack has a short pulse followed by a slower,
opposite-polarity recovery. Cowbell hits from Mambo and Cha-Cha measure about
893 Hz in Reverb and 897 Hz on the CD. The model uses 895 Hz.
The snare attack and tone changes improved several groove scores. In that
pass, isolated 35–540 Hz waveform correlation fell from 0.95 to 0.94 on
Drumbase and from 0.92 to 0.86 on Reverb.

The high bongo uses Roland's 600 Hz service setting and two decays at that
frequency. The 32 ms mode carries more of the body of the hit. On three
isolated hits from the [Vintage Synth Explorer demo](https://www.vintagesynth.com/roland/tr-66),
the envelope error fell from 0.39 to 0.11 dB. With one Arrange cycle of
warm-up at the fitted Balance, Reverb Mambo B fell from 3.42 to 3.37 dB.
Drumbase Samba rose from 2.53 to 2.56 dB. The two loop packs still differ
in how long the bongos ring.

The high conga adds a short 200 Hz decay alongside its longer ring.
This strengthens the initial cycles and brings the ratio of the first
trough to the first crest from 0.577 to 0.674. In 14 separated loop hits,
the corresponding median ratios are 0.680 in Drumbase and 0.662 in
Reverb. The 58 ms tail remains a compromise between the recordings.
After one Arrange cycle at the fitted Balance, Drumbase Cha-Cha improves
from 2.78 to 2.63 dB and Reverb Beguine B from 4.27 to 4.12 dB. At center
Balance, Drumbase Beguine rises from 3.03 to 3.11 dB.

Both recording sets have a brief low-frequency pulse under cymbal and
hi-hat hits. The C engine now adds two decays of opposite sign to each
voice. On cymbal-only steps in Reverb Slow Rock B, the 130–220 Hz band
measures −40.4 dB relative to 6–10.5 kHz. The C render moves from −70.3 to
−40.6 dB at the same Balance setting.

The snare uses a 1.7 ms noise rise time constant and a 29 ms hold, then
closes as the decay crosses a threshold. In 36 snare-only hits across
both loop packs, the 3–6 ms noise measures about +0.6 dB relative to the
6–40 ms body. The C model moves from about −8.3 dB to −1.2 dB on those hits.
The model also uses less upper treble. Reverb's snare has about 8 dB more
noise relative to its tonal ring than Drumbase's, so the firmware uses a
shared setting between those recordings. The
[Drum Abuse one-shots](https://github.com/smpldsnds/drum-abuse-vol1/tree/58a9b6246d232d862fad15e06c97a4a030655017/samples/roland-tr-66)
show a similarly quick rise. Two snare noise waveforms match hits in
[Vintage Synth Explorer's demo](https://www.vintagesynth.com/roland/tr-66)
at 0.92 and 0.97 normalized correlation, so those matches count as one source.

The rim shot now uses a short deterministic pulse to drive a 1.525 kHz
resonator with a 2.10 ms decay. Figure 10 shows a passive resonator and
trigger pulse. The two loop packs give rim decays of about 2.09–2.17 ms.
Claves decay in about 4.7–4.8 ms and the model uses 4.75 ms at Roland's
nominal 2.35 kHz. Reverb's claves ring around 2.50 kHz. In isolated
Drumbase hits, rim waveform correlation rises from 0.975 to 0.988 and
claves from 0.979 to 0.983. The full-pack settled medians remain 2.27 dB
for Drumbase and 3.09 dB for Reverb with these two voice changes.

The hi-hat and maracas now use the same gate across all rhythms. Its
threshold is lower following a cymbal hit and recovers with a fitted
554 ms time constant. In the recordings, the hat falls 18 dB below its
attack at about 46 ms without a preceding cymbal and 97 ms when the last
cymbal is under 300 ms old. Across 90 hats after a cymbal, median timing
error falls from 18.2 to 5.5 ms. The fit estimates the effect of shared
trigger circuitry from the audio.

The hi-hat and maracas noise filter now has a broader resonance at
8.06 kHz. Across 130 usable hits, the RMS error of the relative spectrum
over 12 bands falls from 1.82 to 1.42 dB in Drumbase and from 4.13 to
2.33 dB in Reverb. After one Arrange cycle at the fitted Balance,
Beguine B in Reverb improves from 4.42 to 4.27 dB, and Mambo A from 3.35
to 3.17 dB. The settled Reverb median falls from 3.09 to 3.04 dB.
The settled Drumbase median is 2.27 dB, and its center-Balance median
rises from 2.80 to 2.85 dB.

The hi-hat and maracas attack now rises with a 3.5 ms time constant, down
from 5.3 ms. A higher gate threshold and longer decay keep the calculated
gate closing time near 49 ms when no cymbal precedes the hit. Across 234 usable
steps, the median absolute error of 3–8 ms noise energy relative to the
8–30 ms body falls from 2.31 to 1.46 dB in Drumbase and from 3.87 to
1.94 dB in Reverb. The 30–43 ms envelope error rises from 1.66 to 2.02 dB
in Drumbase and from 1.89 to 2.13 dB in Reverb. At the fitted Balance
after one Arrange cycle, the Drumbase loop median is 2.27 dB and the
Reverb median improves from 3.04 to 2.99 dB. At center Balance, the
Drumbase median rises from 2.82 to 2.84 dB.

Both packs shift cymbal energy upward as the hit decays. Across 113 cymbal
hits, the mean spectrum peaks near 7.8–7.9 kHz at 5–25 ms and 8.1–8.3 kHz
at 45–95 ms. On cymbal-only triggers, the C engine holds the fast part of
the envelope for 22 ms, then moves the first noise filter from 7.84 to
8.17 kHz as the fast part decays. At 70–100 ms the model has 2.7 dB more
cymbal energy than the Reverb recording and 0.6 dB less than Drumbase,
relative to the early part of the hit.

Figure 10 of the service notes shows the hi-hat and cymbal trigger circuits
feeding one resonator. In six Bossa Nova, Samba, and Beguine captures, the
median combined attack was 0.32 dB quieter than hi-hat alone in the
6.2–11.8 kHz band. When both inputs fire on the same tick, the C engine
uses one cymbal hit with a 44 ms fast hold. The latest fit starts nearer
the resting resonance and puts more drive into the slow decay. The
median combined attack moves from −1.36 to −0.90 dB relative to a single
hat attack. At 40–65 ms the model moves from −4.12 to −3.39 dB, compared
with −2.14 dB in the recordings.

Reverb's Bossa Nova recording confirms Roland's rim timing. The Drumbase
recording places some rim hits at different steps. Both recording sets
play the high conga in Samba where the service chart calls for a low bongo.
The firmware follows the recorded Samba and the documented Bossa Nova.
Both Habanera A captures support the score's simultaneous kick and snare.

| Level-matched spectral error | Before ring-down pass | After ring-down pass | Current |
| --- | ---: | ---: | ---: |
| Drumbase, all 23 loops with clipped windows excluded | 3.54 dB | 3.57 dB | 2.82 dB |
| Drumbase, 12 loops omitted from the gain fit | 3.53 dB | 3.49 dB | 2.87 dB |
| Reverb, all 25 loops | 5.70 dB | 4.62 dB | 4.16 dB |
| Reverb Slow Rock B | 5.70 dB | 5.17 dB | 4.16 dB |
| Reverb Mambo A | 5.82 dB | 4.41 dB | 3.72 dB |
| Reverb Mambo B | 6.37 dB | 4.66 dB | 4.08 dB |
| Reverb Rhumba A | 7.29 dB | 5.71 dB | 5.53 dB |
| Reverb Rhumba B | 7.36 dB | 5.31 dB | 4.92 dB |

The original Drumbase held-out score was 6.69 dB with clipped windows
excluded. Its Slow Rock score rose from 3.19 to 3.68 dB after the quieter
fill kick and other voice changes, then fell to 3.44 dB after the cymbal
pulse and noise envelope refinements. The latest cymbal fit brings it to
2.71 dB with the current snare coefficients. The two packs cover 24 of
the 27 factory rhythms between them and a subset of their A, Auto and B
phrases. Bass & Snare in each beat bank and Foxtrot 2 still await a matching
recording.

All 36 A, B and Auto traces across the 2 Beat and 4 Beat banks match a
transcription of the service pulse charts after correcting their cycle
pickups and two B fills. The Foxtrot pickups follow the service charts
where they differ from the printed player scores. The Drumbase Jazz Waltz
Auto recording confirms a snare pickup on the first downbeat of the four-bar
cycle.

In Waltz Auto, the last bar adds three quieter kicks at ticks 24, 42 and 48
on the 24-tick beat grid. The Drumbase recording's kick ring measures about
35%, 37% and 19% of the downbeat at those hits. The revised C score uses the
existing 43% kick trigger for the first two and a 19% trigger for the third.
Of the two quieter renders tested, those levels give the closest loop match.
At the fitted Balance, the clean-window error drops from 4.69 to 2.78 dB
from silence and from 4.73 to 2.82 dB after one Arrange cycle. Reverb's
Waltz clip contains the A phrase, so the fill fit has one recorded reference.

In eight matched first-bar excerpts, the two recordings differ by a median
4.23 dB. In that comparison, the C model was 2.83 dB from the CD and
3.33 dB from Reverb. In the matched Slow Rock excerpt, the recordings
differ by 3.12 dB, and the model scored 4.11 dB from the CD and 2.94 dB
from Reverb.

An earlier comparison fitted one fixed position per pack using the
linear Balance model and only the gain-fit loops. The best positions were
−0.19 for Drumbase and +0.32 for Reverb, on a scale from −1 for bass to +1
for treble. Holding those positions fixed, the noise envelope refinement
reduced median errors on loops outside the original gain fit from 3.31 to 3.03 dB for Drumbase
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

The current Balance model follows the grounded wiper of the 50 kΩ
potentiometer in Figures 1 and 10 of the service notes. It estimates each
shunt's gain around 65 Hz for bass drum and 8.17 kHz for hi-hat, maracas,
and cymbal. At either end, the retained path gains about 3.1–3.3 dB
relative to center. All 48 center-Balance renders are sample-identical
to the previous build. Refitting each law to the recordings gives
settled medians of 2.28 → 2.29 dB for Drumbase and 3.00 → 3.02 dB for
Reverb. The fitted positions for the current model are −0.2882 and
+0.4551. A recorded knob sweep would verify the gain estimates and show
any frequency-response changes through the control's travel.

The main tables start the C engine from silence. The cymbal history check
compares the archived hardware loops after both C builds have played a
complete Arrange cycle. At the fixed Balance positions, the Reverb median falls
from 3.25 to 3.12 dB. Reverb Bossa Nova A moves from 2.71 to 2.42 dB,
Samba A from 3.26 to 2.94 dB, and Beguine B from 4.69 to 4.45 dB.
Drumbase Samba falls from 2.99 to 2.84 dB after a cycle and rises from
2.98 to 4.05 dB from silence. The archived loop starts partway through
the recovery from a cymbal. Two-cycle results are in both reports.

The coincident-trigger fit lowers the settled Drumbase Bossa Nova
error from 3.67 to 3.49 dB, Drumbase Samba B from 2.84 to 2.61 dB, and
Reverb Samba A from 2.94 to 2.82 dB. Drumbase Tango B rises from 3.24 to
3.41 dB and Reverb Bossa Nova A rises from 2.42 to 2.46 dB. The full-pack
medians remain 2.37 dB for Drumbase and 3.12 dB for Reverb after one
Arrange cycle.

With the revised snare noise gate, those medians fall to 2.30 dB and 3.09 dB.
Reverb Habanera A and Waltz A improve by 0.19 dB and 0.16 dB respectively.

The snare pulse fit adjusts the two attack decays and the 292.5 Hz and
401 Hz ringing modes against isolated hits in both packs. At the current
Balance settings, the settled loop medians move from 2.29 to 2.27 dB for
Drumbase and 3.02 to 2.85 dB for Reverb. Reverb Habanera A moves from 3.50
to 3.13 dB. On six combined kick and snare hits that passed the clipping
screen, median normalized waveform agreement improves from 0.889 to 0.917.
The originals still cancel more energy between 110 and 240 Hz on these
hits. The isolated Drumbase snare waveform agreement falls slightly,
and the held-out Drumbase loop median rises by 0.02 dB.

The bass drum combines a 65.15 Hz ring with a fast edge and two opposing
decays. The fit uses clean kick hits with cymbal or hi-hat accompaniment.
On 15 kick and snare hits excluded from that fit, median normalized
waveform agreement over 37–700 Hz improves from 0.943 to 0.947. The
Reverb settled loop median falls from 2.85 to 2.72 dB, including Rock 5
from 2.75 to 2.57 dB. The Drumbase median rises from 2.27 to 2.32 dB
while Rock 5 improves from 2.21 to 2.06 dB. The captures still cancel
more energy around 195–240 Hz when both drums trigger together.

On bare kick and snare hits in Rock 5, the recorded main rings are
81–87% as strong as comparable hits with cymbal in the same groove.
The engine uses 85% drive for both tonal responses on that pair and
full drive for the snare noise gate. In held-out Reverb Habanera A, the
snare ring is 83% as strong on the pair as on an isolated snare. The C
render moves from 100% to 85%. At fitted Balance, Drumbase Rock 5 goes
from 2.06 to 1.95 dB and Reverb Rock 5 goes from 2.57 to 2.52 dB.
At center Balance, Drumbase Rock 5 rises from 2.26 to 2.36 dB.

Figures 2 and 13 of the service notes show a shared counter and the 3/4
shunt. It plays counts 0–5 and 8–13 of each 16-count bar in triple time.
The C sequencer now reads variations from those positions and merges
coincident hits. A test checks the complete Slow Rock plus Rock 6 Auto
cycle against the timing charts. Direct hardware comparisons are still
needed for mixed-meter audio, trigger dynamics on dense percussion chords,
and the SP-1's physical output.

[^reports]: The [Drumbase report](outputs/tr66-recording-fit.json) covers 23 hardware
    loops, including 12 held out of the gain fit. The
    [Reverb report](outputs/tr66-reverb-check.json) covers 25 loops from a second
    reference pack. The [voice report](outputs/tr66-voice-fit.json) covers
    isolated hits. See the comparison commands above to generate level-matched
    A/B audio using your local copies of the recordings.
