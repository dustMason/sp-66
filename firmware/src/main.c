#include "codecs.h"
#include "controls.h"
#include "usb_midi.h"

#include <soc.h>
#include <nrfx_saadc.h>
#include <string.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>

enum {
    RATE = 48000,
    FRAMES = 64,
    BLOCK_BYTES = FRAMES * 2 * sizeof(int16_t),
    QUEUE = CONFIG_I2S_NRFX_TX_BLOCK_COUNT,
    BLOCK_BUDGET = 64000000u * FRAMES / RATE,
    FADER_MAX_RAW = 3701,
    LED_PERIOD = 16000,
    CODEC_START_TIMEOUT_MS = 2500,
    AUDIO_STALL_TIMEOUT_MS = 500,
    FN_PIN = 27,
    BTN_COM_PIN = 10,
    CHARGE_ENABLE_PIN = 21,
    USB_POWER_GOOD_PIN = 24,
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
static atomic_t power_requested;
static atomic_t power_idle;
static uint8_t prior_leds[8];
static float pcm_gain;
static float mono[FRAMES];

/* Inspect over SWD if available. Kept in RAM and accumulated per session. */
volatile struct {
    uint32_t peak_render_cycles, over_budget, write_errors;
    int status;
    unsigned stage, failed_stage, scans, blocks, scan_phase;
    int adc_status;
    int usb_status;
    uint32_t reset_reason;
    unsigned raw[6];
    unsigned adc_timeouts;
    uint32_t adc_enable, adc_end, adc_started, adc_amount;
} tr66_sp1_diagnostics = {.usb_status = -EINPROGRESS};

static __noinit uint32_t fault_key, fault_reason, fault_pc;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    fault_reason = reason;
    fault_pc = esf != NULL ? esf->basic.pc : 0;
    fault_key = 0x6631fa17;
    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}

/* The direct Function GPIO must remain usable if audio or ADC stops progressing.
 * A four-second hold is the last-resort reset when orderly shutdown stalls. */
static void power_monitor(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int64_t pressed_since = -1;
    bool released = false;
    gpio_pin_configure(gpio0, FN_PIN, GPIO_INPUT | GPIO_PULL_UP);
    for (;;) {
        bool pressed = gpio_pin_get(gpio0, FN_PIN) == 0;
        if (!pressed) {
            released = true;
            pressed_since = -1;
        } else if (released && !atomic_get(&power_idle)) {
            if (pressed_since < 0)
                pressed_since = k_uptime_get();
            int64_t held = k_uptime_get() - pressed_since;
            if (held >= 1800)
                atomic_set(&power_requested, 1);
            if (held >= 4000)
                sys_reboot(SYS_REBOOT_COLD);
        }
        k_msleep(10);
    }
}
K_THREAD_DEFINE(power_thread, 1024, power_monitor, NULL, NULL, NULL, -1, 0, 0);

static void start_usb(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    tr66_sp1_diagnostics.usb_status = sp1_usb_start();
}
K_THREAD_DEFINE(usb_init_thread, 1536, start_usb, NULL, NULL, NULL, 1, 0, 0);

static void diagnostic_console(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    for (;;) {
        uint32_t dtr = 0;
        if (device_is_ready(console) &&
            uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr) {
            unsigned char command;
            if (uart_poll_in(console, &command) == 0 && command == 'r')
                sys_reboot(SYS_REBOOT_COLD);
            printk("SP66 t=%u stage=%u failed=%u status=%d scans=%u adc=%d blocks=%u "
                   "peak=%u over=%u errors=%u fn=%d reset=%x fault=%u pc=%x usb=%d "
                   "phase=%u raw=%u,%u,%u,%u,%u,%u timeouts=%u "
                   "saadc=%u,%u,%u,%u\n",
                   k_uptime_get_32(), tr66_sp1_diagnostics.stage,
                   tr66_sp1_diagnostics.failed_stage,
                   tr66_sp1_diagnostics.status, tr66_sp1_diagnostics.scans,
                   tr66_sp1_diagnostics.adc_status, tr66_sp1_diagnostics.blocks,
                   tr66_sp1_diagnostics.peak_render_cycles,
                   tr66_sp1_diagnostics.over_budget, tr66_sp1_diagnostics.write_errors,
                   gpio_pin_get(gpio0, FN_PIN) == 0,
                   tr66_sp1_diagnostics.reset_reason,
                   fault_key == 0x6631fa17 ? fault_reason : 0,
                   fault_key == 0x6631fa17 ? fault_pc : 0,
                   tr66_sp1_diagnostics.usb_status,
                   tr66_sp1_diagnostics.scan_phase,
                   tr66_sp1_diagnostics.raw[0], tr66_sp1_diagnostics.raw[1],
                   tr66_sp1_diagnostics.raw[2], tr66_sp1_diagnostics.raw[3],
                   tr66_sp1_diagnostics.raw[4], tr66_sp1_diagnostics.raw[5],
                   tr66_sp1_diagnostics.adc_timeouts,
                   tr66_sp1_diagnostics.adc_enable, tr66_sp1_diagnostics.adc_end,
                   tr66_sp1_diagnostics.adc_started, tr66_sp1_diagnostics.adc_amount);
        }
        k_msleep(1000);
    }
}
K_THREAD_DEFINE(console_thread, 1536, diagnostic_console, NULL, NULL, NULL, 1, 0, 1000);

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

static int read_analog(const struct adc_dt_spec *spec, int16_t *value)
{
    struct adc_sequence sample = {.buffer = value, .buffer_size = sizeof(*value)};
    int rc = adc_sequence_init_dt(spec, &sample);
    if (rc != 0)
        return rc;
    sample.oversampling = 1;
    struct k_poll_signal done;
    k_poll_signal_init(&done);
    struct k_poll_event event = K_POLL_EVENT_INITIALIZER(
        K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &done);
    rc = adc_read_async(spec->dev, &sample, &done);
    if (rc != 0)
        return rc;
    if (k_poll(&event, 1, K_MSEC(10)) == 0)
        return done.result;

    ++tr66_sp1_diagnostics.adc_timeouts;
    /* A timed-out request still owns the sample buffer and the driver's lock.
     * Stop it, then await its completion before either can be reused. */
    unsigned key = irq_lock();
    tr66_sp1_diagnostics.adc_enable = NRF_SAADC->ENABLE;
    tr66_sp1_diagnostics.adc_end = NRF_SAADC->EVENTS_END;
    tr66_sp1_diagnostics.adc_started = NRF_SAADC->EVENTS_STARTED;
    tr66_sp1_diagnostics.adc_amount = NRF_SAADC->RESULT.AMOUNT;
    if (!done.signaled)
        nrfx_saadc_abort();
    irq_unlock(key);
    event.state = K_POLL_STATE_NOT_READY;
    if (k_poll(&event, 1, K_MSEC(10)) != 0) {
        /* Do not leave DMA pointing into a returned stack frame. */
        sys_reboot(SYS_REBOOT_COLD);
        CODE_UNREACHABLE;
    }
    return -ETIMEDOUT;
}

static bool read_input(Tr66Input *input)
{
    unsigned raw[ARRAY_SIZE(inputs)];
    for (unsigned i = 0; i < ARRAY_SIZE(inputs); ++i) {
        tr66_sp1_diagnostics.scan_phase = 10 + i;
        int16_t value = 0;
        int rc = read_analog(&inputs[i], &value);
        tr66_sp1_diagnostics.adc_status = rc;
        if (rc != 0)
            return false;
        raw[i] = value > 0 ? value : 0;
        tr66_sp1_diagnostics.raw[i] = raw[i];
    }
    tr66_sp1_diagnostics.scan_phase = 20;
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
        if (level != prior_leds[i] && device_is_ready(leds[i / 4])) {
            tr66_sp1_diagnostics.scan_phase = 30 + i;
            static const unsigned pulse[] = {0, LED_PERIOD / 100, LED_PERIOD / 18, LED_PERIOD / 4};
            unsigned channel = i < 4 ? i : 7 - i;
            pwm_set_cycles(leds[i / 4], channel, LED_PERIOD, pulse[level], 0);
            prior_leds[i] = level;
        }
    }
    tr66_sp1_diagnostics.scan_phase = 40;
}

static void publish_leds(void)
{
    unsigned state = 3u << (controls.bank * 2);
    /* Show the pin for the rhythm under Fader 3 while Function is held. */
    bool t4_lit = controls.fn_scans > 0 ?
                  (controls.pinned_rhythms & TR66_RHYTHM_BIT(controls.request.rhythm)) != 0 :
                  controls.variation_enabled;
    if (t4_lit)
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
    ++tr66_sp1_diagnostics.scans;
    tr66_sp1_diagnostics.scan_phase = 1;
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
        tr66_sp1_diagnostics.scan_phase = 50;
        sp1_codecs_poll_jack();
        jack_count = 0;
    }
    tr66_sp1_diagnostics.scan_phase = 60;
    k_work_schedule(k_work_delayable_from_work(work), K_MSEC(10));
    tr66_sp1_diagnostics.scan_phase = 61;
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
    memset(prior_leds, 0xff, sizeof(prior_leds));
    for (unsigned i = 0; i < ARRAY_SIZE(leds); ++i) {
        int rc = device_init(leds[i]);
        if (rc != 0 && rc != -EALREADY)
            return rc;
    }
    set_leds(3u << 8); /* Light the LED nearest Function while starting. */

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
    return 0;
}

static int run_audio(void)
{
    bool codecs_ready = false;
    const int64_t codec_deadline = k_uptime_get() + CODEC_START_TIMEOUT_MS;
    int64_t last_write = 0;
    while (!atomic_get(&power_requested)) {
        sp1_feed_watchdog();
        const int64_t now = k_uptime_get();
        if (!codecs_ready) {
            int status = atomic_get(&codec_status);
            if (status == 0) {
                codecs_ready = true;
                last_write = now;
                tr66_sp1_diagnostics.stage = 5;
                tr66_sp1_diagnostics.status = 0;
                k_work_schedule(&scan_work, K_NO_WAIT);
            } else if (status != -EINPROGRESS) {
                return status;
            } else if (now >= codec_deadline) {
                return -ETIMEDOUT;
            }
        } else if (now - last_write >= AUDIO_STALL_TIMEOUT_MS) {
            return -ETIMEDOUT;
        }

        bool shutdown = false;
        /* Expire a disconnected clock before Play chooses its clock source. */
        sp1_midi_poll(&seq, controls.initialized);
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

        int rc = i2s_write(i2s, data, BLOCK_BYTES);
        if (rc == 0) {
            ++tr66_sp1_diagnostics.blocks;
            last_write = k_uptime_get();
            if (codecs_ready)
                publish_leds();
            continue;
        }
        k_mem_slab_free(&audio_blocks, data);
        /* A full TX queue is expected until the headphone codec supplies
         * LRCK. Dropping the stream here may leave it waiting for STOPPED. */
        if (rc != -EAGAIN || codecs_ready)
            ++tr66_sp1_diagnostics.write_errors;
        if (rc == -EAGAIN)
            continue;
        if (!codecs_ready) {
            k_msleep(5); /* Let codec work finish before trying to restart TX. */
            continue;
        }

        i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
        k_msleep(5);
        rc = begin_stream();
        if (rc != 0)
            return rc;
    }
    return 0;
}

static void idle_until_wake(void)
{
    atomic_set(&power_idle, 1);
    tr66_sp1_diagnostics.stage = 9;
    /* Wait for release so GPIO level sensing will see the next press. */
    while (gpio_pin_get(gpio0, FN_PIN) == 0) {
        sp1_feed_watchdog();
        k_msleep(20);
    }
    k_msleep(60);
    gpio_pin_set(gpio1, BTN_COM_PIN, 0);
    gpio_pin_configure(gpio0, USB_POWER_GOOD_PIN, GPIO_INPUT | GPIO_PULL_UP);
    while ((NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) ||
           gpio_pin_get(gpio0, USB_POWER_GOOD_PIN) == 0) {
        if (gpio_pin_get(gpio0, FN_PIN) == 0)
            sys_reboot(SYS_REBOOT_COLD);
        sp1_feed_watchdog();
        k_msleep(20);
    }
    NRF_P0->PIN_CNF[FN_PIN] |= GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos;
    sp1_feed_watchdog();
    NRF_POWER->RESETREAS = 0xffffffff;
    sys_poweroff();
}

int main(void)
{
    tr66_sp1_diagnostics.reset_reason = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xffffffff;
    sp1_feed_watchdog();
    const struct device *watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
    if (device_is_ready(watchdog)) {
        const struct wdt_timeout_cfg timeout = {
            .window.max = 4000, .flags = WDT_FLAG_RESET_SOC,
        };
        /* An already-running bootloader watchdog cannot be reconfigured.
         * The existing feed helper reloads every channel in either case. */
        if (wdt_install_timeout(watchdog, &timeout) >= 0)
            wdt_setup(watchdog, 0);
    }
    tr66_sp1_diagnostics.stage = 1;
    tr66_seq_init(&seq, RATE, UINT32_C(14293));
    tr66_controls_init(&controls);
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    tr66_sp1_diagnostics.stage = 2;
    int rc = gpio_pin_configure(gpio0, CHARGE_ENABLE_PIN, GPIO_OUTPUT_LOW);
    if (rc == 0)
        rc = init_controls();
    if (rc == 0) {
        tr66_sp1_diagnostics.stage = 3;
        rc = gpio_pin_set(gpio0, CHARGE_ENABLE_PIN, 1); /* disable charging while playing */
    }
    if (rc == 0 && device_is_ready(i2s))
        rc = sp1_codecs_power_on();
    else if (rc == 0)
        rc = -ENODEV;
    if (rc == 0)
        rc = begin_stream();

    bool codec_work_started = false;
    if (rc == 0) {
        tr66_sp1_diagnostics.stage = 4;
        /* The codec starts the frame clock before its setup finishes.
         * Keep supplying silence while setup runs on the work queue. */
        rc = k_work_submit(&codec_start_work);
        if (rc >= 0) {
            codec_work_started = true;
            tr66_sp1_diagnostics.status = -EINPROGRESS;
            rc = run_audio();
        }
    }
    tr66_sp1_diagnostics.status = rc;
    if (rc != 0)
        tr66_sp1_diagnostics.failed_stage = tr66_sp1_diagnostics.stage;
    if (codec_work_started) {
        tr66_sp1_diagnostics.stage = 6;
        struct k_work_sync sync;
        k_work_cancel_sync(&codec_start_work, &sync);
        k_work_cancel_delayable_sync(&scan_work, &sync);
    }

    if (device_is_ready(i2s))
        i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
    k_msleep(5);
    /* An I2S slave may never see STOPPED if the frame clock disappeared.
     * Disable the peripheral before parking the codec and its clocks. */
    NRF_I2S->ENABLE = I2S_ENABLE_ENABLE_Disabled;
    gpio_pin_configure(gpio0, 12, GPIO_DISCONNECTED); /* BCLK */
    gpio_pin_configure(gpio0, 11, GPIO_DISCONNECTED); /* LRCK */
    gpio_pin_configure(gpio1, 9, GPIO_DISCONNECTED);  /* SDOUT */
    sp1_codecs_power_off();
    gpio_pin_configure(gpio0, CHARGE_ENABLE_PIN, GPIO_OUTPUT_LOW);
    if (rc != 0) {
        tr66_sp1_diagnostics.stage = 8;
        while (!atomic_get(&power_requested)) {
            set_leds(k_uptime_get_32() % 500 < 250 ? 0xff00u : 0);
            sp1_feed_watchdog();
            k_msleep(50);
        }
    }
    set_leds(0);
    idle_until_wake();
    return rc;
}
