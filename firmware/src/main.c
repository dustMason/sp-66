#include "codecs.h"
#include "controls.h"

#include <soc.h>
#include <string.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

enum {
    RATE = 48000,
    FRAMES = 64,
    BLOCK_BYTES = FRAMES * 2 * sizeof(int16_t),
    QUEUE = CONFIG_I2S_NRFX_TX_BLOCK_COUNT,
    BLOCK_BUDGET = 64000000u * FRAMES / RATE,
    FADER_MAX_RAW = 3701,
    LED_PERIOD = 16000,
    FN_PIN = 27,
    BTN_COM_PIN = 10,
};

K_MEM_SLAB_DEFINE_STATIC(audio_blocks, BLOCK_BYTES, QUEUE + 4, 4);
K_MSGQ_DEFINE(input_scans, sizeof(Tr66Input), 8, 4);

static const struct device *const i2s = DEVICE_DT_GET(DT_NODELABEL(i2s0));
static const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *const gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static const struct device *const leds[] = {
    DEVICE_DT_GET(DT_NODELABEL(pwm2)), DEVICE_DT_GET(DT_NODELABEL(pwm3)),
};

#define ANALOG(index) ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), index)
static const struct adc_dt_spec inputs[] = {
    ANALOG(1), ANALOG(2), ANALOG(3), ANALOG(4), ANALOG(5), ANALOG(6),
};

static Tr66Sequencer seq;
static Tr66Controls controls;
static atomic_t codec_status = ATOMIC_INIT(-EINPROGRESS);
static atomic_t led_state;
static uint8_t prior_leds[8];
static float pcm_gain;
static float mono[FRAMES];

/* Inspect over SWD if available. Kept in RAM and accumulated per session. */
volatile struct {
    uint32_t peak_render_cycles, over_budget, write_errors;
    int status;
} tr66_sp1_diagnostics;

static const float volume_gains[] = {
    0.0f, 0.00794f, 0.01122f, 0.01585f, 0.02239f, 0.03162f, 0.04467f, 0.06310f,
    0.08913f, 0.12589f, 0.17783f, 0.25119f, 0.35481f, 0.50119f, 0.70795f, 1.0f,
};

static enum Tr66ControlKey decode_ladder(unsigned raw, const unsigned *centers,
                                        unsigned count, enum Tr66ControlKey first)
{
    if (raw < 85)
        return TR66_KEY_NONE;
    for (unsigned i = 0; i < count; ++i) {
        if (raw + 25 >= centers[i] && raw <= centers[i] + 25)
            return first + i;
    }
    return TR66_KEY_INVALID;
}

static bool read_input(Tr66Input *input)
{
    unsigned raw[ARRAY_SIZE(inputs)];
    for (unsigned i = 0; i < ARRAY_SIZE(inputs); ++i) {
        int16_t value;
        struct adc_sequence sample = {.buffer = &value, .buffer_size = sizeof(value), .oversampling = 1};
        if (adc_sequence_init_dt(&inputs[i], &sample) != 0 || adc_read_dt(&inputs[i], &sample) != 0)
            return false;
        raw[i] = value > 0 ? value : 0;
    }
    static const unsigned track[] = {210, 399, 728, 1215, 1813};
    static const unsigned side[] = {403, 1216, 729, 1817};
    input->track = decode_ladder(raw[0], track, ARRAY_SIZE(track), TR66_KEY_T1);
    input->side = decode_ladder(raw[1], side, ARRAY_SIZE(side), TR66_KEY_A);
    input->fn = gpio_pin_get(gpio0, FN_PIN) == 0;
    for (unsigned i = 0; i < ARRAY_SIZE(input->fader); ++i)
        input->fader[i] = MIN(raw[2 + i], FADER_MAX_RAW) * 4095 / FADER_MAX_RAW;
    return true;
}

static void set_leds(unsigned mask)
{
    for (unsigned i = 0; i < ARRAY_SIZE(prior_leds); ++i) {
        uint8_t level = (mask >> (2 * i)) & 3u;
        if (level != prior_leds[i]) {
            static const unsigned pulse[] = {0, LED_PERIOD / 100, LED_PERIOD / 18, LED_PERIOD / 4};
            unsigned channel = i < 4 ? i : 7 - i;
            pwm_set_cycles(leds[i / 4], channel, LED_PERIOD, pulse[level], 0);
            prior_leds[i] = level;
        }
    }
}

static void publish_leds(void)
{
    unsigned state = 3u << (controls.bank * 2);
    if (controls.variation_enabled)
        state |= 3u << 6;
    if (controls.unavailable && (k_uptime_get_32() % 500 < 250))
        state &= ~(3u << (controls.bank * 2));

    unsigned bars = 0x0f;
    if (controls.request.arrange == TR66_ARRANGE_A)
        bars = 0x03;
    if (controls.request.arrange == TR66_ARRANGE_B)
        bars = 0x0c;
    for (unsigned i = 0; i < 4; ++i) {
        if (bars & BIT(i))
            state |= 1u << (8 + 2 * i);
    }
    if (seq.running) {
        unsigned bar = seq.bar;
        if (seq.selection.arrange == TR66_ARRANGE_A)
            bar %= 2;
        if (seq.selection.arrange == TR66_ARRANGE_B)
            bar = 2 + bar % 2;
        if (seq.fill)
            bar = 3;
        unsigned shift = 8 + 2 * bar;
        state &= ~(3u << shift);
        state |= (seq.tick < 4 ? 3u : 2u) << shift;
    }
    atomic_set(&led_state, state);
}

static void scan_controls(struct k_work *work)
{
    static Tr66Input held;
    static unsigned ladder_idle;
    Tr66Input input = {0};
    if (read_input(&input)) {
        /* The ladder loads the fader rail. Freeze positions through a press
         * and its short settling period. Keep the raw button states. */
        if (input.track != TR66_KEY_NONE || input.side != TR66_KEY_NONE)
            ladder_idle = 0;
        else if (ladder_idle < 3)
            ++ladder_idle;
        if (ladder_idle == 3)
            held = input;
        else
            memcpy(input.fader, held.fader, sizeof(input.fader));
        k_msgq_put(&input_scans, &input, K_NO_WAIT);
    }
    set_leds(atomic_get(&led_state));
    static unsigned jack_count;
    if (++jack_count == 4) {
        sp1_codecs_poll_jack();
        jack_count = 0;
    }
    k_work_schedule(k_work_delayable_from_work(work), K_MSEC(10));
}
K_WORK_DELAYABLE_DEFINE(scan_work, scan_controls);

static void start_codecs(struct k_work *work)
{
    ARG_UNUSED(work);
    atomic_set(&codec_status, sp1_codecs_start());
}
K_WORK_DEFINE(codec_start_work, start_codecs);

static int begin_stream(void)
{
    const struct i2s_config config = {
        .word_size = 16, .channels = 2,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .options = I2S_OPT_FRAME_CLK_SLAVE | I2S_OPT_BIT_CLK_SLAVE,
        .frame_clk_freq = RATE, .block_size = BLOCK_BYTES, .mem_slab = &audio_blocks,
        .timeout = 100,
    };
    int rc = i2s_configure(i2s, I2S_DIR_TX, &config);
    if (rc != 0)
        return rc;
    for (unsigned i = 0; i < QUEUE; ++i) {
        void *data;
        rc = k_mem_slab_alloc(&audio_blocks, &data, K_NO_WAIT);
        if (rc != 0)
            return rc;
        memset(data, 0, BLOCK_BYTES);
        rc = i2s_write(i2s, data, BLOCK_BYTES);
        if (rc != 0) {
            k_mem_slab_free(&audio_blocks, data);
            return rc;
        }
    }
    return i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START);
}

static void fill_audio(int16_t *stereo)
{
    float target = tr66_line_gain * volume_gains[controls.volume];
    tr66_seq_render(&seq, mono, FRAMES);
    for (unsigned i = 0; i < FRAMES; ++i) {
        pcm_gain += 0.0026f * (target - pcm_gain);
        float value = CLAMP(mono[i] * pcm_gain, -1.0f, 1.0f);
        int16_t pcm = (int16_t)(value * 32767.0f);
        stereo[2 * i] = pcm;
        stereo[2 * i + 1] = pcm;
    }
}

static int init_controls(void)
{
    if (!device_is_ready(gpio0) || !device_is_ready(gpio1))
        return -ENODEV;
    int rc = gpio_pin_configure(gpio0, FN_PIN, GPIO_INPUT | GPIO_PULL_UP);
    if (rc == 0)
        rc = gpio_pin_configure(gpio1, BTN_COM_PIN, GPIO_OUTPUT_HIGH);
    if (rc != 0)
        return rc;
    for (unsigned i = 0; i < ARRAY_SIZE(inputs); ++i) {
        if (!adc_is_ready_dt(&inputs[i]))
            return -ENODEV;
        rc = adc_channel_setup_dt(&inputs[i]);
        if (rc != 0)
            return rc;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(leds); ++i) {
        rc = device_init(leds[i]);
        if (rc != 0 && rc != -EALREADY)
            return rc;
    }
    memset(prior_leds, 0xff, sizeof(prior_leds));
    set_leds(0);
    return 0;
}

static void idle_until_wake(void)
{
    /* Wait for release so GPIO level sensing will see the next press. */
    while (gpio_pin_get(gpio0, FN_PIN) == 0) {
        sp1_feed_watchdog();
        k_msleep(20);
    }
    k_msleep(60);
    gpio_pin_set(gpio1, BTN_COM_PIN, 0);
    gpio_pin_configure(gpio0, 21, GPIO_OUTPUT_LOW);  /* charger nCE */
    gpio_pin_configure(gpio0, 24, GPIO_INPUT | GPIO_PULL_UP);
    while ((NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) ||
           gpio_pin_get(gpio0, 24) == 0) {
        if (gpio_pin_get(gpio0, FN_PIN) == 0)
            sys_reboot(SYS_REBOOT_COLD);
        sp1_feed_watchdog();
        k_msleep(20);
    }
    NRF_P0->PIN_CNF[FN_PIN] |= GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos;
    sp1_feed_watchdog();
    sys_poweroff();
}

int main(void)
{
    sp1_feed_watchdog();
    tr66_seq_init(&seq, RATE, UINT32_C(14293));
    tr66_controls_init(&controls);
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    int rc = init_controls();
    if (rc == 0)
        rc = gpio_pin_configure(gpio0, 21, GPIO_OUTPUT_HIGH); /* disable charge while playing */
    if (rc == 0 && device_is_ready(i2s))
        rc = sp1_codecs_power_on();
    else if (rc == 0)
        rc = -ENODEV;
    if (rc == 0)
        rc = begin_stream();
    if (rc == 0) {
        /* The codec starts the frame clock before its setup finishes.
         * Keep supplying silence while setup runs on the work queue. */
        int submitted = k_work_submit(&codec_start_work);
        rc = submitted < 0 ? submitted : 0;
    }
    tr66_sp1_diagnostics.status = rc == 0 ? -EINPROGRESS : rc;

    if (rc == 0) {
        bool codecs_ready = false;
        bool shutdown = false;
        while (!shutdown) {
            sp1_feed_watchdog();
            if (!codecs_ready) {
                int status = atomic_get(&codec_status);
                if (status != -EINPROGRESS) {
                    rc = status;
                    tr66_sp1_diagnostics.status = rc;
                    if (rc != 0)
                        break;
                    codecs_ready = true;
                    k_work_schedule(&scan_work, K_NO_WAIT);
                }
            }
            Tr66Input input;
            while (k_msgq_get(&input_scans, &input, K_NO_WAIT) == 0)
                shutdown |= tr66_controls_scan(&controls, &seq, input);
            if (shutdown)
                break;
            void *data;
            if (k_mem_slab_alloc(&audio_blocks, &data, K_MSEC(100)) != 0)
                continue;
            uint32_t start = DWT->CYCCNT;
            fill_audio(data);
            uint32_t cycles = DWT->CYCCNT - start;
            tr66_sp1_diagnostics.peak_render_cycles =
                MAX(tr66_sp1_diagnostics.peak_render_cycles, cycles);
            tr66_sp1_diagnostics.over_budget += cycles > BLOCK_BUDGET;
            int write_status = i2s_write(i2s, data, BLOCK_BYTES);
            if (write_status != 0) {
                k_mem_slab_free(&audio_blocks, data);
                if (!codecs_ready && write_status == -EAGAIN)
                    continue;
                ++tr66_sp1_diagnostics.write_errors;
                i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
                k_msleep(5);
                rc = begin_stream();
                if (codecs_ready || rc != 0)
                    tr66_sp1_diagnostics.status = rc;
                if (rc != 0)
                    break;
            }
            publish_leds();
        }
        struct k_work_sync sync;
        k_work_cancel_sync(&codec_start_work, &sync);
        k_work_cancel_delayable_sync(&scan_work, &sync);
    }

    if (device_is_ready(i2s))
        i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
    k_msleep(5);
    sp1_codecs_power_off();
    if (device_is_ready(leds[0]) && device_is_ready(leds[1]))
        set_leds(0);
    idle_until_wake();
    return rc;
}
