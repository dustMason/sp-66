#include "controls.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { RATE = 48000, BLOCK = 64 };
#define MASK(voice) (UINT16_C(1) << TR66_ ## voice)

typedef struct {
    uint64_t frame[256];
    Tr66TriggerMask mask[256];
    unsigned count;
} Trace;

static void record(void *context, uint64_t frame, Tr66TriggerMask mask)
{
    Trace *trace = context;
    assert(trace->count < sizeof(trace->frame) / sizeof(trace->frame[0]));
    trace->frame[trace->count] = frame;
    trace->mask[trace->count++] = mask;
}

static void test_all_voices(void)
{
    for (unsigned voice = 0; voice < TR66_VOICE_COUNT; ++voice) {
        Tr66 machine;
        tr66_init(&machine, RATE, 179);
        tr66_hit(&machine, (uint16_t)(UINT16_C(1) << voice));
        float sample[BLOCK], peak = 0;
        for (unsigned frame = 0; frame < 2 * RATE; frame += BLOCK) {
            tr66_render(&machine, sample, BLOCK);
            for (unsigned i = 0; i < BLOCK; ++i) {
                assert(isfinite(sample[i]));
                peak = fmaxf(peak, fabsf(sample[i]));
            }
        }
        assert(peak > 0.02f && peak < 2.0f);
        tr66_render(&machine, sample, BLOCK);
        for (unsigned i = 0; i < BLOCK; ++i)
            assert(sample[i] == 0.0f);
    }
}

static void test_snare_noise_envelope(void)
{
    Tr66Patch noise = tr66_factory_voices[TR66_SNARE];
    memset(noise.tone, 0, sizeof(noise.tone));
    float samples[RATE / 8];
    const unsigned frames = sizeof(samples) / sizeof(samples[0]);
    const unsigned late = RATE / 10;
    tr66_render_hit(&noise, RATE, 179, samples, frames);
    /* The noise has reached most of its body level within 6 ms. */
    const unsigned per_ms = RATE / 1000;
    float attack_power = 0.0f, body_power = 0.0f;
    for (unsigned i = 3 * per_ms; i < 6 * per_ms; ++i)
        attack_power += samples[i] * samples[i];
    for (unsigned i = 12 * per_ms; i < 23 * per_ms; ++i)
        body_power += samples[i] * samples[i];
    assert(attack_power / 3 > 0.5f * body_power / 11);
    float peak = 0.0f;
    for (unsigned i = 0; i < late; ++i)
        peak = fmaxf(peak, fabsf(samples[i]));
    assert(peak > 0.01f);
    for (unsigned i = late; i < frames; ++i)
        assert(samples[i] == 0.0f);

    noise.noise_threshold = 0.0f;
    tr66_render_hit(&noise, RATE, 179, samples, frames);
    peak = 0.0f;
    for (unsigned i = late; i < frames; ++i)
        peak = fmaxf(peak, fabsf(samples[i]));
    assert(peak > 0.01f);
}

static void test_shared_and_reduced_triggers(void)
{
    Tr66 first, second;
    float a[BLOCK], b[BLOCK];
    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(KICK) | TR66_REDUCED_KICK);
    tr66_hit(&second, MASK(KICK));
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(KICK));
    tr66_hit(&second, TR66_REDUCED_KICK);
    Tr66Resonator full_ring = first.voices[TR66_KICK].resonator[0];
    Tr66Resonator reduced_ring = second.voices[TR66_KICK].resonator[0];
    const float full_amplitude = hypotf(full_ring.real, full_ring.imag);
    const float reduced_amplitude = hypotf(reduced_ring.real, reduced_ring.imag);
    assert(fabsf(reduced_amplitude / full_amplitude - 0.5f) < 1e-4f);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(fabsf(b[BLOCK - 1] - 0.5f * a[BLOCK - 1]) > 0.001f);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(KICK) | TR66_GHOST_KICK);
    tr66_hit(&second, TR66_GHOST_KICK);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(fabsf(b[i] - 0.21f * a[i]) < 1e-6f);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, TR66_QUIET_KICK | TR66_GHOST_KICK);
    tr66_hit(&second, TR66_QUIET_KICK);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);

    tr66_init(&first, RATE, 31);
    second = first;
    const Tr66TriggerMask light = TR66_LIGHT_HIGH_BONGO | TR66_QUIET_HIGH_BONGO;
    const Tr66TriggerMask reduced = TR66_REDUCED_HIGH_BONGO | light;
    const Tr66TriggerMask accented = TR66_ACCENT_HIGH_BONGO | reduced;
    tr66_hit(&first, MASK(HIGH_BONGO) | accented);
    tr66_hit(&second, MASK(HIGH_BONGO));
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);

    const struct {
        Tr66TriggerMask mask;
        float drive[2];
    } bongo_routes[] = {
        {light, {0.45f, 0.62f}}, {reduced, {0.53f, 0.53f}}, {accented, {0.66f, 0.25f}},
    };
    tr66_init(&first, RATE, 31);
    tr66_hit(&first, MASK(HIGH_BONGO));
    for (unsigned i = 0; i < sizeof(bongo_routes) / sizeof(bongo_routes[0]); ++i) {
        tr66_init(&second, RATE, 31);
        tr66_hit(&second, bongo_routes[i].mask);
        for (unsigned mode = 0; mode < 2; ++mode) {
            const Tr66Resonator full = first.voices[TR66_HIGH_BONGO].resonator[mode];
            const Tr66Resonator hit = second.voices[TR66_HIGH_BONGO].resonator[mode];
            const float drive = bongo_routes[i].drive[mode];
            assert(fabsf(hit.real - drive * full.real) < 1e-6f);
            assert(fabsf(hit.imag - drive * full.imag) < 1e-6f);
        }
    }

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(HAT) | MASK(MARACAS));
    tr66_hit(&second, MASK(HAT));
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(CYMBAL) | MASK(HAT));
    tr66_hit(&second, MASK(CYMBAL));
    assert(first.voices[TR66_HAT].frames_left == 0);
    float combined_power = 0.0f, cymbal_power = 0.0f;
    for (unsigned frame = 0; frame < RATE; frame += BLOCK) {
        tr66_render(&first, a, BLOCK);
        tr66_render(&second, b, BLOCK);
        if (frame >= RATE * 3 / 100 && frame < RATE * 4 / 100)
            for (unsigned i = 0; i < BLOCK; ++i) {
                combined_power += a[i] * a[i];
                cymbal_power += b[i] * b[i];
            }
    }
    assert(combined_power > 1.25f * cymbal_power);
    tr66_hit(&first, MASK(CYMBAL));
    tr66_hit(&second, MASK(CYMBAL));
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);
}

static void test_quiet_high_bongo(void)
{
    Tr66 full, quiet;
    tr66_init(&full, RATE, 31);
    quiet = full;
    tr66_hit(&full, MASK(HIGH_BONGO));
    tr66_hit(&quiet, TR66_QUIET_HIGH_BONGO);

    /* Beguine's quiet route lowers the attack relative to the ring. */
    const unsigned ms = RATE / 1000;
    float full_attack = 0.0f, quiet_attack = 0.0f;
    float full_ring = 0.0f, quiet_ring = 0.0f;
    for (unsigned frame = 0; frame < 60 * ms; ++frame) {
        float a, b;
        tr66_render(&full, &a, 1);
        tr66_render(&quiet, &b, 1);
        if (frame >= 4 * ms && frame < 14 * ms) {
            full_attack += a * a;
            quiet_attack += b * b;
        }
        if (frame >= 32 * ms) {
            full_ring += a * a;
            quiet_ring += b * b;
        }
    }
    const float attack_ratio = sqrtf(quiet_attack / full_attack);
    const float ring_ratio = sqrtf(quiet_ring / full_ring);
    assert(attack_ratio > 0.16f && attack_ratio < 0.18f);
    assert(ring_ratio > 0.195f && ring_ratio < 0.21f);
}

static void test_kick_snare_pair(void)
{
    Tr66 kick, snare, noise, pair;
    tr66_init(&kick, RATE, 31);
    pair = noise = snare = kick;
    noise.voices[TR66_SNARE].tone_count = 0;
    noise.voices[TR66_LOW_BONGO].tone_count = 0;
    tr66_hit(&kick, MASK(KICK));
    tr66_hit(&snare, MASK(SNARE));
    tr66_hit(&noise, MASK(SNARE));
    tr66_hit(&pair, MASK(KICK) | MASK(SNARE));
    float k[BLOCK], s[BLOCK], n[BLOCK], p[BLOCK];
    for (unsigned frame = 0; frame < RATE / 10; frame += BLOCK) {
        tr66_render(&kick, k, BLOCK);
        tr66_render(&snare, s, BLOCK);
        tr66_render(&noise, n, BLOCK);
        tr66_render(&pair, p, BLOCK);
        for (unsigned i = 0; i < BLOCK; ++i)
            assert(fabsf(p[i] - (0.85f * (k[i] + s[i] - n[i]) + n[i])) < 1e-5f);
    }

    tr66_init(&pair, RATE, 31);
    kick = pair;
    tr66_hit(&pair, MASK(KICK) | MASK(SNARE) | MASK(CYMBAL));
    tr66_hit(&kick, MASK(KICK) | MASK(CYMBAL));
    tr66_hit(&kick, MASK(SNARE));
    tr66_render(&pair, p, BLOCK);
    tr66_render(&kick, k, BLOCK);
    assert(memcmp(p, k, sizeof(p)) == 0);
}

static void test_snare_drives_low_bongo(void)
{
    Tr66 snare, bongo, combined;
    tr66_init(&snare, RATE, 31);
    combined = bongo = snare;
    tr66_hit(&snare, MASK(SNARE));
    tr66_hit(&bongo, MASK(LOW_BONGO));
    tr66_hit(&combined, MASK(SNARE) | MASK(LOW_BONGO));

    const Tr66VoiceState *weak = &snare.voices[TR66_LOW_BONGO];
    const Tr66VoiceState *direct = &bongo.voices[TR66_LOW_BONGO];
    const Tr66VoiceState *both = &combined.voices[TR66_LOW_BONGO];
    assert(weak->frames_left > 0);
    assert(weak->resonator[0].real != 0.0f);
    assert(weak->resonator[1].real == 0.0f && weak->resonator[2].real == 0.0f);
    assert(both->resonator[0].real == weak->resonator[0].real + direct->resonator[0].real);
    assert(both->resonator[0].imag == weak->resonator[0].imag + direct->resonator[0].imag);

    float s[BLOCK], b[BLOCK], c[BLOCK];
    for (unsigned frame = 0; frame < RATE / 5; frame += BLOCK) {
        tr66_render(&snare, s, BLOCK);
        tr66_render(&bongo, b, BLOCK);
        tr66_render(&combined, c, BLOCK);
        for (unsigned i = 0; i < BLOCK; ++i)
            assert(fabsf(c[i] - (s[i] + b[i])) < 1e-5f);
    }
}

static void test_cymbal_attack_after_gap(void)
{
    Tr66 machine;
    tr66_init(&machine, RATE, 31);
    const Tr66VoiceState *cymbal = &machine.voices[TR66_CYMBAL];
    const unsigned normal_release = cymbal->gate.fast_release_frame;

    tr66_hit(&machine, MASK(CYMBAL));
    const unsigned rested_release = cymbal->gate.fast_release_frame;
    const float rested_fast = cymbal->fast_noise;
    assert(rested_release > normal_release);
    assert(cymbal->frames_left == cymbal->gate.tail_frames);

    float out[BLOCK];
    for (unsigned frame = 0; frame < RATE / 10; frame += BLOCK)
        tr66_render(&machine, out, BLOCK);
    tr66_hit(&machine, MASK(CYMBAL));
    const float repeated_fast = cymbal->fast_noise;
    assert(repeated_fast < rested_fast);
    assert(cymbal->gate.fast_release_frame == normal_release);

    for (unsigned frame = 0; frame < RATE * 4 / 10; frame += BLOCK)
        tr66_render(&machine, out, BLOCK);
    tr66_hit(&machine, MASK(CYMBAL));
    assert(cymbal->fast_noise > repeated_fast && cymbal->fast_noise < rested_fast);
    assert(cymbal->gate.fast_release_frame > normal_release);
    assert(cymbal->gate.fast_release_frame < rested_release);

    tr66_hit(&machine, MASK(HAT) | MASK(CYMBAL));
    assert(cymbal->gate.fast_release_frame == normal_release);
    assert(cymbal->frames_left > cymbal->gate.tail_frames);
}

static void test_cymbal_extends_hat(void)
{
    Tr66 rested, recent, earlier, coincident;
    tr66_init(&rested, RATE, 31);
    recent = rested;
    coincident = rested;
    tr66_hit(&recent, MASK(CYMBAL));
    tr66_hit(&coincident, MASK(HAT) | MASK(CYMBAL));
    float a[BLOCK], b[BLOCK];
    for (unsigned frame = 0; frame < RATE / 2; frame += BLOCK) {
        tr66_render(&recent, b, BLOCK);
        tr66_render(&coincident, b, BLOCK);
    }
    earlier = recent;
    for (unsigned frame = 0; frame < RATE / 2; frame += BLOCK)
        tr66_render(&earlier, b, BLOCK);

    tr66_hit(&rested, MASK(HAT));
    tr66_hit(&recent, MASK(HAT));
    tr66_hit(&earlier, MASK(HAT));
    tr66_hit(&coincident, MASK(HAT));
    assert(coincident.voices[TR66_HAT].frames_left > recent.voices[TR66_HAT].frames_left);
    assert(recent.voices[TR66_HAT].frames_left > earlier.voices[TR66_HAT].frames_left);
    assert(earlier.voices[TR66_HAT].frames_left > rested.voices[TR66_HAT].frames_left);

    float late_peak = 0.0f;
    for (unsigned frame = 0; frame < RATE * 8 / 100; frame += BLOCK) {
        tr66_render(&rested, a, BLOCK);
        tr66_render(&recent, b, BLOCK);
        if (frame >= RATE * 6 / 100)
            for (unsigned i = 0; i < BLOCK; ++i) {
                assert(a[i] == 0.0f);
                late_peak = fmaxf(late_peak, fabsf(b[i]));
            }
    }
    assert(late_peak > 0.005f);
    for (unsigned frame = 0; frame < RATE / 8; frame += BLOCK)
        tr66_render(&recent, b, BLOCK);
    tr66_render(&recent, b, BLOCK);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(b[i] == 0.0f);
}

static void test_callbacks(void)
{
    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 7);
    seq.listener = record;
    seq.listener_context = &trace;
    assert(!tr66_seq_select(&seq, (Tr66Selection){TR66_ROCK_6, TR66_OFF, TR66_ARRANGE_A}));
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_BOSSA_NOVA, TR66_ROCK_6, TR66_ARRANGE_AUTO}));
    tr66_seq_start(&seq);
    float buffer[128];
    for (unsigned frame = 0; frame < 4 * 96000; frame += 128)
        tr66_seq_render(&seq, buffer, 128);
    assert(trace.count == 33);
    assert(trace.frame[0] == 0 && trace.mask[0] == (MASK(KICK) | MASK(HAT) | MASK(CYMBAL) | MASK(RIM)));
    assert(trace.frame[8] == 96000 && trace.mask[8] == (MASK(KICK) | MASK(HAT) | MASK(CYMBAL)));
    assert(trace.frame[9] == 108000 && trace.mask[9] == (MASK(HAT) | MASK(CYMBAL) | MASK(SNARE)));
    assert(trace.frame[24] == 288000);
    assert(trace.frame[28] == 330000 && trace.mask[28] == MASK(CYMBAL));
}

static void test_arrange_and_downbeat(void)
{
    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 5);
    seq.listener = record;
    seq.listener_context = &trace;
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_WALTZ, TR66_OFF, TR66_ARRANGE_B}));
    tr66_seq_start(&seq);
    float buffer[BLOCK];
    for (unsigned frame = 0; frame < 144000; frame += BLOCK)
        tr66_seq_render(&seq, buffer, BLOCK);
    assert(trace.count == 7);
    assert(trace.frame[3] == 72000 && trace.frame[5] == 114000);
    tr66_seq_fill(&seq);
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_OFF, TR66_ROCK_6, TR66_ARRANGE_A}));
    assert(seq.selection.rhythm == TR66_WALTZ);
    tr66_seq_render(&seq, buffer, 1);
    assert(seq.selection.rhythm == TR66_OFF && seq.fill && seq.bar_ticks == 96);
    assert(trace.frame[7] == 144000);
}

static void test_arrange_pickups(void)
{
    const uint16_t kick_snare = MASK(KICK) | MASK(SNARE);
    const uint16_t kick_hat = MASK(KICK) | MASK(HAT);
    const uint16_t snare_cymbal = MASK(SNARE) | MASK(CYMBAL);
    const struct {
        enum Tr66PatternId rhythm, variation;
        unsigned beats;
        uint16_t downbeat, auto_pickup, b_pickup;
    } cases[] = {
        {TR66_JAZZ_WALTZ, TR66_OFF, 3, MASK(KICK) | MASK(CYMBAL), MASK(SNARE), MASK(SNARE)},
        {TR66_OFF, TR66_BASS_SNARE_2, 4, kick_snare, MASK(CYMBAL), MASK(CYMBAL)},
        {TR66_OFF, TR66_FOXTROT_1, 4, kick_hat, snare_cymbal, snare_cymbal},
        {TR66_OFF, TR66_HABANERA, 4, kick_snare, MASK(CYMBAL), MASK(CYMBAL)},
        {TR66_OFF, TR66_BASS_SNARE_4, 4, kick_snare, MASK(CYMBAL), MASK(CYMBAL)},
        {TR66_OFF, TR66_FOXTROT_2, 4, kick_hat, 0, snare_cymbal},
        {TR66_OFF, TR66_TANGO, 4, kick_hat, snare_cymbal, snare_cymbal},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const unsigned bar_frames = cases[i].beats * RATE / 2;
        for (enum Tr66Arrange mode = TR66_ARRANGE_A; mode <= TR66_ARRANGE_B; ++mode) {
            Tr66Sequencer seq;
            Trace trace = {0};
            tr66_seq_init(&seq, RATE, 11);
            seq.listener = record;
            seq.listener_context = &trace;
            assert(tr66_seq_select(&seq, (Tr66Selection){cases[i].rhythm, cases[i].variation, mode}));
            tr66_seq_start(&seq);
            float buffer[BLOCK];
            for (unsigned frame = 0; frame < 4 * bar_frames; frame += BLOCK)
                tr66_seq_render(&seq, buffer, BLOCK);
            tr66_seq_render(&seq, buffer, 1);

            unsigned downbeats = 0;
            for (unsigned j = 0; j < trace.count; ++j) {
                if (trace.frame[j] % bar_frames != 0)
                    continue;
                const unsigned bar = (unsigned)(trace.frame[j] / bar_frames);
                uint16_t expected = cases[i].downbeat;
                if (mode == TR66_ARRANGE_AUTO && bar % 4 == 0)
                    expected |= cases[i].auto_pickup;
                if (mode == TR66_ARRANGE_B && bar % 2 == 0)
                    expected |= cases[i].b_pickup;
                assert(bar == downbeats && trace.mask[j] == expected);
                ++downbeats;
            }
            assert(downbeats == 5);
        }
    }
}

static void test_slow_rock_rock6_layer(void)
{
    const uint16_t y = MASK(CYMBAL), ky = MASK(KICK) | y;
    const uint16_t sy = MASK(SNARE) | y, khy = ky | MASK(HAT);
    /* Service Notes figures 13 and 15–17. Each 12-pulse bar reads
     * matrix counts 0–5 and 8–13. Pulse numbers below run across bars. */
    const struct {
        unsigned pulse;
        uint16_t voices;
    } expected[] = {
        {0, ky}, {2, y}, {4, khy}, {6, sy}, {8, y}, {10, khy},
        {12, ky}, {14, sy}, {16, khy}, {18, sy}, {20, y}, {22, khy},
        {24, ky}, {26, y}, {28, khy}, {30, sy}, {32, y}, {34, khy},
        {36, ky}, {38, sy}, {39, y}, {40, khy}, {42, sy},
        {44, TR66_QUIET_KICK | y}, {46, khy},
    };

    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 5);
    seq.listener = record;
    seq.listener_context = &trace;
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_SLOW_ROCK, TR66_ROCK_6, TR66_ARRANGE_AUTO}));
    tr66_seq_start(&seq);
    float buffer[BLOCK];
    for (unsigned frame = 0; frame < 4 * 72000; frame += BLOCK)
        tr66_seq_render(&seq, buffer, BLOCK);
    assert(trace.count == sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < trace.count; ++i) {
        assert(trace.frame[i] == expected[i].pulse * (RATE / 8));
        assert(trace.mask[i] == expected[i].voices);
    }
}

static void test_waltz_mambo_stack(void)
{
    const uint16_t m = MASK(MARACAS), mw = m | MASK(COWBELL);
    /* Player's Manual setting example. In triple time the six Mambo steps
     * here come from counts 0, 2, 4, 8, 10 and 12 of its score. */
    const uint16_t expected[] = {
        MASK(KICK) | MASK(CYMBAL) | MASK(LOW_BONGO) | mw,
        m, MASK(SNARE) | mw, MASK(HIGH_BONGO) | mw, MASK(SNARE) | m,
        MASK(CONGA) | mw,
    };
    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 5);
    seq.listener = record;
    seq.listener_context = &trace;
    Tr66Selection selection = {TR66_MAMBO, TR66_OFF, TR66_ARRANGE_A};
    const uint16_t pins = TR66_RHYTHM_BIT(TR66_WALTZ) | TR66_RHYTHM_BIT(TR66_MAMBO);
    assert(tr66_seq_select_layers(&seq, selection, pins));
    assert(!tr66_seq_select_layers(&seq, selection, TR66_RHYTHM_BIT(TR66_OFF)));
    tr66_seq_start(&seq);
    assert(seq.bar_ticks == 72 && seq.layer_count == 2);
    assert(tr66_seq_select(&seq, selection));

    float buffer[BLOCK];
    for (unsigned frame = 0; frame < 72000; frame += BLOCK)
        tr66_seq_render(&seq, buffer, BLOCK);
    assert(trace.count == sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < trace.count; ++i) {
        assert(trace.frame[i] == i * 12000);
        assert(trace.mask[i] == expected[i]);
    }
    tr66_seq_render(&seq, buffer, 1);
    assert(seq.bar_ticks == 96 && seq.layer_count == 1);
    assert(trace.frame[6] == 72000 && trace.mask[6] == (MASK(LOW_BONGO) | mw));
}

static void test_rhumba_bongo_route(void)
{
    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 5);
    seq.listener = record;
    seq.listener_context = &trace;
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_RHUMBA, TR66_OFF, TR66_ARRANGE_B}));
    tr66_seq_tempo(&seq, 120000);
    tr66_seq_start(&seq);

    float buffer[BLOCK];
    for (unsigned frame = 0; frame < 2 * 96000; frame += BLOCK)
        tr66_seq_render(&seq, buffer, BLOCK);

    const Tr66TriggerMask full = MASK(KICK) | MASK(LOW_BONGO);
    unsigned combined_hits = 0, quiet_hits = 0;
    unsigned light_bongos = 0, accented_bongos = 0;
    for (unsigned i = 0; i < trace.count; ++i) {
        const Tr66TriggerMask hit = trace.mask[i];
        assert(((hit & full) == full) == ((hit & TR66_RHUMBA_BONGO) != 0));
        assert(((hit & TR66_QUIET_KICK) != 0) == ((hit & TR66_RHUMBA_FILL_BONGO) != 0));
        if (hit & TR66_LIGHT_HIGH_BONGO) {
            const unsigned tick = (trace.frame[i] / 1000) % 96;
            assert(tick == 12 || tick == 60);
            ++light_bongos;
        }
        if (hit & TR66_ACCENT_HIGH_BONGO) {
            const unsigned tick = (trace.frame[i] / 1000) % 96;
            assert(tick == 84 || trace.frame[i] == 114000);
            ++accented_bongos;
        }
        if (hit & TR66_RHUMBA_BONGO)
            ++combined_hits;
        if (hit & TR66_QUIET_KICK) {
            assert(trace.frame[i] == 156000);
            assert(hit & MASK(LOW_BONGO));
            ++quiet_hits;
        }
    }
    assert(combined_hits == 6 && quiet_hits == 1);
    assert(light_bongos == 4 && accented_bongos == 3);

    Tr66 direct, fill;
    tr66_init(&direct, RATE, 5);
    fill = direct;
    tr66_hit(&direct, MASK(LOW_BONGO));
    tr66_hit(&fill, MASK(LOW_BONGO) | TR66_RHUMBA_FILL_BONGO);
    float inverted[BLOCK];
    tr66_render(&direct, buffer, BLOCK);
    tr66_render(&fill, inverted, BLOCK);
    assert(buffer[0] != 0.0f);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(inverted[i] == -buffer[i]);
}

static void test_rim_cowbell_shunt(void)
{
    Tr66Sequencer seq;
    Trace trace = {0};
    tr66_seq_init(&seq, RATE, 5);
    seq.listener = record;
    seq.listener_context = &trace;
    Tr66Selection selection = {TR66_MAMBO, TR66_OFF, TR66_ARRANGE_A};
    assert(tr66_seq_select_layers(&seq, selection, TR66_RHYTHM_BIT(TR66_BOSSA_NOVA)));
    tr66_seq_start(&seq);

    float buffer[BLOCK];
    for (unsigned frame = 0; frame < 96000; frame += BLOCK)
        tr66_seq_render(&seq, buffer, BLOCK);
    assert(trace.count == 8);
    /* A coincident RS pulse and a Mambo pulse on its own both sound rim. */
    assert(trace.mask[0] & MASK(RIM));
    assert(trace.frame[4] == 48000 && (trace.mask[4] & MASK(RIM)));
    for (unsigned i = 0; i < trace.count; ++i)
        assert(!(trace.mask[i] & MASK(COWBELL)));

    assert(tr66_seq_select(&seq, selection));
    tr66_seq_render(&seq, buffer, 1);
    assert(trace.frame[8] == 96000 && (trace.mask[8] & MASK(COWBELL)));
    assert(!(trace.mask[8] & MASK(RIM)));
}

static void test_split_blocks(void)
{
    static float whole[99000], sliced[99000];
    Tr66Sequencer a;
    tr66_seq_init(&a, RATE, 177);
    tr66_seq_tempo(&a, 123456);
    assert(tr66_seq_select(&a, (Tr66Selection){TR66_BOSSA_NOVA, TR66_ROCK_6, TR66_ARRANGE_AUTO}));
    tr66_seq_start(&a);
    Tr66Sequencer b = a;
    tr66_seq_render(&a, whole, sizeof(whole) / sizeof(whole[0]));
    for (unsigned i = 0; i < sizeof(sliced) / sizeof(sliced[0]);) {
        unsigned count = 57;
        if (i + count > sizeof(sliced) / sizeof(sliced[0]))
            count = sizeof(sliced) / sizeof(sliced[0]) - i;
        tr66_seq_render(&b, sliced + i, count);
        i += count;
    }
    assert(memcmp(whole, sliced, sizeof(whole)) == 0);
}

static void test_balance(void)
{
    const uint16_t masks[] = {MASK(KICK), MASK(HAT), MASK(CYMBAL), MASK(SNARE)};
    const float expected[][4] = {
        {1.422f, 0.0f, 0.0f, 1.0f},
        {1.247f, 0.576f, 0.576f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.628f, 1.283f, 1.283f, 1.0f},
        {0.0f, 1.470f, 1.470f, 1.0f},
    };
    float neutral[RATE / 40], adjusted[RATE / 40];
    for (unsigned step = 0; step < 5; ++step) {
        const float position = 0.5f * step - 1.0f;
        for (unsigned voice = 0; voice < 4; ++voice) {
            Tr66 reference, machine;
            tr66_init(&reference, RATE, 91);
            machine = reference;
            tr66_balance(&machine, position);
            /* Advance both noise generators while the fader settles. */
            for (unsigned block = 0; block < 12; ++block) {
                tr66_render(&reference, neutral, RATE / 40);
                tr66_render(&machine, adjusted, RATE / 40);
            }
            tr66_hit(&reference, masks[voice]);
            tr66_hit(&machine, masks[voice]);
            tr66_render(&reference, neutral, RATE / 40);
            tr66_render(&machine, adjusted, RATE / 40);
            float neutral_energy = 0.0f, adjusted_energy = 0.0f;
            for (unsigned frame = 0; frame < RATE / 40; ++frame) {
                neutral_energy += neutral[frame] * neutral[frame];
                adjusted_energy += adjusted[frame] * adjusted[frame];
            }
            assert(neutral_energy > 0.0f);
            const float ratio = sqrtf(adjusted_energy / neutral_energy);
            assert(fabsf(ratio - expected[step][voice]) < 0.001f);
        }
    }
}

static void test_layered_headroom(void)
{
    Tr66Sequencer seq;
    tr66_seq_init(&seq, RATE, UINT32_C(14293));
    tr66_seq_tempo(&seq, 200000);
    tr66_balance(&seq.synth, -1.0f);
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_BEGUINE, TR66_ROCK_4, TR66_ARRANGE_B}));
    tr66_seq_start(&seq);
    float samples[BLOCK];
    const unsigned frames = RATE * 60 / 200 * 4 * TR66_SCORE_BARS;
    for (unsigned frame = 0; frame < frames; frame += BLOCK) {
        tr66_seq_render(&seq, samples, BLOCK);
        for (unsigned i = 0; i < BLOCK; ++i)
            assert(fabsf(samples[i] * tr66_line_gain) < 1.0f);
    }
}

static void test_score_table(void)
{
    for (unsigned id = 1; id <= TR66_PATTERN_COUNT; ++id) {
        const Tr66Pattern *pattern = &tr66_patterns[id];
        assert(pattern->ticks == 72 || pattern->ticks == 96);
        for (unsigned bar = 0; bar < TR66_SCORE_BARS; ++bar) {
            const Tr66Bar *part = &pattern->bars[bar];
            assert(part->count > 0);
            for (unsigned i = 0; i < part->count; ++i) {
                const Tr66Event *event = &part->events[i];
                assert(event->tick < pattern->ticks);
                assert(event->voices != 0);
                if (i != 0)
                    assert(event->tick > part->events[i - 1].tick);
            }
        }
    }
}

static void test_samba_conga(void)
{
    const Tr66Pattern *samba = &tr66_patterns[TR66_SAMBA];
    const uint16_t conga = MASK(CONGA);
    for (unsigned bar = 0; bar < TR66_SCORE_BARS; ++bar) {
        const Tr66Bar *part = &samba->bars[bar];
        unsigned hits = 0;
        for (unsigned i = 0; i < part->count; ++i) {
            const Tr66Event *event = &part->events[i];
            assert(!(event->voices & MASK(LOW_BONGO)));
            if (event->voices & conga) {
                assert(event->tick == 48 || (bar == 3 && (event->tick == 60 || event->tick == 84)));
                ++hits;
            }
        }
        assert(hits == (bar == 3 ? 3u : 1u));
    }
}

static void test_rock1_offbeat(void)
{
    const Tr66Pattern *rock = &tr66_patterns[TR66_ROCK_1];
    for (unsigned bar = 0; bar < TR66_SCORE_BARS; ++bar) {
        assert(rock->bars[bar].count > 3);
        const Tr66Event offbeat = rock->bars[bar].events[3];
        assert(offbeat.tick == 36);
        assert(offbeat.voices == (MASK(KICK) | MASK(SNARE) | MASK(CYMBAL)));
    }
}

static void test_slow_rock_fill(void)
{
    const Tr66Bar *fill = &tr66_patterns[TR66_SLOW_ROCK].bars[3];
    assert(fill->count == 7);
    assert(fill->events[5].tick == 48);
    assert(fill->events[5].voices == (TR66_QUIET_KICK | MASK(CYMBAL)));
    assert(fill->events[6].tick == 60);
    assert(fill->events[6].voices == (MASK(KICK) | MASK(CYMBAL)));
}

static void test_waltz_fill(void)
{
    const Tr66Bar *fill = &tr66_patterns[TR66_WALTZ].bars[3];
    assert(fill->count == 4);
    assert(fill->events[0].voices == (MASK(KICK) | MASK(CYMBAL)));
    assert(fill->events[1].tick == 24);
    assert(fill->events[1].voices == (TR66_QUIET_KICK | MASK(SNARE) | MASK(CYMBAL)));
    assert(fill->events[2].tick == 42);
    assert(fill->events[2].voices == (TR66_QUIET_KICK | MASK(CYMBAL)));
    assert(fill->events[3].tick == 48);
    assert(fill->events[3].voices == (TR66_GHOST_KICK | MASK(SNARE) | MASK(CYMBAL)));
}

static void test_controls(void)
{
    Tr66Sequencer seq;
    Tr66Controls controls;
    tr66_seq_init(&seq, RATE, 117);
    tr66_controls_init(&controls);
    Tr66Input input = {.fader = {2048, 2048, 1830, 4095}, .track = TR66_KEY_T4};
    for (unsigned i = 0; i < 12; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    assert(controls.variation_enabled && seq.next_selection.variation == TR66_ROCK_6);
    assert(seq.next_selection.rhythm == TR66_BOSSA_NOVA);
    input.fader[2] = 1630; /* Just below Bossa's edge. */
    input.track = TR66_KEY_NONE;
    input.side = TR66_KEY_A;
    for (unsigned i = 0; i < 3; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    assert(controls.request.rhythm == TR66_BOSSA_NOVA);
    assert(controls.request.arrange == TR66_ARRANGE_A);
    input.track = TR66_KEY_PLAY;
    input.fn = true;
    for (unsigned i = 0; i < 6; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    assert(seq.running && seq.selection.arrange == TR66_ARRANGE_A);
    input.fn = false;
    assert(!tr66_controls_scan(&controls, &seq, input));
    assert(seq.fill_pending);
    input.track = TR66_KEY_INVALID;
    for (unsigned i = 0; i < 5; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    input.track = TR66_KEY_PLAY;
    for (unsigned i = 0; i < 5; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    assert(seq.running);
    input.fn = true;
    for (unsigned i = 0; i < 179; ++i)
        assert(!tr66_controls_scan(&controls, &seq, input));
    assert(tr66_controls_scan(&controls, &seq, input));
}

static void scan_key(Tr66Controls *controls, Tr66Sequencer *seq, Tr66Input input)
{
    for (unsigned i = 0; i < 3; ++i)
        assert(!tr66_controls_scan(controls, seq, input));
}

static void test_pin_rhythms(void)
{
    Tr66Sequencer seq;
    Tr66Controls controls;
    tr66_seq_init(&seq, RATE, 117);
    tr66_controls_init(&controls);
    Tr66Input input = {.fader = {1911, 2048, 614, 0}};
    scan_key(&controls, &seq, input);
    tr66_seq_start(&seq);

    const uint16_t waltz = TR66_RHYTHM_BIT(TR66_WALTZ);
    const uint16_t mambo = TR66_RHYTHM_BIT(TR66_MAMBO);
    input.fn = true;
    input.track = TR66_KEY_T4;
    scan_key(&controls, &seq, input);
    assert(controls.pinned_rhythms == waltz && !controls.variation_enabled);

    input.fn = false;
    input.track = TR66_KEY_NONE;
    input.fader[2] = 2662;
    scan_key(&controls, &seq, input);
    assert(!seq.fill_pending && seq.next_rhythms == (waltz | mambo));
    assert(seq.layer_count == 1);

    input.fn = true;
    input.track = TR66_KEY_T4;
    scan_key(&controls, &seq, input);
    assert(controls.pinned_rhythms == (waltz | mambo));
    input.track = TR66_KEY_NONE;
    scan_key(&controls, &seq, input);
    input.track = TR66_KEY_T4;
    scan_key(&controls, &seq, input);
    assert(controls.pinned_rhythms == waltz);

    input.track = TR66_KEY_NONE;
    input.fader[2] = 0;
    scan_key(&controls, &seq, input);
    assert(seq.next_rhythms == waltz);
    input.track = TR66_KEY_T4;
    scan_key(&controls, &seq, input);
    assert(!controls.pinned_rhythms && !seq.next_rhythms);

    input.fn = false;
    input.track = TR66_KEY_NONE;
    scan_key(&controls, &seq, input);
    assert(!seq.fill_pending);
    input.track = TR66_KEY_T4;
    scan_key(&controls, &seq, input);
    assert(controls.variation_enabled && seq.next_selection.variation == TR66_ROCK_1);
}

static void test_usb_midi_packets(void)
{
    const uint8_t realtime[] = {
        TR66_MIDI_CLOCK, TR66_MIDI_START, TR66_MIDI_CONTINUE, TR66_MIDI_STOP,
    };
    for (unsigned i = 0; i < sizeof(realtime); ++i)
        assert(tr66_midi_usb_event((uint8_t[]){0x0f, realtime[i], 0, 0}).status == realtime[i]);
    Tr66MidiEvent seek = tr66_midi_usb_event((uint8_t[]){0x03, 0xf2, 3, 4});
    assert(seek.status == TR66_MIDI_POSITION && seek.position == 515);
    assert(tr66_midi_usb_event((uint8_t[]){0x03, 0xf2, 0x80, 0}).status == TR66_MIDI_NONE);
    assert(tr66_midi_usb_event((uint8_t[]){0x1f, 0xf8, 0, 0}).status == TR66_MIDI_NONE);
    assert(tr66_midi_usb_event((uint8_t[]){0x0f, 0xfe, 0, 0}).status == TR66_MIDI_NONE);
    assert(tr66_midi_usb_event((uint8_t[]){0x09, 0x90, 36, 127}).status == TR66_MIDI_NONE);
}

static void test_midi_pattern_clock(void)
{
    enum { FRAMES_PER_PULSE = RATE / 2 / TR66_PPQN };
    Tr66Sequencer internal, external;
    tr66_seq_init(&internal, RATE, 43);
    assert(tr66_seq_select(&internal, (Tr66Selection){TR66_WALTZ, TR66_ROCK_6, TR66_ARRANGE_B}));
    external = internal;
    tr66_seq_start(&internal);
    tr66_seq_midi_event(&external, (Tr66MidiEvent){.status = TR66_MIDI_START}, 100);
    assert(!external.running && external.midi_armed);
    /* At exactly 120 BPM, feed one clock every 1000 samples. The voice
     * triggers, 3/4 counter skips, fill and new selection must match. */
    float a[FRAMES_PER_PULSE], b[FRAMES_PER_PULSE];
    for (unsigned pulse = 0; pulse < 400; ++pulse) {
        if (pulse == 220) {
            tr66_seq_fill(&internal);
            tr66_seq_fill(&external);
            Tr66Selection next = {TR66_MAMBO, TR66_HABANERA, TR66_ARRANGE_AUTO};
            assert(tr66_seq_select(&internal, next));
            assert(tr66_seq_select(&external, next));
        }
        tr66_seq_midi_event(&external, (Tr66MidiEvent){.status = TR66_MIDI_CLOCK},
                           103 + pulse * 21 + (pulse % 5 == 0 ? 2 : 0));
        /* Fader scans can still update the standalone tempo while following. */
        tr66_seq_tempo(&external, 200000);
        tr66_seq_render(&internal, a, FRAMES_PER_PULSE);
        tr66_seq_render(&external, b, FRAMES_PER_PULSE);
        assert(external.tick == internal.tick && external.bar == internal.bar);
        assert(memcmp(a, b, sizeof(a)) == 0);
    }
}

static void test_midi_transport(void)
{
    const Tr66MidiEvent clock = {.status = TR66_MIDI_CLOCK};
    const Tr66MidiEvent start = {.status = TR66_MIDI_START};
    const Tr66MidiEvent resume = {.status = TR66_MIDI_CONTINUE};
    const Tr66MidiEvent stop = {.status = TR66_MIDI_STOP};
    const Tr66MidiEvent seek = {.status = TR66_MIDI_POSITION, .position = 17};
    Tr66Sequencer seq;
    tr66_seq_init(&seq, RATE, 43);
    assert(tr66_seq_select(&seq, (Tr66Selection){TR66_WALTZ, TR66_OFF, TR66_ARRANGE_AUTO}));
    tr66_seq_midi_event(&seq, clock, 10);
    assert(!seq.running && !seq.midi_armed);
    tr66_seq_toggle_play(&seq);
    assert(seq.midi_armed && !seq.running);
    tr66_seq_toggle_play(&seq);
    tr66_seq_midi_event(&seq, clock, 30);
    assert(!seq.running);

    tr66_seq_midi_event(&seq, seek, 31);
    tr66_seq_midi_event(&seq, resume, 32);
    assert(seq.midi_armed && !seq.running);
    tr66_seq_midi_event(&seq, clock, 50);
    assert(seq.running && seq.bar == 1 && seq.tick == 30);
    tr66_seq_midi_event(&seq, seek, 51);
    tr66_seq_midi_event(&seq, clock, 70);
    assert(seq.bar == 1 && seq.tick == 31);

    tr66_seq_midi_event(&seq, stop, 71);
    tr66_seq_midi_poll(&seq, 10000);
    assert(!seq.running && seq.tick == 31);
    tr66_seq_midi_event(&seq, resume, 10001);
    tr66_seq_midi_event(&seq, clock, 10020);
    assert(seq.running && seq.bar == 1 && seq.tick == 32);

    tr66_seq_midi_event(&seq, start, UINT32_MAX - 15);
    tr66_seq_midi_event(&seq, clock, UINT32_MAX - 9);
    assert(seq.running && seq.bar == 0 && seq.tick == 0);
    tr66_seq_midi_poll(&seq, 489);
    assert(seq.running);
    tr66_seq_midi_poll(&seq, 490);
    assert(!seq.running && !seq.midi_clock_seen);
    tr66_seq_toggle_play(&seq);
    assert(seq.running && !seq.midi_follow);

    tr66_seq_midi_event(&seq, start, 500);
    tr66_seq_midi_event(&seq, clock, 520);
    tr66_seq_midi_disconnect(&seq);
    assert(!seq.running && !seq.midi_follow && !seq.midi_clock_seen);
    tr66_seq_toggle_play(&seq);
    tr66_seq_midi_disconnect(&seq);
    assert(seq.running);
}

int main(void)
{
    test_all_voices();
    test_snare_noise_envelope();
    test_shared_and_reduced_triggers();
    test_quiet_high_bongo();
    test_kick_snare_pair();
    test_snare_drives_low_bongo();
    test_cymbal_attack_after_gap();
    test_cymbal_extends_hat();
    test_callbacks();
    test_arrange_and_downbeat();
    test_arrange_pickups();
    test_slow_rock_rock6_layer();
    test_waltz_mambo_stack();
    test_rhumba_bongo_route();
    test_rim_cowbell_shunt();
    test_split_blocks();
    test_balance();
    test_layered_headroom();
    test_score_table();
    test_rock1_offbeat();
    test_samba_conga();
    test_slow_rock_fill();
    test_waltz_fill();
    test_controls();
    test_pin_rhythms();
    test_usb_midi_packets();
    test_midi_pattern_clock();
    test_midi_transport();
    puts("TR-66 voice, score, timing, Arrange, block, control and MIDI tests passed.");
}
