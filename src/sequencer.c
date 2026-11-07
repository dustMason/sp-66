#include "sequencer.h"

static unsigned score_bar(const Tr66Sequencer *seq)
{
    if (seq->fill)
        return 3;
    switch (seq->selection.arrange) {
    case TR66_ARRANGE_A: return seq->bar % 2;
    case TR66_ARRANGE_B: return 2 + seq->bar % 2;
    case TR66_ARRANGE_AUTO: return seq->bar;
    }
    return 0;
}

static uint16_t pattern_voices(const Tr66Sequencer *seq, enum Tr66PatternId id,
                               unsigned bar, unsigned *index)
{
    enum { TRIPLE_HALF = 3 * TR66_PPQN / 2, COMMON_HALF = 2 * TR66_PPQN };
    const Tr66Pattern *pattern = &tr66_patterns[id];
    const Tr66Bar *part = &pattern->bars[bar];
    unsigned tick = seq->tick;
    /* The 3/4 shunt skips counter positions 6 and 7 of each half-bar.
     * In triple time the second half of a 4/4 score starts at count 8. */
    if (seq->bar_ticks == 2 * TRIPLE_HALF && pattern->ticks == 2 * COMMON_HALF &&
        tick >= TRIPLE_HALF)
        tick += COMMON_HALF - TRIPLE_HALF;

    while (*index < part->count && part->events[*index].tick < tick)
        ++*index;
    uint16_t voices = 0;
    if (*index < part->count && part->events[*index].tick == tick)
        voices = part->events[(*index)++].voices;
    /* The pickup is on the first pulse of the selected cycle. */
    if (seq->tick == 0) {
        if (bar == 0 && seq->selection.arrange == TR66_ARRANGE_AUTO)
            voices |= pattern->auto_pickup;
        if (bar == 2 && seq->selection.arrange == TR66_ARRANGE_B)
            voices |= pattern->b_pickup;
    }
    return voices;
}

static void play_tick(Tr66Sequencer *seq)
{
    const unsigned bar = score_bar(seq);
    /* A coincident trigger fires each voice circuit once. */
    uint16_t mask = pattern_voices(seq, seq->selection.rhythm, bar, &seq->event_index[0]);
    mask |= pattern_voices(seq, seq->selection.variation, bar, &seq->event_index[1]);
    if (mask != 0) {
        tr66_hit(&seq->synth, mask);
        if (seq->listener != NULL)
            seq->listener(seq->listener_context, seq->frame, mask);
    }
}

static void prepare_bar(Tr66Sequencer *seq)
{
    seq->tick = 0;
    seq->event_index[0] = 0;
    seq->event_index[1] = 0;
    seq->selection = seq->next_selection;
    seq->fill = seq->fill_pending;
    seq->fill_pending = false;
    unsigned rhythm_ticks = tr66_patterns[seq->selection.rhythm].ticks;
    unsigned variation_ticks = tr66_patterns[seq->selection.variation].ticks;
    seq->bar_ticks = rhythm_ticks != 0 ? rhythm_ticks : variation_ticks;
    if (seq->bar_ticks == 0)
        seq->bar_ticks = 4 * TR66_PPQN;
}

static void advance_tick(Tr66Sequencer *seq)
{
    if (++seq->tick == seq->bar_ticks) {
        seq->bar = (seq->bar + 1) % TR66_SCORE_BARS;
        prepare_bar(seq);
    }
    play_tick(seq);
}

void tr66_seq_init(Tr66Sequencer *seq, unsigned rate, uint32_t seed)
{
    *seq = (Tr66Sequencer){0};
    tr66_init(&seq->synth, (float)rate, seed);
    seq->period = (uint64_t)rate * 60000;
    tr66_seq_tempo(seq, 120000);
}

bool tr66_seq_select(Tr66Sequencer *seq, Tr66Selection selection)
{
    if ((unsigned)selection.rhythm > TR66_RHYTHM_COUNT ||
        (unsigned)selection.variation > TR66_PATTERN_COUNT ||
        (selection.variation != TR66_OFF && (unsigned)selection.variation <= TR66_RHYTHM_COUNT) ||
        (unsigned)selection.arrange > TR66_ARRANGE_B)
        return false;
    if ((selection.rhythm != TR66_OFF && tr66_patterns[selection.rhythm].ticks == 0) ||
        (selection.variation != TR66_OFF && tr66_patterns[selection.variation].ticks == 0))
        return false;
    seq->next_selection = selection;
    return true;
}

void tr66_seq_tempo(Tr66Sequencer *seq, unsigned bpm_milli)
{
    if (bpm_milli < 50000)
        bpm_milli = 50000;
    if (bpm_milli > 200000)
        bpm_milli = 200000;
    seq->increment = bpm_milli * TR66_PPQN;
}

static void start_at(Tr66Sequencer *seq, unsigned tick)
{
    seq->running = true;
    seq->phase = 0;
    seq->frame = 0;
    seq->fill_pending = false;
    prepare_bar(seq);
    seq->bar = (tick / seq->bar_ticks) % TR66_SCORE_BARS;
    seq->tick = tick % seq->bar_ticks;
    play_tick(seq);
}

void tr66_seq_start(Tr66Sequencer *seq)
{
    seq->midi_follow = false;
    seq->midi_armed = false;
    seq->midi_seek = false;
    start_at(seq, 0);
}

void tr66_seq_stop(Tr66Sequencer *seq)
{
    seq->running = false;
    seq->fill_pending = false;
    seq->midi_armed = false;
}

static void midi_arm(Tr66Sequencer *seq)
{
    tr66_seq_stop(seq);
    seq->midi_follow = true;
    seq->midi_armed = true;
}

void tr66_seq_toggle_play(Tr66Sequencer *seq)
{
    if (seq->running || seq->midi_armed) {
        tr66_seq_stop(seq);
    } else if (seq->midi_clock_seen) {
        /* Allow a clock source that sends pulses without transport. */
        seq->midi_seek = true;
        seq->midi_position = 0;
        midi_arm(seq);
    } else {
        tr66_seq_start(seq);
    }
}

void tr66_seq_midi_disconnect(Tr66Sequencer *seq)
{
    if (seq->midi_follow)
        tr66_seq_stop(seq);
    seq->midi_follow = false;
    seq->midi_clock_seen = false;
    seq->midi_seek = false;
}

void tr66_seq_midi_poll(Tr66Sequencer *seq, uint32_t now_ms)
{
    /* Unsigned subtraction keeps this valid across uptime wraparound. */
    if ((seq->midi_armed || seq->midi_clock_seen) &&
        now_ms - seq->midi_activity_ms >= 500) {
        seq->midi_clock_seen = false;
        if (seq->midi_follow)
            tr66_seq_stop(seq);
    }
}

void tr66_seq_midi_event(Tr66Sequencer *seq, Tr66MidiEvent event, uint32_t now_ms)
{
    _Static_assert(TR66_PPQN == 24, "The score uses one tick per MIDI clock");
    tr66_seq_midi_poll(seq, now_ms);
    switch (event.status) {
    case TR66_MIDI_CLOCK:
        seq->midi_activity_ms = now_ms;
        seq->midi_clock_seen = true;
        if (!seq->midi_follow || (!seq->running && !seq->midi_armed))
            break;
        seq->running = true;
        seq->midi_armed = false;
        if (seq->midi_seek) {
            seq->midi_seek = false;
            start_at(seq, seq->midi_position * (TR66_PPQN / 4));
        } else {
            advance_tick(seq);
        }
        break;
    case TR66_MIDI_START:
        seq->midi_seek = true;
        seq->midi_position = 0;
        seq->midi_activity_ms = now_ms;
        midi_arm(seq);
        break;
    case TR66_MIDI_CONTINUE:
        if (seq->midi_follow && (seq->running || seq->midi_armed))
            break;
        if (!seq->midi_follow && !seq->midi_seek) {
            seq->midi_seek = true;
            seq->midi_position = 0;
        }
        seq->midi_activity_ms = now_ms;
        midi_arm(seq);
        break;
    case TR66_MIDI_STOP:
        if (seq->midi_follow) {
            tr66_seq_stop(seq);
            seq->midi_activity_ms = now_ms;
        }
        break;
    case TR66_MIDI_POSITION:
        if (!seq->midi_follow || (!seq->running && !seq->midi_armed)) {
            seq->midi_position = event.position;
            seq->midi_seek = true;
        }
        break;
    }
}

void tr66_seq_fill(Tr66Sequencer *seq)
{
    if (seq->running)
        seq->fill_pending = true;
}

void tr66_seq_render(Tr66Sequencer *seq, float *output, size_t frames)
{
    if (!seq->running) {
        tr66_render(&seq->synth, output, frames);
        return;
    }
    if (seq->midi_follow) {
        tr66_render(&seq->synth, output, frames);
        seq->frame += frames;
        return;
    }
    while (frames > 0) {
        if (seq->phase >= seq->period) {
            seq->phase -= seq->period;
            advance_tick(seq);
        }
        const uint64_t distance = seq->period - seq->phase;
        size_t span = (size_t)((distance + seq->increment - 1) / seq->increment);
        if (span > frames)
            span = frames;
        tr66_render(&seq->synth, output, span);
        seq->phase += (uint64_t)span * seq->increment;
        seq->frame += span;
        output += span;
        frames -= span;
    }
}
