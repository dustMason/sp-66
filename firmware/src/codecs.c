/* SP-1 codec sequences from Wakes and sp1-tape-looper (MIT).
 * See firmware/NOTICE for the original pin and register sources. */
#include "codecs.h"

#include <soc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define TAS2505_ADDR 0x18u
#define CS42L42_ADDR 0x48u
#define SPEAKER_TRIM_HALF_DB (-18)

static const struct device *const i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static bool speaker_on;
static bool last_wanted;
static unsigned stable_reads;

void sp1_feed_watchdog(void)
{
    /* The bootloader may have armed channels before entering the app. */
    for (unsigned ch = 0; ch < 8; ++ch)
        NRF_WDT->RR[ch] = WDT_RR_RR_Reload;
}

static int tas_wr(uint8_t reg, uint8_t val)
{
	sp1_feed_watchdog();
	return i2c_reg_write_byte(i2c_bus, TAS2505_ADDR, reg, val);
}

#define TAS_W(reg, val) do { if (tas_wr((reg), (val)) != 0) { return false; } } while (0)

static bool tas2505_configure(void)
{

	TAS_W(0x00, 0x00);          /* page 0 */
	TAS_W(0x01, 0x01);          /* software reset */
	k_msleep(5);

	TAS_W(0x00, 0x01);          /* page 1 */
	TAS_W(0x02, 0x00);          /* LDO 1.8 V, level shifters up */

	TAS_W(0x00, 0x00);          /* page 0: clocking + interface */
	TAS_W(0x04, 0x07);          /* PLL_CLKIN = BCLK, CODEC_CLKIN = PLL */
	TAS_W(0x05, 0x91);          /* PLL powered, P=1, R=1 */
	TAS_W(0x06, 0x20);          /* PLL J = 32 -> 98.304 MHz */
	TAS_W(0x07, 0x00);          /* PLL D MSB */
	TAS_W(0x08, 0x00);          /* PLL D LSB */
	k_msleep(15);                      /* PLL lock */
	TAS_W(0x0B, 0x82);          /* NDAC = 2, powered */
	TAS_W(0x0C, 0x88);          /* MDAC = 8, powered */
	TAS_W(0x0D, 0x00);          /* DOSR MSB */
	TAS_W(0x0E, 0x80);          /* DOSR = 128 -> 48 000 Hz */
	TAS_W(0x1B, 0x00);          /* I2S, 16-bit, slave */
	TAS_W(0x1C, 0x00);          /* data slot offset 0 */
	TAS_W(0x3C, 0x02);          /* DAC processing block PRB_P2 (mono) */
	TAS_W(0x3F, 0x90);          /* DAC powered, left -> left, soft-step */
	TAS_W(0x41, (uint8_t)(int8_t)SPEAKER_TRIM_HALF_DB);  /* speaker-only trim */
	TAS_W(0x40, 0x04);          /* DAC unmuted */

	TAS_W(0x00, 0x01);          /* page 1: analog, routing, driver */
	TAS_W(0x01, 0x10);          /* master analog reference on */
	TAS_W(0x0A, 0x00);          /* output common mode 0.9 V */
	TAS_W(0x0C, 0x04);          /* DAC -> output mixer */
	TAS_W(0x16, 0x00);          /* HP volume 0 dB */
	TAS_W(0x18, 0x00);          /* AINL volume / mixer */
	TAS_W(0x09, 0x20);          /* HP driver up */
	TAS_W(0x10, 0x00);          /* HP unmuted */
	TAS_W(0x2E, 0x00);          /* speaker attenuation 0 dB */
	TAS_W(0x30, 0x30);          /* class-D gain +18 dB (tape-looper's value) */
	TAS_W(0x2D, 0x00);          /* wait for jack detection before enabling speaker */

	TAS_W(0x00, 0x00);          /* back to page 0 */
	k_msleep(10);

	return true;
}

/* ============================================================================
 *  CS42L42 headphone codec, and FRAME-CLOCK MASTER -- VERBATIM from sp1-tape-looper's
 *  live path (HP_TIM_TEST 1), which is Tim Knapen's SP-1-dev wiki sequence.
 *
 *  ⚠️ tape-looper's own comments above that code describe an OLDER design with the
 *  oscillator OFF and the nRF as master. That path is dead code there. This is the
 *  live one: oscillator ON, CS42L42 mastering the frame. Do not port the stale comment.
 * ========================================================================== */
static bool cs_pw(uint16_t reg, uint8_t val)   /* paged write */
{
	sp1_feed_watchdog();                                /* see the note above tas_wr */
	const uint8_t p[2] = { 0x00, (uint8_t)(reg >> 8) };
	const uint8_t b[2] = { (uint8_t)reg, val };
	if (i2c_write(i2c_bus, p, 2, CS42L42_ADDR) != 0) {
		return false;
	}
	return i2c_write(i2c_bus, b, 2, CS42L42_ADDR) == 0;
}

static bool cs_pr(uint16_t reg, uint8_t *val)  /* paged read */
{
	sp1_feed_watchdog();
	const uint8_t p[2] = { 0x00, (uint8_t)(reg >> 8) };
	const uint8_t o = (uint8_t)reg;
	if (i2c_write(i2c_bus, p, 2, CS42L42_ADDR) != 0) {
		return false;
	}
	return i2c_write_read(i2c_bus, CS42L42_ADDR, &o, 1, val, 1) == 0;
}

#define CS_W(reg, val) do { if (!cs_pw((reg), (val))) { return false; } } while (0)

static bool cs42l42_configure(void)
{
	/* Hard reset pulse -- without it the codec can be wedged and NAK everything. */
	gpio_pin_set(gpio, 15, 0);
	k_msleep(5);
	gpio_pin_set(gpio, 15, 1);
	k_msleep(10);

	uint8_t id = 0;
	if (!cs_pr(0x1001, &id) || id != 0x42) {
		return false;                      /* not a CS42L42: leave it alone */
	}

	CS_W(0x1508, 0x10);   /* PLL Control 3 */
	CS_W(0x1504, 0x80);   /* PLL Division Fractional Byte 2 */
	CS_W(0x1505, 0x3E);   /* PLL Division Integer */
	CS_W(0x150A, 0x7D);   /* PLL Calibration Ratio */
	CS_W(0x1009, 0x00);   /* MCLK Control */
	CS_W(0x1201, 0x01);   /* MCLK Source Select */
	CS_W(0x120A, 0x01);   /* Input ASRC Clock Select */
	CS_W(0x120B, 0x01);   /* Output ASRC Clock Select */
	CS_W(0x1501, 0x01);   /* PLL Control 1: start */
	CS_W(0x1107, 0x01);   /* Oscillator Switch (the 3.072 MHz osc drives SCLK) */
	for (int t = 0; t < 10; t++) {         /* wait for "SCLK selected" (0x02) */
		uint8_t s = 0;
		k_msleep(1);
		if (cs_pr(0x1109, &s) && s == 0x02) {
			break;
		}
	}
	CS_W(0x1007, 0x13);   /* Serial Port SRC Control */
	CS_W(0x1203, 0x1F);   /* FSYNC Pulse Width Lower (64-SCLK frame) */
	CS_W(0x1205, 0x3F);   /* FSYNC Period Lower */
	CS_W(0x1207, 0x34);   /* ASP Clock Config: MASTER */
	CS_W(0x1208, 0x1A);   /* ASP Frame Configuration */
	CS_W(0x2A02, 0x02);   /* Channel 1: 24-bit */
	CS_W(0x2A05, 0x42);   /* Channel 2: phase + 24-bit */
	CS_W(0x2601, 0x4C);   /* SRC Input Sample Rate */
	CS_W(0x2609, 0x4C);   /* SRC Output Sample Rate */
	CS_W(0x2A01, 0x0C);   /* ASP Receive Enable */
	CS_W(0x240E, 0x01);   /* Equalizer Input Mute Control */
	CS_W(0x2301, 0x00);   /* Mixer A vol 0 dB */
	CS_W(0x2303, 0x00);   /* Mixer B vol */
	CS_W(0x1101, 0x96);   /* power up the codec */
	k_msleep(10);                /* HP amp operational after 10 ms */
	CS_W(0x1121, 0x41);   /* Headset switch control */
	CS_W(0x1B74, 0x03);   /* Miscellaneous detect control */
	CS_W(0x1129, 0x01);   /* Headset clamp disable */
	CS_W(0x2001, 0x0D);   /* HP Control: mute all */
	CS_W(0x1F06, 0x84);   /* DAC Control 2 */
	CS_W(0x2301, 0x00);   /* Mixer A vol again */
	CS_W(0x2303, 0x00);   /* Mixer B vol again */
	CS_W(0x1B73, 0xC2);   /* Tip Sense Control */
	CS_W(0x1B75, 0x9F);   /* Mic detect control 1 */
	CS_W(0x2001, 0x01);   /* UNMUTE headphones */
	return true;
}


int sp1_codecs_power_on(void)
{
    if (!device_is_ready(i2c_bus) || !device_is_ready(gpio))
        return -ENODEV;
    int rc = gpio_pin_configure(gpio, 9, GPIO_OUTPUT_HIGH);
    if (rc == 0)
        rc = gpio_pin_configure(gpio, 15, GPIO_OUTPUT_HIGH);
    if (rc == 0)
        rc = gpio_pin_configure(gpio, 13, GPIO_OUTPUT_HIGH);
    k_msleep(25);
    return rc;
}

int sp1_codecs_start(void)
{
    speaker_on = false;
    stable_reads = 0;
    /* BCLK exists now. Start the amp's PLL, then the frame clock master. */
    if (!tas2505_configure() || !cs42l42_configure())
        return -EIO;
    return 0;
}

void sp1_codecs_power_off(void)
{
    gpio_pin_configure(gpio, 9, GPIO_OUTPUT_LOW);
    gpio_pin_configure(gpio, 15, GPIO_OUTPUT_LOW);
    gpio_pin_configure(gpio, 13, GPIO_OUTPUT_LOW);
}

void sp1_codecs_poll_jack(void)
{
    uint8_t status;
    if (!cs_pr(0x1B77, &status)) {
        stable_reads = 0;
        return;
    }
    bool want_speaker = (status & 0x80u) == 0;
    if (want_speaker != last_wanted) {
        last_wanted = want_speaker;
        stable_reads = 0;
    }
    if (stable_reads < 3)
        ++stable_reads;
    if (stable_reads == 3 && want_speaker != speaker_on) {
        if (tas_wr(0x00, 0x01) == 0 && tas_wr(0x2D, want_speaker ? 0x02 : 0x00) == 0)
            speaker_on = want_speaker;
        tas_wr(0x00, 0x00);
    }
}
