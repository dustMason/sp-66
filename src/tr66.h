#ifndef TR66_H
#define TR66_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum Tr66Voice {
    TR66_KICK, TR66_SNARE, TR66_HAT, TR66_CYMBAL,
    TR66_CLAVES, TR66_RIM, TR66_COWBELL, TR66_CONGA,
    TR66_HIGH_BONGO, TR66_LOW_BONGO, TR66_VOICE_COUNT,
    TR66_MARACAS = TR66_HAT /* The service notes label the same input M and HH. */
};

enum { TR66_TONES = 4, TR66_NOISE_BANDS = 2 };
enum {
    TR66_REDUCED_KICK = 1u << TR66_VOICE_COUNT,
    TR66_REDUCED_CONGA = TR66_REDUCED_KICK << 1,
    TR66_REDUCED_HIGH_BONGO = TR66_REDUCED_CONGA << 1,
    TR66_QUIET_HIGH_BONGO = TR66_REDUCED_HIGH_BONGO << 1,
    TR66_QUIET_KICK = TR66_QUIET_HIGH_BONGO << 1,
    TR66_GHOST_KICK = TR66_QUIET_KICK << 1,
};

static inline bool tr66_coincident_hat_cymbal(uint16_t voices)
{
    return (voices & (1u << TR66_HAT)) && (voices & (1u << TR66_CYMBAL));
}

typedef struct {
    float hz;
    float decay_ms; /* amplitude time constant */
    float cosine;
    float sine;
} Tr66Tone;

typedef struct {
    float hz;
    float q;
    float gain;
} Tr66Band;

typedef struct {
    Tr66Tone tone[TR66_TONES];
    Tr66Band band[TR66_NOISE_BANDS];
    float noise_attack_ms;
    float noise_decay_ms;
    float noise_highpass_hz, noise_fast_ms, noise_fast_mix;
    float noise_threshold; /* envelope level below which the gate closes */
    float noise_sweep_hz; /* initial downward offset of the first noise band */
    float noise_fast_hold_ms;
} Tr66Patch;

typedef struct {
    float real, imag;
    float rotation_real, rotation_imag;
} Tr66Resonator;

typedef struct {
    float z1, z2;
    float b0, b1, b2, a1, a2;
} Tr66Filter;

typedef struct {
    float decay_coeff, attack_coeff, threshold;
    unsigned tail_frames, fast_release_frame;
} Tr66NoiseGate;

typedef struct {
    Tr66Resonator resonator[TR66_TONES];
    Tr66Filter filter[TR66_NOISE_BANDS], highpass;
    float trigger_real[TR66_TONES], trigger_imag[TR66_TONES];
    Tr66NoiseGate gate;
    float noise, fast_noise, attack, fast_coeff, fast_mix;
    float first_band_sweep;
    unsigned frames_left;
    uint8_t tone_count, band_count;
} Tr66VoiceState;

typedef struct {
    Tr66VoiceState voices[TR66_VOICE_COUNT];
    float cymbal_first_band_sweep;
    unsigned cymbal_extra_hold_frames;
    uint32_t cymbal_age;
    uint32_t random;
    float sample_rate;
    float balance, balance_target, balance_coeff;
} Tr66;

extern const char *const tr66_voice_names[TR66_VOICE_COUNT];
extern const Tr66Patch tr66_factory_voices[TR66_VOICE_COUNT];
extern const float tr66_line_gain;

void tr66_init(Tr66 *machine, float sample_rate, uint32_t seed);
void tr66_hit(Tr66 *machine, uint16_t voices);
void tr66_balance(Tr66 *machine, float position); /* -1 bass, 0 center, +1 treble */
void tr66_render(Tr66 *machine, float *output, size_t frames);

/* The host calibration tool calls the same voice code as the firmware mixer. */
void tr66_render_hit(const Tr66Patch *patch, float sample_rate, uint32_t seed,
                     float *output, size_t frames);

#endif
