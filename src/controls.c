#include "controls.h"

enum { FADER_STEPS = 4096, FADER_HYSTERESIS = 42, POWER_SCANS = 180, VOLUME_STEPS = 16 };

static enum Tr66ControlKey press(Tr66KeyDebounce *key, enum Tr66ControlKey input)
{
    if (input != key->candidate) {
        key->candidate = input;
        key->consecutive = 0;
    }
    if (key->consecutive < 3 && ++key->consecutive == 3 && key->stable != input) {
        bool released = key->stable == TR66_KEY_NONE;
        key->stable = input;
        if (released)
            return input;
    }
    return TR66_KEY_NONE;
}

static unsigned slot(unsigned raw, unsigned slots, unsigned old, bool initialized)
{
    unsigned next = raw * slots / FADER_STEPS;
    if (initialized) {
        if (next > old && raw < ((old + 1) * FADER_STEPS) / slots + FADER_HYSTERESIS)
            return old;
        if (next < old && raw + FADER_HYSTERESIS >= (old * FADER_STEPS) / slots)
            return old;
    }
    return next < slots ? next : slots - 1;
}

static bool available(enum Tr66PatternId id)
{
    return id == TR66_OFF || tr66_patterns[id].ticks != 0;
}

void tr66_controls_init(Tr66Controls *controls)
{
    *controls = (Tr66Controls){.request.arrange = TR66_ARRANGE_AUTO, .volume = 7};
}

bool tr66_controls_scan(Tr66Controls *controls, Tr66Sequencer *seq, Tr66Input input)
{
    tr66_seq_tempo(seq, 50000 + 150000u * input.fader[0] / (FADER_STEPS - 1));
    tr66_balance(&seq->synth, 2.0f * input.fader[1] / (FADER_STEPS - 1) - 1.0f);
    controls->rhythm_slot = slot(input.fader[2], TR66_RHYTHM_COUNT + 1,
                                 controls->rhythm_slot, controls->initialized);
    controls->variation_slot = slot(input.fader[3], TR66_VARIATION_COUNT / 3,
                                    controls->variation_slot, controls->initialized);
    controls->initialized = true;

    enum Tr66ControlKey track = press(&controls->track, input.track);
    if (track >= TR66_KEY_T1 && track <= TR66_KEY_T3)
        controls->bank = track - TR66_KEY_T1;
    if (track == TR66_KEY_T4)
        controls->variation_enabled = !controls->variation_enabled;

    switch (press(&controls->side, input.side)) {
    case TR66_KEY_A:
        if (controls->request.arrange > TR66_ARRANGE_A)
            --controls->request.arrange;
        break;
    case TR66_KEY_B:
        if (controls->request.arrange < TR66_ARRANGE_B)
            ++controls->request.arrange;
        break;
    case TR66_KEY_DOWN:
        if (controls->volume > 0)
            --controls->volume;
        break;
    case TR66_KEY_UP:
        if (controls->volume < VOLUME_STEPS - 1)
            ++controls->volume;
        break;
    default:
        break;
    }
    controls->request.rhythm = (enum Tr66PatternId)controls->rhythm_slot;
    controls->request.variation = TR66_OFF;
    if (controls->variation_enabled)
        controls->request.variation = TR66_ROCK_1 + 6 * controls->bank + controls->variation_slot;

    Tr66Selection playable = controls->request;
    bool rhythm_ready = available(playable.rhythm);
    bool variation_ready = available(playable.variation);
    controls->unavailable = !(rhythm_ready && variation_ready);
    if (!rhythm_ready)
        playable.rhythm = TR66_OFF;
    if (!variation_ready)
        playable.variation = TR66_OFF;
    tr66_seq_select(seq, playable);
    if (track == TR66_KEY_PLAY) {
        if (seq->running)
            tr66_seq_stop(seq);
        else
            tr66_seq_start(seq);
    }

    if (input.fn)
        return ++controls->fn_scans >= POWER_SCANS;
    if (controls->fn_scans >= 3 && controls->fn_scans < POWER_SCANS)
        tr66_seq_fill(seq);
    controls->fn_scans = 0;
    return false;
}
