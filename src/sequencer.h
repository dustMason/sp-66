#ifndef TR66_SEQUENCER_H
#define TR66_SEQUENCER_H

#include <stdbool.h>
#include "midi.h"
#include "patterns.h"
#include "tr66.h"

enum Tr66Arrange { TR66_ARRANGE_A, TR66_ARRANGE_AUTO, TR66_ARRANGE_B };

typedef struct {
    enum Tr66PatternId rhythm;
    enum Tr66PatternId variation;
    enum Tr66Arrange arrange;
} Tr66Selection;

typedef void (*Tr66EventListener)(void *context, uint64_t frame, uint16_t voices);

typedef struct {
    Tr66 synth;
    Tr66Selection selection, next_selection;
    uint64_t phase, period, frame;
    Tr66EventListener listener;
    void *listener_context;
    unsigned increment, tick, bar, bar_ticks;
    unsigned event_index[2];
    bool running, fill_pending, fill;
    uint32_t midi_activity_ms;
    uint16_t midi_position;
    bool midi_follow, midi_armed, midi_seek, midi_clock_seen;
} Tr66Sequencer;

void tr66_seq_init(Tr66Sequencer *seq, unsigned rate, uint32_t seed);
bool tr66_seq_select(Tr66Sequencer *seq, Tr66Selection selection);
void tr66_seq_tempo(Tr66Sequencer *seq, unsigned bpm_milli);
void tr66_seq_start(Tr66Sequencer *seq);
void tr66_seq_stop(Tr66Sequencer *seq);
void tr66_seq_toggle_play(Tr66Sequencer *seq);
void tr66_seq_midi_event(Tr66Sequencer *seq, Tr66MidiEvent event, uint32_t now_ms);
void tr66_seq_midi_poll(Tr66Sequencer *seq, uint32_t now_ms);
void tr66_seq_midi_disconnect(Tr66Sequencer *seq);
void tr66_seq_fill(Tr66Sequencer *seq);
void tr66_seq_render(Tr66Sequencer *seq, float *output, size_t frames);

#endif
