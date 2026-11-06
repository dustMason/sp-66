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
                               unsigned *index)
{
    const Tr66Pattern *pattern = &tr66_patterns[id];
    const unsigned bar = score_bar(seq);
    const Tr66Bar *part = &pattern->bars[bar];
    uint16_t voices = 0;
    if (*index < part->count && part->events[*index].tick == seq->tick)
        voices = part->events[(*index)++].voices;
    if (seq->selection.arrange == TR66_ARRANGE_AUTO && bar == 0 && seq->tick == 0)
        voices |= pattern->auto_pickup;
    return voices;
}

static void play_tick(Tr66Sequencer *seq)
{
    /* A coincident trigger fires each voice circuit once. */
    uint16_t mask = pattern_voices(seq, seq->selection.rhythm, &seq->event_index[0]);
    mask |= pattern_voices(seq, seq->selection.variation, &seq->event_index[1]);
    if (mask != 0) {
        tr66_hit(&seq->synth, mask);
        if (seq->listener != NULL)
            seq->listener(seq->listener_context, seq->frame, mask);
    }
}

static void begin_bar(Tr66Sequencer *seq)
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

void tr66_seq_start(Tr66Sequencer *seq)
{
    seq->running = true;
    seq->phase = 0;
    seq->frame = 0;
    seq->bar = 0;
    seq->fill_pending = false;
    begin_bar(seq);
}

void tr66_seq_stop(Tr66Sequencer *seq)
{
    seq->running = false;
    seq->fill_pending = false;
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
    while (frames > 0) {
        if (seq->phase >= seq->period) {
            seq->phase -= seq->period;
            if (++seq->tick == seq->bar_ticks) {
                seq->bar = (seq->bar + 1) % TR66_SCORE_BARS;
                begin_bar(seq);
            } else {
                play_tick(seq);
            }
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
