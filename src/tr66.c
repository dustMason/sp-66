#include "tr66.h"

#include <math.h>
#include <string.h>

#define TAU 6.2831853071795864769f

/* Leave room for overlapping voices with Balance at either end. */
const float tr66_line_gain = 0.18f;
static const float coincident_cymbal_hold_ms = 44.0f;

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
        if (j == 0 && patch->noise_sweep_hz > 0.0f && patch->noise_fast_mix > 0.0f) {
            const Tr66Filter *filter = &state->filter[j];
            const float start_phase = TAU * (band->hz - patch->noise_sweep_hz) / rate;
            const float start_a1 = -(1.0f + filter->a2) * cosf(start_phase);
            state->first_band_sweep = (start_a1 - filter->a1) / patch->noise_fast_mix;
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

static void voice_trigger(Tr66VoiceState *state, float strength)
{
    for (unsigned i = 0; i < state->tone_count; ++i) {
        state->resonator[i].real += strength * state->trigger_real[i];
        state->resonator[i].imag += strength * state->trigger_imag[i];
    }
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
        Tr66Filter *first = &state->filter[0];
        /* The cymbal resonance rises as the fast part of its gate decays. */
        const float first_a1 = first->a1 + state->first_band_sweep * state->fast_noise;
        float filtered = filter_sample(first, noise, first_a1);
        for (unsigned i = 1; i < state->band_count; ++i) {
            Tr66Filter *band = &state->filter[i];
            filtered += filter_sample(band, noise, band->a1);
        }
        const float envelope = (state->noise + state->fast_noise) * (1.0f - state->attack);
        if (envelope > state->gate.threshold)
            out += filtered * (envelope - state->gate.threshold);
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
    for (unsigned i = 0; i < TR66_VOICE_COUNT; ++i)
        voice_init(&machine->voices[i], &tr66_factory_voices[i], sample_rate);

    machine->cymbal_first_band_sweep = machine->voices[TR66_CYMBAL].first_band_sweep;
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
    const float bias = 2.1f * expf(-(float)machine->cymbal_age / (554.0f * samples_per_ms));
    const float threshold = patch->noise_threshold * expf(-bias);
    const float level_at_14ms = (1.0f - expf(-14.0f / patch->noise_attack_ms)) *
                              expf(-14.0f / patch->noise_decay_ms);
    const float drive = (level_at_14ms - patch->noise_threshold) / (level_at_14ms - threshold);
    const float tail_ms = patch->noise_decay_ms * (bias - logf(patch->noise_threshold));
    hat->gate.threshold = drive * threshold;
    hat->gate.tail_frames = (unsigned)ceilf(samples_per_ms * tail_ms);
    hat->gate.fast_release_frame = hat->gate.tail_frames;
    voice_trigger(hat, 1.0f);
    hat->noise *= drive;
}

void tr66_hit(Tr66 *machine, uint16_t voices)
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
        const float coupling_real = -0.4696063f;
        const float coupling_imag = 0.06587713f;
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
        voices &= (uint16_t)~(1u << TR66_HAT);

    if (voices & (1u << TR66_HAT)) {
        trigger_hat(machine);
        voices &= (uint16_t)~(1u << TR66_HAT);
    }
    Tr66VoiceState *cymbal = &machine->voices[TR66_CYMBAL];
    if (voices & (1u << TR66_CYMBAL)) {
        machine->cymbal_age = 0;
        cymbal->first_band_sweep = machine->cymbal_first_band_sweep;
    }
    for (unsigned i = 0; i < TR66_VOICE_COUNT; ++i)
        if (voices & (UINT16_C(1) << i))
            voice_trigger(&machine->voices[i], 1.0f);
    if (shared_hit) {
        /* The coincident hit puts more of the gate drive into the slow
         * decay and starts nearer the resting resonance. */
        cymbal->frames_left += machine->cymbal_extra_hold_frames;
        cymbal->noise = 0.75075f;
        cymbal->fast_noise = 0.29925f;
        cymbal->first_band_sweep *= 0.82f * cymbal->fast_mix / cymbal->fast_noise;
    }
    /* Measured against full hits in the recordings. The louder route wins
     * if layered rhythms trigger the same voice on the same clock pulse. */
    if (!(voices & (1u << TR66_KICK))) {
        if (voices & TR66_REDUCED_KICK)
            voice_trigger(&machine->voices[TR66_KICK], 0.5f);
        else if (voices & TR66_QUIET_KICK)
            voice_trigger(&machine->voices[TR66_KICK], 0.43f);
        else if (voices & TR66_GHOST_KICK)
            voice_trigger(&machine->voices[TR66_KICK], 0.19f);
    }
    if ((voices & TR66_REDUCED_CONGA) && !(voices & (1u << TR66_CONGA)))
        voice_trigger(&machine->voices[TR66_CONGA], 0.66f);
    if (!(voices & (1u << TR66_HIGH_BONGO))) {
        if (voices & TR66_REDUCED_HIGH_BONGO)
            voice_trigger(&machine->voices[TR66_HIGH_BONGO], 0.53f);
        else if (voices & TR66_QUIET_HIGH_BONGO)
            voice_trigger(&machine->voices[TR66_HIGH_BONGO], 0.20f);
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
