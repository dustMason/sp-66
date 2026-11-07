/* Host calibration only. Sum unity-gain stems to reproduce the C mono mix. */
#include "tr66.h"

void tr66_render_stem(unsigned voice, const uint32_t *positions, const Tr66TriggerMask *masks,
                      size_t events, float *samples, size_t frames)
{
    Tr66 synth;
    tr66_init(&synth, 48000, UINT32_C(14293));
    /* Keep the full trigger history when isolating the sound of one voice. */
    for (unsigned i = 0; i < TR66_VOICE_COUNT; ++i) {
        if (i != voice) {
            synth.voices[i].tone_count = 0;
            synth.voices[i].band_count = 0;
        }
    }
    size_t cursor = 0;
    for (size_t i = 0; i < events && positions[i] < frames; ++i) {
        const size_t next = positions[i];
        tr66_render(&synth, samples + cursor, next - cursor);
        tr66_hit(&synth, masks[i]);
        cursor = next;
    }
    tr66_render(&synth, samples + cursor, frames - cursor);
}
