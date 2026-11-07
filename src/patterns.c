#include "patterns.h"

const char *const tr66_pattern_names[TR66_PATTERN_COUNT + 1] = {
    "off", "waltz", "jazz-waltz", "slow-rock", "bossa-nova",
    "samba", "mambo", "cha-cha", "beguine", "rhumba",
    "rock-1", "rock-2", "rock-3", "rock-4", "rock-5", "rock-6",
    "bass-snare-2", "foxtrot-1", "swing-1", "march", "parade", "habanera",
    "bass-snare-4", "foxtrot-2", "swing-2", "swing-3", "shuffle", "tango",
};

/* Player's Manual scores, checked against Service Notes figures 13–18 and
 * recorded loops. Each event time is in 24 ticks per quarter note. */
#define K  (1u << TR66_KICK)
#define SOFT_K  TR66_REDUCED_KICK
#define QUIET_K  TR66_QUIET_KICK
#define S  (1u << TR66_SNARE)
#define H  (1u << TR66_HAT)
#define Y  (1u << TR66_CYMBAL)
#define TANGO_SY  (S | TR66_TANGO_CYMBAL)
#define R  (1u << TR66_RIM)
#define C  (1u << TR66_CONGA)
#define SOFT_C  TR66_REDUCED_CONGA
#define CHA_CHA_C  TR66_CHA_CHA_CONGA
#define M  (1u << TR66_MARACAS)
#define V  (1u << TR66_CLAVES)
#define W  (1u << TR66_COWBELL)
#define U  (1u << TR66_HIGH_BONGO)
#define SOFT_U  TR66_REDUCED_HIGH_BONGO
#define QUIET_U  TR66_QUIET_HIGH_BONGO
#define LIGHT_U  TR66_LIGHT_HIGH_BONGO
#define ACCENT_U  TR66_ACCENT_HIGH_BONGO
#define MAMBO_U  TR66_MAMBO_FILL_HIGH_BONGO
#define SAMBA_WU  (W | TR66_SAMBA_BELL_BONGO)
#define L  (1u << TR66_LOW_BONGO)
#define RHUMBA_KL  (K | L | TR66_RHUMBA_BONGO)
#define BAR(events) {events, sizeof(events) / sizeof((events)[0])}

static const Tr66Event waltz_plain[] = {{0, K | Y}, {24, S}, {48, S}};
/* Drumbase and Goldbaby both have a full downbeat and three quieter fill kicks. */
static const Tr66Event waltz_fill[] = {
    {0, K | Y}, {24, QUIET_K | S | TR66_WALTZ_FILL_CYMBAL},
    {42, QUIET_K | TR66_WALTZ_FILL_CYMBAL}, {48, TR66_GHOST_KICK | S | TR66_WALTZ_FILL_CYMBAL},
};

static const Tr66Event jazz_waltz_a1[] = {
    {0, K | Y}, {24, S | Y}, {42, Y}, {48, S | Y},
};
static const Tr66Event jazz_waltz_b2[] = {
    {0, K | Y}, {24, S | Y}, {42, S | Y}, {48, S | Y},
};

static const Tr66Event slow_rock_a[] = {
    {0, K | Y}, {12, Y}, {24, Y}, {36, S | Y}, {48, Y}, {60, K | Y},
};
static const Tr66Event slow_rock_b2[] = {
    {0, K | Y}, {12, Y}, {18, Y}, {24, Y}, {36, S | Y}, {48, QUIET_K | Y}, {60, K | Y},
};

static const Tr66Event bossa_a1[] = {
    {0, K | H | R}, {12, H}, {24, H | Y}, {36, SOFT_K | H | R},
    {48, K | H}, {60, H}, {72, H | R | Y}, {84, SOFT_K | H},
};
static const Tr66Event bossa_a2[] = {
    {0, K | H}, {12, H}, {24, H | R | Y}, {36, SOFT_K | H},
    {48, K | H}, {60, H | R}, {72, H | Y}, {84, SOFT_K | H},
};
static const Tr66Event bossa_b2[] = {
    {0, K | H}, {12, H}, {24, H | R | Y}, {36, SOFT_K | H},
    {48, K | H | R}, {60, H | R}, {72, H | Y}, {84, SOFT_K | H},
};

static const Tr66Event samba_a1[] = {
    {0, M | SAMBA_WU}, {12, M}, {24, M | Y | SAMBA_WU | TR66_SAMBA_ACCENT_BONGO}, {36, M},
    {48, M | C}, {60, M}, {72, M | Y}, {84, M | SAMBA_WU | TR66_SAMBA_SOFT_BONGO},
};
static const Tr66Event samba_a2[] = {
    {0, M}, {12, M | SAMBA_WU | TR66_SAMBA_ACCENT_BONGO},
    {24, M | Y}, {36, M | SAMBA_WU | TR66_SAMBA_LIGHT_BONGO},
    {48, M | C}, {60, M}, {72, M | Y}, {84, M},
};
static const Tr66Event samba_b2[] = {
    {0, M}, {12, M | SAMBA_WU | TR66_SAMBA_ACCENT_BONGO},
    {24, M | Y}, {36, M | SAMBA_WU | TR66_SAMBA_LIGHT_BONGO},
    {48, M | C}, {60, M | C}, {72, M | Y}, {84, M | C},
};

static const Tr66Event mambo_a[] = {
    {0, M | W | L}, {12, M}, {24, M | W}, {36, M | U},
    {48, M | W | U}, {60, M}, {72, M | W | C}, {84, M | C},
};
static const Tr66Event mambo_b2[] = {
    {0, M | W | L}, {12, M | MAMBO_U | C}, {24, M | W}, {36, M | U},
    {48, M | W | U | C}, {60, M}, {72, M | W | C}, {84, M | MAMBO_U | C},
};

static const Tr66Event cha_cha_a[] = {
    {0, M | W | K}, {12, M}, {24, M | W}, {36, M | K},
    {48, M | W | K}, {60, M}, {72, M | W | K | CHA_CHA_C}, {84, M | CHA_CHA_C},
};
static const Tr66Event cha_cha_b2[] = {
    {0, M | W | K}, {12, M | K | CHA_CHA_C}, {24, M | W}, {36, M | K},
    {48, M | W | K | CHA_CHA_C}, {60, M | W},
    {72, M | W | K | CHA_CHA_C}, {84, M | K | CHA_CHA_C},
};

static const Tr66Event beguine_a1[] = {
    {0, M | V | L}, {12, M | Y | U}, {24, M}, {36, M | V | QUIET_U},
    {48, M | L}, {60, M | QUIET_U}, {72, M | V | L | C}, {84, M | QUIET_U | C},
};
static const Tr66Event beguine_a2[] = {
    {0, M | L}, {12, M | Y | U}, {24, M | V}, {36, M | QUIET_U},
    {48, M | V | L}, {60, M | QUIET_U}, {72, M | L | C}, {84, M | QUIET_U | C},
};
static const Tr66Event beguine_b2[] = {
    {0, M | L}, {12, M | Y | U | TR66_BEGUINE_FILL_LOW_BONGO | TR66_BEGUINE_FILL_CONGA},
    {18, M}, {24, M | V}, {36, M | QUIET_U},
    {48, M | V | L | C}, {60, M | QUIET_U}, {72, M | L | C},
    {84, M | QUIET_U | TR66_BEGUINE_FILL_LOW_BONGO | C},
};

/* Both recordings have three high-bongo ring levels in the same steps. */
static const Tr66Event rhumba_a1[] = {
    {0, M | V | RHUMBA_KL}, {12, M | LIGHT_U}, {24, M | SOFT_U}, {36, M | V | RHUMBA_KL},
    {48, M | SOFT_U}, {60, M | LIGHT_U},
    {72, M | V | SOFT_C | RHUMBA_KL}, {84, M | ACCENT_U | SOFT_C},
};
static const Tr66Event rhumba_a2[] = {
    {0, M | RHUMBA_KL}, {12, M | LIGHT_U}, {24, M | V | SOFT_U}, {36, M | RHUMBA_KL},
    {48, M | V | SOFT_U}, {60, M | LIGHT_U},
    {72, M | SOFT_C | RHUMBA_KL}, {84, M | ACCENT_U | SOFT_C},
};
static const Tr66Event rhumba_b2[] = {
    {0, M | RHUMBA_KL}, {12, M | LIGHT_U | SOFT_C}, {18, M | ACCENT_U}, {24, M | V | SOFT_U},
    {36, M | RHUMBA_KL}, {48, M | V | SOFT_U | SOFT_C},
    {60, M | LIGHT_U | L | QUIET_K | TR66_RHUMBA_FILL_BONGO},
    {72, M | SOFT_C | RHUMBA_KL}, {84, M | ACCENT_U | SOFT_C},
};

static const Tr66Event rock1_a[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, K | S | Y},
    {48, K | Y}, {60, Y}, {72, S | H | Y}, {84, Y},
};
static const Tr66Event rock1_b2[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, K | S | Y}, {42, Y},
    {48, K | Y}, {60, Y}, {72, S | H | Y}, {84, Y},
};

static const Tr66Event rock2_a[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, SOFT_K | Y},
    {48, K | Y}, {60, Y}, {72, S | H | Y}, {84, SOFT_K | Y},
};
static const Tr66Event rock2_b2[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, SOFT_K | Y}, {42, Y},
    {48, K | Y}, {60, Y}, {72, S | H | Y}, {84, SOFT_K | Y},
};

static const Tr66Event rock3_a[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, SOFT_K | Y},
    {48, K | Y}, {60, S | Y}, {72, H | Y}, {84, SOFT_K | Y},
};
static const Tr66Event rock3_b2[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, SOFT_K | Y}, {42, Y},
    {48, K | Y}, {60, S | Y}, {72, H | Y}, {84, SOFT_K | Y},
};

static const Tr66Event rock4_a1[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, Y}, {42, S},
    {48, K | Y}, {60, S | Y}, {72, H | Y}, {84, S | Y},
};
static const Tr66Event rock4_a2[] = {
    {0, K | Y}, {12, K | Y}, {24, S | H | Y}, {36, Y}, {42, S},
    {48, K | Y}, {60, S | Y}, {72, H | Y}, {84, K | S | Y},
};
static const Tr66Event rock4_b2[] = {
    {0, K | Y}, {12, K | Y}, {24, S | H | Y}, {36, Y}, {42, S | Y},
    {48, K | Y}, {60, S | Y}, {72, H | Y}, {84, K | S | Y},
};

static const Tr66Event rock5_a[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, Y}, {42, K | S},
    {48, K | Y}, {60, K | Y}, {72, S | H | Y}, {84, K | Y},
};
static const Tr66Event rock5_b2[] = {
    {0, K | Y}, {12, Y}, {24, S | H | Y}, {36, Y}, {42, K | S | Y},
    {48, K | Y}, {60, K | Y}, {72, S | H | Y}, {84, K | Y},
};

static const Tr66Event rock6_a1[] = {
    {0, K | Y}, {12, Y}, {24, K | H | Y}, {36, Y},
    {48, S | Y}, {60, Y}, {72, K | H | Y}, {84, S | Y},
};
static const Tr66Event rock6_a2[] = {
    {0, K | Y}, {12, S | Y}, {24, K | H | Y}, {36, Y},
    {48, S | Y}, {60, Y}, {72, H | Y}, {84, S | Y},
};
static const Tr66Event rock6_b2[] = {
    {0, K | Y}, {12, S | Y}, {24, K | H | Y}, {36, Y}, {42, Y},
    {48, S | Y}, {60, Y}, {72, H | Y}, {84, S | Y},
};

static const Tr66Event bass_snare_2_a[] = {{0, K | S}, {48, K | S}};
static const Tr66Event bass_snare_2_b2[] = {
    {0, K | S}, {42, Y}, {48, K | S | Y}, {60, Y}, {84, Y},
};

static const Tr66Event foxtrot_1_a[] = {{0, K | H}, {24, S | Y}, {48, K | H}, {72, S | Y}};
static const Tr66Event foxtrot_1_b2[] = {
    {0, K | H}, {24, S | Y}, {42, S | Y}, {48, K | H | S | Y},
    {60, S | Y}, {72, S | Y}, {84, S | Y},
};

static const Tr66Event swing_1_a[] = {
    {0, K | Y}, {24, H}, {42, H}, {48, K | Y}, {72, H}, {90, H},
};
static const Tr66Event swing_1_b2[] = {
    {0, K | Y}, {24, H}, {42, K | H | Y}, {48, K | Y}, {60, K | Y},
    {72, H}, {84, K | Y}, {90, H},
};

static const Tr66Event march_a[] = {{0, K | Y}, {24, S}, {48, K | Y}, {72, S}};
static const Tr66Event march_b2[] = {
    {0, K | Y}, {24, S}, {42, Y}, {48, K | Y}, {60, Y}, {72, S}, {84, Y},
};

static const Tr66Event parade_a2[] = {
    {0, K | Y}, {24, S}, {36, S}, {48, K | Y}, {72, S},
};
static const Tr66Event parade_b2[] = {
    {0, K | Y}, {24, S}, {36, S}, {42, Y}, {48, K | Y}, {60, Y}, {72, S}, {84, Y},
};

static const Tr66Event habanera_a[] = {{0, K | S}, {36, S}, {48, K | S}, {72, S}};
static const Tr66Event habanera_b2[] = {
    {0, K | S}, {12, S}, {36, S}, {42, Y}, {48, K | S | Y}, {60, Y}, {72, S}, {84, S | Y},
};

static const Tr66Event bass_snare_4_a[] = {{0, K | S}, {24, K | S}, {48, K | S}, {72, K | S}};
static const Tr66Event bass_snare_4_b2[] = {
    {0, K | S}, {24, K | S}, {42, Y}, {48, K | S | Y}, {60, Y}, {72, K | S}, {84, Y},
};

static const Tr66Event foxtrot_2_a[] = {
    {0, K | H}, {24, K | S | Y}, {48, K | H}, {72, K | S | Y},
};
static const Tr66Event foxtrot_2_b2[] = {
    {0, K | H}, {24, K | S | Y}, {42, S | Y}, {48, K | H | S | Y},
    {60, S | Y}, {72, K | S | Y}, {84, S | Y},
};

static const Tr66Event swing_2_a[] = {
    {0, K | Y}, {24, K | H}, {42, H}, {48, K | Y}, {72, K | H}, {90, H},
};
static const Tr66Event swing_2_b2[] = {
    {0, K | Y}, {24, K | H}, {42, H | Y}, {48, K | Y}, {60, Y}, {72, K | H}, {84, Y}, {90, H},
};

static const Tr66Event swing_3_a[] = {
    {0, K | Y}, {24, K | H | Y}, {42, H}, {48, K | Y}, {72, K | H | Y}, {90, H},
};
static const Tr66Event swing_3_b2[] = {
    {0, K | Y}, {24, K | H | Y}, {42, H | Y}, {48, K | Y}, {60, Y}, {72, K | H | Y}, {84, Y}, {90, H},
};

static const Tr66Event shuffle_a[] = {
    {0, K | S | Y}, {18, S}, {24, K | S | Y}, {42, S},
    {48, K | S | Y}, {66, S}, {72, K | S | Y}, {90, S},
};
static const Tr66Event shuffle_b2[] = {
    {0, K | S | Y}, {18, S}, {24, K | S | Y}, {42, S | Y},
    {48, K | S | Y}, {60, Y}, {66, S}, {72, K | S | Y}, {84, Y}, {90, S},
};

static const Tr66Event tango_a[] = {
    {0, K | H}, {24, K | H}, {48, K | H}, {72, K | H}, {84, TANGO_SY},
};
static const Tr66Event tango_b2[] = {
    {0, K | H}, {24, K | H}, {42, TANGO_SY}, {48, K | H | TANGO_SY},
    {60, TANGO_SY}, {72, K | H}, {84, TANGO_SY},
};

const Tr66Pattern tr66_patterns[TR66_PATTERN_COUNT + 1] = {
    [TR66_WALTZ] = {
        .bars = {BAR(waltz_plain), BAR(waltz_plain), BAR(waltz_plain), BAR(waltz_fill)},
        .ticks = 72,
    },
    [TR66_JAZZ_WALTZ] = {
        .bars = {BAR(jazz_waltz_a1), BAR(jazz_waltz_a1), BAR(jazz_waltz_a1), BAR(jazz_waltz_b2)},
        .auto_pickup = S, .b_pickup = S, .ticks = 72,
    },
    [TR66_SLOW_ROCK] = {
        .bars = {BAR(slow_rock_a), BAR(slow_rock_a), BAR(slow_rock_a), BAR(slow_rock_b2)},
        .ticks = 72,
    },
    [TR66_BOSSA_NOVA] = {
        .bars = {BAR(bossa_a1), BAR(bossa_a2), BAR(bossa_a1), BAR(bossa_b2)},
        .ticks = 96,
    },
    [TR66_SAMBA] = {
        .bars = {BAR(samba_a1), BAR(samba_a2), BAR(samba_a1), BAR(samba_b2)},
        .ticks = 96,
    },
    [TR66_MAMBO] = {
        .bars = {BAR(mambo_a), BAR(mambo_a), BAR(mambo_a), BAR(mambo_b2)},
        .ticks = 96,
    },
    [TR66_CHA_CHA] = {
        .bars = {BAR(cha_cha_a), BAR(cha_cha_a), BAR(cha_cha_a), BAR(cha_cha_b2)},
        .ticks = 96,
    },
    [TR66_BEGUINE] = {
        .bars = {BAR(beguine_a1), BAR(beguine_a2), BAR(beguine_a1), BAR(beguine_b2)},
        .ticks = 96,
    },
    [TR66_RHUMBA] = {
        .bars = {BAR(rhumba_a1), BAR(rhumba_a2), BAR(rhumba_a1), BAR(rhumba_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_1] = {
        .bars = {BAR(rock1_a), BAR(rock1_a), BAR(rock1_a), BAR(rock1_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_2] = {
        .bars = {BAR(rock2_a), BAR(rock2_a), BAR(rock2_a), BAR(rock2_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_3] = {
        .bars = {BAR(rock3_a), BAR(rock3_a), BAR(rock3_a), BAR(rock3_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_4] = {
        .bars = {BAR(rock4_a1), BAR(rock4_a2), BAR(rock4_a1), BAR(rock4_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_5] = {
        .bars = {BAR(rock5_a), BAR(rock5_a), BAR(rock5_a), BAR(rock5_b2)},
        .ticks = 96,
    },
    [TR66_ROCK_6] = {
        .bars = {BAR(rock6_a1), BAR(rock6_a2), BAR(rock6_a1), BAR(rock6_b2)},
        .ticks = 96,
    },
    [TR66_BASS_SNARE_2] = {
        .bars = {BAR(bass_snare_2_a), BAR(bass_snare_2_a), BAR(bass_snare_2_a), BAR(bass_snare_2_b2)},
        .auto_pickup = Y, .b_pickup = Y, .ticks = 96,
    },
    [TR66_FOXTROT_1] = {
        .bars = {BAR(foxtrot_1_a), BAR(foxtrot_1_a), BAR(foxtrot_1_a), BAR(foxtrot_1_b2)},
        .auto_pickup = S | Y, .b_pickup = S | Y, .ticks = 96,
    },
    [TR66_SWING_1] = {
        .bars = {BAR(swing_1_a), BAR(swing_1_a), BAR(swing_1_a), BAR(swing_1_b2)},
        .ticks = 96,
    },
    [TR66_MARCH] = {
        .bars = {BAR(march_a), BAR(march_a), BAR(march_a), BAR(march_b2)},
        .ticks = 96,
    },
    [TR66_PARADE] = {
        .bars = {BAR(march_a), BAR(parade_a2), BAR(march_a), BAR(parade_b2)},
        .ticks = 96,
    },
    [TR66_HABANERA] = {
        .bars = {BAR(habanera_a), BAR(habanera_a), BAR(habanera_a), BAR(habanera_b2)},
        .auto_pickup = Y, .b_pickup = Y, .ticks = 96,
    },
    [TR66_BASS_SNARE_4] = {
        .bars = {BAR(bass_snare_4_a), BAR(bass_snare_4_a), BAR(bass_snare_4_a), BAR(bass_snare_4_b2)},
        .auto_pickup = Y, .b_pickup = Y, .ticks = 96,
    },
    [TR66_FOXTROT_2] = {
        .bars = {BAR(foxtrot_2_a), BAR(foxtrot_2_a), BAR(foxtrot_2_a), BAR(foxtrot_2_b2)},
        .b_pickup = S | Y, .ticks = 96,
    },
    [TR66_SWING_2] = {
        .bars = {BAR(swing_2_a), BAR(swing_2_a), BAR(swing_2_a), BAR(swing_2_b2)},
        .ticks = 96,
    },
    [TR66_SWING_3] = {
        .bars = {BAR(swing_3_a), BAR(swing_3_a), BAR(swing_3_a), BAR(swing_3_b2)},
        .ticks = 96,
    },
    [TR66_SHUFFLE] = {
        .bars = {BAR(shuffle_a), BAR(shuffle_a), BAR(shuffle_a), BAR(shuffle_b2)},
        .ticks = 96,
    },
    [TR66_TANGO] = {
        .bars = {BAR(tango_a), BAR(tango_a), BAR(tango_a), BAR(tango_b2)},
        .auto_pickup = TANGO_SY, .b_pickup = TANGO_SY, .ticks = 96,
    },
};
