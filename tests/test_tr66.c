#include "controls.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { RATE = 48000, BLOCK = 64 };
#define MASK(voice) (UINT16_C(1) << TR66_ ## voice)

typedef struct {
    uint64_t frame[256];
    uint16_t mask[256];
    unsigned count;
} Trace;

static void record(void *context, uint64_t frame, uint16_t mask)
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
    tr66_hit(&second, TR66_REDUCED_KICK);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(b[i] == 0.5f * a[i]);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(KICK) | TR66_GHOST_KICK);
    tr66_hit(&second, TR66_GHOST_KICK);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(fabsf(b[i] - 0.19f * a[i]) < 1e-6f);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, TR66_QUIET_KICK | TR66_GHOST_KICK);
    tr66_hit(&second, TR66_QUIET_KICK);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    assert(memcmp(a, b, sizeof(a)) == 0);

    tr66_init(&first, RATE, 31);
    second = first;
    tr66_hit(&first, MASK(HIGH_BONGO) | TR66_REDUCED_HIGH_BONGO | TR66_QUIET_HIGH_BONGO);
    tr66_hit(&second, TR66_REDUCED_HIGH_BONGO | TR66_QUIET_HIGH_BONGO);
    tr66_render(&first, a, BLOCK);
    tr66_render(&second, b, BLOCK);
    for (unsigned i = 0; i < BLOCK; ++i)
        assert(fabsf(b[i] - 0.53f * a[i]) < 1e-6f);

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

static void test_cymbal_extends_hat(void)
{
    Tr66 rested, recent, earlier;
    tr66_init(&rested, RATE, 31);
    recent = rested;
    tr66_hit(&recent, MASK(CYMBAL));
    float a[BLOCK], b[BLOCK];
    for (unsigned frame = 0; frame < RATE / 2; frame += BLOCK)
        tr66_render(&recent, b, BLOCK);
    earlier = recent;
    for (unsigned frame = 0; frame < RATE / 2; frame += BLOCK)
        tr66_render(&earlier, b, BLOCK);

    tr66_hit(&rested, MASK(HAT));
    tr66_hit(&recent, MASK(HAT));
    tr66_hit(&earlier, MASK(HAT));
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

int main(void)
{
    test_all_voices();
    test_snare_noise_envelope();
    test_shared_and_reduced_triggers();
    test_cymbal_extends_hat();
    test_callbacks();
    test_arrange_and_downbeat();
    test_arrange_pickups();
    test_slow_rock_rock6_layer();
    test_split_blocks();
    test_layered_headroom();
    test_score_table();
    test_samba_conga();
    test_slow_rock_fill();
    test_waltz_fill();
    test_controls();
    puts("TR-66 voice, score, timing, Arrange, block and control tests passed.");
}
