#include "tr66.h"

#include <math.h>
#include <string.h>

#define TAU 6.2831853071795864769f

/* Leave room for overlapping voices with Balance at either end. */
const float tr66_line_gain = 0.18f;
static const float coincident_cymbal_hold_ms = 44.0f;
static const float coincident_cymbal_hat_recovery_ms = 554.0f;

const char *const tr66_voice_names[TR66_VOICE_COUNT] = {
    "kick", "snare", "hat", "cymbal", "claves", "rim",
    "cowbell", "conga", "high-bongo", "low-bongo",
};

const Tr66Patch tr66_factory_voices[TR66_VOICE_COUNT] = {
#include "voice_patches.inc"
};

static float decay(float sample_rate, float milliseconds)
{
    return milliseconds > 0.0f ? expf(-1000.0f / (sample_rate * milliseconds)) : 0.0f;
}

static void voice_init(Tr66VoiceState *state, const Tr66Patch *patch, float rate)
{
    memset(state, 0, sizeof(*state));
    const float noise_ms = fmaxf(patch->noise_decay_ms, patch->noise_fast_ms);
    float tail_ms = 9.0f * noise_ms;
    state->gate.threshold = fminf(1.0f, fmaxf(0.0f, patch->noise_threshold));
    state->gate.curve = patch->noise_gate_curve;
    if (state->gate.threshold > 0.0f)
        tail_ms = fminf(tail_ms, -noise_ms * logf(state->gate.threshold));
    const float hold_ms = fmaxf(0.0f, patch->noise_fast_hold_ms);
    tail_ms += hold_ms;
    for (unsigned i = 0; i < TR66_TONES; ++i) {
        const Tr66Tone *tone = &patch->tone[i];
        if (tone->decay_ms <= 0.0f)
            continue;
        const unsigned j = state->tone_count++;
        const float phase = TAU * tone->hz / rate;
        const float radius = decay(rate, tone->decay_ms);
        state->resonator[j].rotation_real = radius * cosf(phase);
        state->resonator[j].rotation_imag = radius * sinf(phase);
        state->trigger_real[j] = tone->cosine;
        state->trigger_imag[j] = -tone->sine;
        tail_ms = fmaxf(tail_ms, 9.0f * tone->decay_ms);
    }
    for (unsigned i = 0; i < TR66_NOISE_BANDS; ++i) {
        const Tr66Band *band = &patch->band[i];
        if (band->gain == 0.0f)
            continue;
        const float phase = TAU * band->hz / rate;
        const float alpha = sinf(phase) / (2.0f * band->q);
        const float normalize = 1.0f / (1.0f + alpha);
        const unsigned j = state->band_count++;
        state->filter[j] = (Tr66Filter) {
            .b0 = band->gain * alpha * normalize,
            .b2 = -band->gain * alpha * normalize,
            .a1 = -2.0f * cosf(phase) * normalize,
            .a2 = (1.0f - alpha) * normalize,
        };
        if (j == 0 && (patch->noise_sweep_hz > 0.0f || patch->noise_start_q > 0.0f)) {
            const Tr66Filter *filter = &state->filter[0];
            Tr66NoiseSweep *sweep = &state->sweep;
            const float start_phase = TAU * (band->hz - patch->noise_sweep_hz) / rate;
            const float start_a1 = -(1.0f + filter->a2) * cosf(start_phase);
            sweep->bend_decay = decay(rate, patch->noise_sweep_ms);
            if (sweep->bend_decay > 0.0f)
                sweep->bend_start = start_a1 - filter->a1;
            else if (patch->noise_sweep_hz > 0.0f && patch->noise_fast_mix > 0.0f)
                sweep->bend_start = (start_a1 - filter->a1) / patch->noise_fast_mix;

            if (patch->noise_start_q > 0.0f && patch->noise_damping_ms > 0.0f) {
                const float start_alpha = sinf(start_phase) / (2.0f * patch->noise_start_q);
                sweep->damping_start = filter->a2 - (1.0f - start_alpha) / (1.0f + start_alpha);
                sweep->damping_decay = decay(rate, patch->noise_damping_ms);
                sweep->phase_per_damping = 1.0f / (1.0f + filter->a2);
                /* Partially compensate for the extra power of a wider band. */
                sweep->gain_per_damping = 0.31f * band->gain;
            }
        }
    }
    if (patch->noise_highpass_hz > 0.0f) {
        const float phase = TAU * patch->noise_highpass_hz / rate;
        const float alpha = 0.7071067812f * sinf(phase);
        const float c = cosf(phase);
        const float normalize = 1.0f / (1.0f + alpha);
        const float b0 = (1.0f + c) * 0.5f * normalize;
        state->highpass = (Tr66Filter) {
            .b0 = b0, .b1 = -2.0f * b0, .b2 = b0,
            .a1 = -2.0f * c * normalize, .a2 = (1.0f - alpha) * normalize,
        };
    }
    state->gate.decay_coeff = decay(rate, patch->noise_decay_ms);
    state->fast_coeff = decay(rate, patch->noise_fast_ms);
    state->fast_mix = patch->noise_fast_mix;
    state->gate.attack_coeff = decay(rate, patch->noise_attack_ms);
    /* Ungated tails end below -78 dB in amplitude. */
    state->gate.tail_frames = (unsigned)ceilf(0.001f * tail_ms * rate);
    state->gate.fast_release_frame = state->gate.tail_frames - (unsigned)ceilf(0.001f * hold_ms * rate);
}

static void add_mode_drive(Tr66VoiceState *state, unsigned mode, float strength)
{
    state->resonator[mode].real += strength * state->trigger_real[mode];
    state->resonator[mode].imag += strength * state->trigger_imag[mode];
}

static void voice_trigger(Tr66VoiceState *state, float strength)
{
    for (unsigned i = 0; i < state->tone_count; ++i)
        add_mode_drive(state, i, strength);
    state->sweep.bend = state->sweep.bend_start;
    state->sweep.damping = state->sweep.damping_start;
    state->noise = 1.0f - state->fast_mix;
    state->fast_noise = state->fast_mix;
    state->attack = state->gate.attack_coeff != 0.0f ? 1.0f : 0.0f;
    state->frames_left = state->gate.tail_frames;
}

static float white_noise(uint32_t *seed)
{
    uint32_t bits = *seed;
    bits ^= bits << 13;
    bits ^= bits >> 17;
    bits ^= bits << 5;
    *seed = bits;
    return (float)(bits >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

static float filter_sample(Tr66Filter *filter, float input, float a1)
{
    const float out = filter->b0 * input + filter->z1;
    filter->z1 = filter->b1 * input + filter->z2 - a1 * out;
    filter->z2 = filter->b2 * input - filter->a2 * out;
    return out;
}

static float first_band_sample(Tr66VoiceState *state, float input)
{
    Tr66Filter *band = &state->filter[0];
    Tr66NoiseSweep *sweep = &state->sweep;
    const float bend = sweep->bend_decay > 0.0f
        ? sweep->bend : sweep->bend_start * state->fast_noise;
    sweep->bend *= sweep->bend_decay;
    if (sweep->damping == 0.0f)
        return filter_sample(band, input, band->a1 + bend);

    /* The transient broadens the band while its center glides upward.
     * Scale a1 with the new a2 to keep damping from shifting the center. */
    const float damping = sweep->damping;
    const float a2 = band->a2 - damping;
    const float a1 = (band->a1 + bend) * (1.0f - sweep->phase_per_damping * damping);
    const float b0 = band->b0 + sweep->gain_per_damping * damping;
    const float feed = b0 * input;
    const float out = feed + band->z1;
    band->z1 = band->z2 - a1 * out;
    band->z2 = -feed - a2 * out;
    sweep->damping *= sweep->damping_decay;
    return out;
}

static float voice_sample(Tr66VoiceState *state, float noise)
{
    if (state->frames_left == 0)
        return 0.0f;
    --state->frames_left;

    float out = 0.0f;
    for (unsigned i = 0; i < state->tone_count; ++i) {
        Tr66Resonator *mode = &state->resonator[i];
        const float real = mode->real;
        const float imag = mode->imag;
        out += real;
        mode->real = real * mode->rotation_real - imag * mode->rotation_imag;
        mode->imag = imag * mode->rotation_real + real * mode->rotation_imag;
    }
    if (state->band_count != 0) {
        if (state->highpass.b0 != 0.0f)
            noise = filter_sample(&state->highpass, noise, state->highpass.a1);
        float filtered = first_band_sample(state, noise);
        for (unsigned i = 1; i < state->band_count; ++i) {
            Tr66Filter *band = &state->filter[i];
            filtered += filter_sample(band, noise, band->a1);
        }
        const float envelope = (state->noise + state->fast_noise) * (1.0f - state->attack);
        if (envelope > state->gate.threshold) {
            const float level = envelope - state->gate.threshold;
            /* Round the closed-hat attack and bring up its body. The
             * correction approaches zero as the gate closes. */
            const float gain = 1.0f + state->gate.curve * level * (0.333f - level);
            out += filtered * (level * gain);
        }
        state->noise *= state->gate.decay_coeff;
        if (state->frames_left < state->gate.fast_release_frame)
            state->fast_noise *= state->fast_coeff;
        state->attack *= state->gate.attack_coeff;
    }
    if (state->frames_left == 0) {
        for (unsigned i = 0; i < state->tone_count; ++i) {
            state->resonator[i].real = 0.0f;
            state->resonator[i].imag = 0.0f;
        }
        state->highpass.z1 = 0.0f;
        state->highpass.z2 = 0.0f;
        for (unsigned i = 0; i < state->band_count; ++i) {
            state->filter[i].z1 = 0.0f;
            state->filter[i].z2 = 0.0f;
        }
    }
    return out;
}

void tr66_init(Tr66 *machine, float sample_rate, uint32_t seed)
{
    machine->random = seed != 0 ? seed : 1;
    machine->balance = (Tr66Balance){1.0f, 1.0f};
    machine->balance_target = machine->balance;
    machine->balance_coeff = 1.0f - decay(sample_rate, 8.0f);
    machine->sample_rate = sample_rate;
    machine->cymbal_age = UINT32_MAX;
    machine->cymbal_hat_recovery_ms = coincident_cymbal_hat_recovery_ms;
    for (unsigned i = 0; i < TR66_VOICE_COUNT; ++i)
        voice_init(&machine->voices[i], &tr66_factory_voices[i], sample_rate);

    const Tr66VoiceState *cymbal = &machine->voices[TR66_CYMBAL];
    machine->cymbal_fast_release_frame = cymbal->gate.fast_release_frame;
    machine->cymbal_sweep_decay = cymbal->sweep.bend_decay;
    machine->coincident_cymbal_sweep_decay = decay(sample_rate, 34.0f);
    const float extra_hold_ms = coincident_cymbal_hold_ms - tr66_factory_voices[TR66_CYMBAL].noise_fast_hold_ms;
    machine->cymbal_extra_hold_frames = (unsigned)ceilf(0.001f * fmaxf(0.0f, extra_hold_ms) * sample_rate);
}

static void trigger_hat(Tr66 *machine)
{
    const Tr66Patch *patch = &tr66_factory_voices[TR66_HAT];
    Tr66VoiceState *hat = &machine->voices[TR66_HAT];
    const float samples_per_ms = 0.001f * machine->sample_rate;
    /* Both trigger circuits feed the same transistor. Cymbal history lowers
     * the effective cutoff of later hats, even after its audible tail. */
    const float recovery_frames = machine->cymbal_hat_recovery_ms * samples_per_ms;
    const float bias = 2.1f * expf(-(float)machine->cymbal_age / recovery_frames);
    const float threshold = patch->noise_threshold * expf(-bias);
    const float level_at_14ms = (1.0f - expf(-14.0f / patch->noise_attack_ms)) *
                              expf(-14.0f / patch->noise_decay_ms);
    const float drive = (level_at_14ms - patch->noise_threshold) / (level_at_14ms - threshold);
    const float tail_ms = patch->noise_decay_ms * (bias - logf(patch->noise_threshold));
    hat->gate.threshold = drive * threshold;
    /* When the cymbal has biased the gate open, the slower tail already
     * carries more energy. Scale the closed-hat curve with its drive. */
    hat->gate.curve = patch->noise_gate_curve * drive * drive;
    hat->gate.tail_frames = (unsigned)ceilf(samples_per_ms * tail_ms);
    hat->gate.fast_release_frame = hat->gate.tail_frames;
    voice_trigger(hat, 1.0f);
    hat->noise *= drive;
    /* Cymbal bias deepens and slows the hat's return to the resting resonance. */
    const float sweep_ms = patch->noise_sweep_ms * (1.0f + 0.4f * bias);
    hat->sweep.bend_decay = decay(machine->sample_rate, sweep_ms);
    hat->sweep.bend *= 1.0f + 0.02f * bias;
}

static void trigger_cymbal(Tr66 *machine, bool shared_hit)
{
    Tr66VoiceState *cymbal = &machine->voices[TR66_CYMBAL];
    cymbal->gate.fast_release_frame = machine->cymbal_fast_release_frame;
    cymbal->sweep.bend_decay = shared_hit
        ? machine->coincident_cymbal_sweep_decay : machine->cymbal_sweep_decay;
    voice_trigger(cymbal, 1.0f);

    if (shared_hit) {
        /* The coincident hit puts more of the gate drive into the slow
         * decay and starts nearer the resting resonance. */
        cymbal->frames_left += machine->cymbal_extra_hold_frames;
        cymbal->noise = 0.75075f;
        cymbal->fast_noise = 0.29925f;
        cymbal->sweep.bend *= 0.82f;
        cymbal->sweep.damping *= 1.25f;
    } else {
        /* In the recordings, a cymbal after a longer gap has a stronger
         * attack relative to its body. Shift the gate drive toward the
         * fast envelope and release it earlier as the gap grows. */
        const float samples_per_ms = 0.001f * machine->sample_rate;
        const float age_ms = (float)machine->cymbal_age / samples_per_ms;
        const float recovered = fminf(1.0f, fmaxf(0.0f, (age_ms - 190.0f) / 480.0f));
        cymbal->noise -= 0.025f * recovered;
        cymbal->fast_noise += 0.08f * recovered;
        cymbal->gate.fast_release_frame += (unsigned)(8.0f * samples_per_ms * recovered);
    }
    /* Later hats close sooner after CY alone than after both gate inputs. */
    machine->cymbal_hat_recovery_ms = shared_hit ? coincident_cymbal_hat_recovery_ms : 490.0f;
    machine->cymbal_age = 0;
}

typedef struct { float real, imag; } Tr66ToneDrive;

/* Figure 13 scores a combined bass-drum and low-bongo trigger.
 * Drives follow the long ring, pulse and short ring in the voice patch. */
static const Tr66ToneDrive rhumba_bongo_drive[TR66_TONES] = {
    {.real = -0.199725f, .imag = 0.188981f},
    {.real = 0.165581f},
    {.real = -0.026078f, .imag = 0.079157f},
};

/* The reduced kick shifts the phase of the two rings and softens the
 * attack. The fundamental has half the amplitude of the full drive. */
static const Tr66ToneDrive reduced_kick_drive[TR66_TONES] = {
    {.real = -0.71158f, .imag = 0.24133f},
    {.real = 0.26275f},
    {.real = 1.14484f},
    {.real = -0.67813f},
    {.real = -0.01788f, .imag = 0.00560f},
};

static void trigger_tones(Tr66VoiceState *voice, const Tr66ToneDrive drive[TR66_TONES])
{
    for (unsigned i = 0; i < voice->tone_count; ++i) {
        voice->resonator[i].real += drive[i].real;
        voice->resonator[i].imag += drive[i].imag;
    }
    voice->frames_left = voice->gate.tail_frames;
}

static void trigger_high_bongo(Tr66VoiceState *bongo, float ring, float attack)
{
    voice_trigger(bongo, ring);
    /* The alternate triggers drive the short 600 Hz mode separately. */
    if (bongo->tone_count > 1)
        add_mode_drive(bongo, 1, attack - ring);
}

void tr66_hit(Tr66 *machine, Tr66TriggerMask voices)
{
    const bool bare_kick_snare = voices == ((1u << TR66_KICK) | (1u << TR66_SNARE));
    if (voices & (1u << TR66_SNARE)) {
        /* The SD trigger also reaches LB through a diode and resistor
         * (Service Notes Figure 1). It weakly drives the same bongo ring. */
        Tr66VoiceState *bongo = &machine->voices[TR66_LOW_BONGO];
        Tr66Resonator *ring = &bongo->resonator[0];
        const float strength = bare_kick_snare ? 0.85f : 1.0f;
        const float real = bongo->trigger_real[0];
        const float imag = bongo->trigger_imag[0];
        /* Attenuation and phase shift relative to the direct bongo drive. */
        const float coupling_real = 0.4696063f;
        const float coupling_imag = -0.06587713f;
        ring->real += strength * (coupling_real * real - coupling_imag * imag);
        ring->imag += strength * (coupling_imag * real + coupling_real * imag);
        bongo->frames_left = bongo->gate.tail_frames;
    }
    if (bare_kick_snare) {
        /* The two main rings are quieter on bare kick/snare hits in both
         * recording sets. Keep the snare noise gate at its normal drive. */
        voice_trigger(&machine->voices[TR66_KICK], 0.85f);
        voice_trigger(&machine->voices[TR66_SNARE], 0.85f);
        return;
    }

    /* The hat and cymbal triggers feed the same resonator. */
    const bool shared_hit = tr66_coincident_hat_cymbal(voices);
    if (shared_hit)
        voices &= ~(1u << TR66_HAT);

    if (voices & (1u << TR66_HAT)) {
        trigger_hat(machine);
        voices &= ~(1u << TR66_HAT);
    }
    if (voices & (1u << TR66_CYMBAL)) {
        trigger_cymbal(machine, shared_hit);
        voices &= ~(1u << TR66_CYMBAL);
    }
    if (voices & (TR66_RHUMBA_BONGO | TR66_RHUMBA_FILL_BONGO)) {
        Tr66VoiceState *bongo = &machine->voices[TR66_LOW_BONGO];
        if (voices & TR66_RHUMBA_BONGO)
            trigger_tones(bongo, rhumba_bongo_drive);
        else
            voice_trigger(bongo, -1.0f);
        voices &= ~(1u << TR66_LOW_BONGO);
    }
    if (voices & TR66_MAMBO_FILL_HIGH_BONGO) {
        /* The extra Mambo B hits mostly excite the long ring. */
        trigger_high_bongo(&machine->voices[TR66_HIGH_BONGO], 1.58f, 0.07f);
        voices &= ~(1u << TR66_HIGH_BONGO);
    }
    for (unsigned i = 0; i < TR66_VOICE_COUNT; ++i)
        if (voices & (1u << i))
            voice_trigger(&machine->voices[i], 1.0f);
    /* Measured against full hits in the recordings. The louder route wins
     * if layered rhythms trigger the same voice on the same clock pulse. */
    if (!(voices & (1u << TR66_KICK))) {
        if (voices & TR66_REDUCED_KICK)
            trigger_tones(&machine->voices[TR66_KICK], reduced_kick_drive);
        else if (voices & TR66_QUIET_KICK)
            voice_trigger(&machine->voices[TR66_KICK], 0.43f);
        else if (voices & TR66_GHOST_KICK)
            voice_trigger(&machine->voices[TR66_KICK], 0.21f);
    }
    if (!(voices & (1u << TR66_CONGA))) {
        if (voices & TR66_CHA_CHA_CONGA)
            voice_trigger(&machine->voices[TR66_CONGA], 0.71f);
        else if (voices & TR66_REDUCED_CONGA)
            voice_trigger(&machine->voices[TR66_CONGA], 0.66f);
    }
    if (!(voices & ((1u << TR66_HIGH_BONGO) | TR66_MAMBO_FILL_HIGH_BONGO))) {
        Tr66VoiceState *bongo = &machine->voices[TR66_HIGH_BONGO];
        if (voices & TR66_ACCENT_HIGH_BONGO)
            trigger_high_bongo(bongo, 0.66f, 0.25f);
        else if (voices & TR66_REDUCED_HIGH_BONGO)
            voice_trigger(bongo, 0.53f);
        else if (voices & TR66_LIGHT_HIGH_BONGO)
            trigger_high_bongo(bongo, 0.45f, 0.62f);
        else if (voices & TR66_QUIET_HIGH_BONGO)
            trigger_high_bongo(bongo, 0.204f, 0.11f);
    }
}

Tr66Balance tr66_balance_gains(float position)
{
    position = fmaxf(-1.0f, fminf(1.0f, position));
    const float kick_shunt = 1.0f - position;
    const float treble_shunt = 1.0f + position;
    /* The grounded wiper on VR602 (50 kΩ) shunts each path to ground.
     * Estimate the Thevenin impedances near 65 Hz and 8.17 kHz from
     * Figures 1 and 10, in units of 25 kΩ. Gains are unity at center. */
    const float kick_resistance = 36.5f / 25.0f;
    const float treble_resistance = 7.28f / 25.0f;
    const float treble_reactance = 39.86f / 25.0f;
    const float center = 1.0f + treble_resistance;
    const float at_position = treble_shunt + treble_resistance;
    return (Tr66Balance) {
        .kick = kick_shunt * (1.0f + kick_resistance) / (kick_shunt + kick_resistance),
        .treble = treble_shunt * sqrtf((center * center + treble_reactance * treble_reactance) /
                                      (at_position * at_position + treble_reactance * treble_reactance)),
    };
}

void tr66_balance(Tr66 *machine, float position)
{
    machine->balance_target = tr66_balance_gains(position);
}

void tr66_render(Tr66 *machine, float *output, size_t frames)
{
    const uint32_t age_remaining = UINT32_MAX - machine->cymbal_age;
    machine->cymbal_age += frames < age_remaining ? (uint32_t)frames : age_remaining;
    for (size_t frame = 0; frame < frames; ++frame) {
        machine->balance.kick += machine->balance_coeff *
                                 (machine->balance_target.kick - machine->balance.kick);
        machine->balance.treble += machine->balance_coeff *
                                   (machine->balance_target.treble - machine->balance.treble);
        const float noise = white_noise(&machine->random);
        float sample = machine->balance.kick * voice_sample(&machine->voices[TR66_KICK], noise);
        sample += voice_sample(&machine->voices[TR66_SNARE], noise);
        sample += machine->balance.treble * (voice_sample(&machine->voices[TR66_HAT], noise) +
                            voice_sample(&machine->voices[TR66_CYMBAL], noise));
        for (unsigned i = TR66_CLAVES; i < TR66_VOICE_COUNT; ++i)
            sample += voice_sample(&machine->voices[i], noise);
        output[frame] = sample;
    }
}

void tr66_render_hit(const Tr66Patch *patch, float sample_rate, uint32_t seed,
                     float *output, size_t frames)
{
    Tr66VoiceState state;
    voice_init(&state, patch, sample_rate);
    voice_trigger(&state, 1.0f);
    uint32_t random = seed != 0 ? seed : 1;
    for (size_t frame = 0; frame < frames; ++frame)
        output[frame] = voice_sample(&state, white_noise(&random));
}
