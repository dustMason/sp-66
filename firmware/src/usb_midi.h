#ifndef SP66_USB_MIDI_H
#define SP66_USB_MIDI_H

#include "sequencer.h"

int sp1_usb_start(void);
void sp1_midi_poll(Tr66Sequencer *seq, bool audio_ready);

#endif
