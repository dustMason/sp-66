#ifndef TR66_CODECS_H
#define TR66_CODECS_H

#include <stdbool.h>

void sp1_feed_watchdog(void);
int sp1_codecs_power_on(void);
int sp1_codecs_start(void);
void sp1_codecs_power_off(void);
void sp1_codecs_poll_jack(void);

#endif
