#ifndef TR66_CONTROLS_H
#define TR66_CONTROLS_H

#include "sequencer.h"

enum Tr66ControlKey {
    TR66_KEY_NONE, TR66_KEY_T1, TR66_KEY_T2, TR66_KEY_T3, TR66_KEY_T4,
    TR66_KEY_PLAY, TR66_KEY_A, TR66_KEY_B, TR66_KEY_DOWN, TR66_KEY_UP, TR66_KEY_INVALID,
};

typedef struct {
    enum Tr66ControlKey candidate, stable;
    unsigned consecutive;
} Tr66KeyDebounce;

typedef struct {
    uint16_t fader[4];       /* 0 through 4095, starting at the bottom */
    enum Tr66ControlKey track, side;
    bool fn;
} Tr66Input;

typedef struct {
    Tr66Selection request;
    unsigned rhythm_slot, variation_slot, bank, volume;
    Tr66KeyDebounce track, side;
    unsigned fn_scans;
    bool variation_enabled, initialized, unavailable;
} Tr66Controls;

void tr66_controls_init(Tr66Controls *controls);
/* Call on each 10 ms input scan. True requests shutdown. */
bool tr66_controls_scan(Tr66Controls *controls, Tr66Sequencer *seq, Tr66Input input);

#endif
