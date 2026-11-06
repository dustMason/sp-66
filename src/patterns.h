#ifndef TR66_PATTERNS_H
#define TR66_PATTERNS_H

#include <stdint.h>

enum {
    TR66_PPQN = 24, TR66_SCORE_BARS = 4,
    TR66_RHYTHM_COUNT = 9, TR66_VARIATION_COUNT = 18,
    TR66_PATTERN_COUNT = TR66_RHYTHM_COUNT + TR66_VARIATION_COUNT,
};

enum Tr66PatternId {
    TR66_OFF, TR66_WALTZ, TR66_JAZZ_WALTZ, TR66_SLOW_ROCK, TR66_BOSSA_NOVA,
    TR66_SAMBA, TR66_MAMBO, TR66_CHA_CHA, TR66_BEGUINE, TR66_RHUMBA,
    TR66_ROCK_1, TR66_ROCK_2, TR66_ROCK_3, TR66_ROCK_4, TR66_ROCK_5, TR66_ROCK_6,
    TR66_BASS_SNARE_2, TR66_FOXTROT_1, TR66_SWING_1, TR66_MARCH, TR66_PARADE,
    TR66_HABANERA, TR66_BASS_SNARE_4, TR66_FOXTROT_2, TR66_SWING_2,
    TR66_SWING_3, TR66_SHUFFLE, TR66_TANGO,
};

typedef struct {
    uint8_t tick;
    uint16_t voices;
} Tr66Event;

typedef struct {
    const Tr66Event *events;
    uint8_t count;
} Tr66Bar;

typedef struct {
    Tr66Bar bars[TR66_SCORE_BARS];
    uint16_t auto_pickup;
    uint16_t b_pickup;
    uint8_t ticks;
} Tr66Pattern;

extern const char *const tr66_pattern_names[TR66_PATTERN_COUNT + 1];
extern const Tr66Pattern tr66_patterns[TR66_PATTERN_COUNT + 1];

#endif
