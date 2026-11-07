#ifndef TR66_MIDI_H
#define TR66_MIDI_H

#include <stdint.h>

enum Tr66MidiStatus {
    TR66_MIDI_NONE = 0,
    TR66_MIDI_POSITION = 0xf2,
    TR66_MIDI_CLOCK = 0xf8,
    TR66_MIDI_START = 0xfa,
    TR66_MIDI_CONTINUE = 0xfb,
    TR66_MIDI_STOP = 0xfc,
};

typedef struct {
    uint8_t status;
    uint16_t position; /* Sixteenth notes, for Song Position Pointer. */
} Tr66MidiEvent;

Tr66MidiEvent tr66_midi_usb_event(const uint8_t packet[4]);

#endif
