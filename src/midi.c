#include "midi.h"

Tr66MidiEvent tr66_midi_usb_event(const uint8_t packet[4])
{
    /* The port exposes one virtual cable, cable 0. */
    if (packet[0] == 0x0f) {
        switch (packet[1]) {
        case TR66_MIDI_CLOCK:
        case TR66_MIDI_START:
        case TR66_MIDI_CONTINUE:
        case TR66_MIDI_STOP:
            return (Tr66MidiEvent){.status = packet[1]};
        }
    }
    if (packet[0] == 0x03 && packet[1] == TR66_MIDI_POSITION &&
        ((packet[2] | packet[3]) & 0x80) == 0) {
        return (Tr66MidiEvent){
            .status = TR66_MIDI_POSITION,
            .position = (uint16_t)(packet[2] | (packet[3] << 7)),
        };
    }
    return (Tr66MidiEvent){0};
}
