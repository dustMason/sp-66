/* SPDX-License-Identifier: MIT */
/* USB-MIDI 1.0 descriptors adapted from feldd, Copyright (c) 2026 Benjamin Reece. */
#include "usb_midi.h"

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

enum {
    MIDI_MPS = 64,
    QUEUE_DEPTH = 32,
    EXT_IN_JACK = 1, EMBEDDED_OUT_JACK = 2,
    EMBEDDED_IN_JACK = 3, EXT_OUT_JACK = 4,
};

typedef struct {
    Tr66MidiEvent event;
    uint32_t received_ms;
} ReceivedMidi;

K_MSGQ_DEFINE(midi_events, sizeof(ReceivedMidi), QUEUE_DEPTH, 4);
UDC_BUF_POOL_DEFINE(midi_rx_pool, 1, MIDI_MPS, sizeof(struct udc_buf_info), NULL);

/* AudioControl names the MIDIStreaming interface. Keep the two interfaces
 * grouped via the class header, as in Wakes/feldd, with both bulk directions. */
static struct usb_if_descriptor audio_control = {
    .bLength = sizeof(audio_control),
    .bDescriptorType = USB_DESC_INTERFACE,
    .bInterfaceClass = USB_BCC_AUDIO,
    .bInterfaceSubClass = 0x01,
};
static uint8_t audio_header[] = {9, USB_DESC_CS_INTERFACE, 1, 0, 1, 9, 0, 1, 0};
static struct usb_if_descriptor midi_streaming = {
    .bLength = sizeof(midi_streaming),
    .bDescriptorType = USB_DESC_INTERFACE,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_BCC_AUDIO,
    .bInterfaceSubClass = 0x03,
};
static const uint8_t external_in[] = {6, USB_DESC_CS_INTERFACE, 2, 2, EXT_IN_JACK, 0};
static const uint8_t embedded_out[] = {
    9, USB_DESC_CS_INTERFACE, 3, 1, EMBEDDED_OUT_JACK, 1, EXT_IN_JACK, 1, 0,
};
static const uint8_t embedded_in[] = {6, USB_DESC_CS_INTERFACE, 2, 1, EMBEDDED_IN_JACK, 0};
static const uint8_t external_out[] = {
    9, USB_DESC_CS_INTERFACE, 3, 2, EXT_OUT_JACK, 1, EMBEDDED_IN_JACK, 1, 0,
};
static const uint8_t midi_header[] = {
    7, USB_DESC_CS_INTERFACE, 1, 0, 1,
    7 + sizeof(external_in) + sizeof(embedded_out) + sizeof(embedded_in) + sizeof(external_out),
    0,
};
/* MIDI 1.0 bulk endpoint descriptors have two trailing audio fields.
 * Zephyr assigns the addresses at init. */
struct midi_endpoint {
    struct usb_ep_descriptor usb;
    uint8_t refresh, synch_address;
} __packed;
static struct midi_endpoint midi_out_ep = {
    .usb = {
        .bLength = sizeof(midi_out_ep), .bDescriptorType = USB_DESC_ENDPOINT,
        .bEndpointAddress = 0x01, .bmAttributes = USB_EP_TYPE_BULK,
        .wMaxPacketSize = sys_cpu_to_le16(MIDI_MPS),
    },
};
static struct midi_endpoint midi_in_ep = {
    .usb = {
        .bLength = sizeof(midi_in_ep), .bDescriptorType = USB_DESC_ENDPOINT,
        .bEndpointAddress = 0x81, .bmAttributes = USB_EP_TYPE_BULK,
        .wMaxPacketSize = sys_cpu_to_le16(MIDI_MPS),
    },
};
static const uint8_t midi_out_jack[] = {5, USB_DESC_CS_ENDPOINT, 1, 1, EMBEDDED_IN_JACK};
static const uint8_t midi_in_jack[] = {5, USB_DESC_CS_ENDPOINT, 1, 1, EMBEDDED_OUT_JACK};

#define DESC(item) ((const struct usb_desc_header *)&(item))
static const struct usb_desc_header *midi_descriptors[] = {
    DESC(audio_control), DESC(audio_header), DESC(midi_streaming), DESC(midi_header),
    DESC(external_in), DESC(embedded_out), DESC(embedded_in), DESC(external_out),
    DESC(midi_out_ep), DESC(midi_out_jack), DESC(midi_in_ep), DESC(midi_in_jack),
    NULL,
};
#undef DESC

/* Only the USB stack thread writes the queue and owns the endpoint state. */
static bool receiving, rx_pending;

static void reset_midi_queue(void)
{
    /* An empty event tells the audio thread to discard the old transport.
     * Preserve ordering with any packets arriving after a reset/overflow. */
    k_msgq_purge(&midi_events);
    const ReceivedMidi reset = {0};
    k_msgq_put(&midi_events, &reset, K_NO_WAIT);
}

static void arm_rx(struct usbd_class_data *class_data)
{
    if (!receiving || rx_pending)
        return;
    struct net_buf *buffer = net_buf_alloc(&midi_rx_pool, K_NO_WAIT);
    if (buffer == NULL)
        return;
    udc_get_buf_info(buffer)->ep = midi_out_ep.usb.bEndpointAddress;
    rx_pending = usbd_ep_enqueue(class_data, buffer) == 0;
    if (!rx_pending)
        net_buf_unref(buffer);
}

static int midi_request(struct usbd_class_data *class_data, struct net_buf *buffer, int error)
{
    rx_pending = false;
    if (error == 0 && receiving) {
        const uint32_t now = k_uptime_get_32();
        for (size_t offset = 0; offset + 4 <= buffer->len; offset += 4) {
            ReceivedMidi message = {tr66_midi_usb_event(buffer->data + offset), now};
            if (message.event.status != TR66_MIDI_NONE &&
                k_msgq_put(&midi_events, &message, K_NO_WAIT) != 0) {
                reset_midi_queue();
                break;
            }
        }
    }
    net_buf_unref(buffer);
    arm_rx(class_data);
    return 0;
}

static void midi_enable(struct usbd_class_data *class_data)
{
    receiving = true;
    arm_rx(class_data);
}

static void midi_disable(struct usbd_class_data *class_data)
{
    ARG_UNUSED(class_data);
    receiving = false;
    reset_midi_queue();
}

static void midi_halt(struct usbd_class_data *class_data, uint8_t endpoint, bool halted)
{
    if (endpoint == midi_out_ep.usb.bEndpointAddress && !halted)
        arm_rx(class_data);
}

static int midi_init(struct usbd_class_data *class_data)
{
    ARG_UNUSED(class_data);
    audio_header[8] = midi_streaming.bInterfaceNumber;
    return 0;
}

static void *midi_get_desc(struct usbd_class_data *class_data, enum usbd_speed speed)
{
    ARG_UNUSED(class_data);
    return speed == USBD_SPEED_FS ? midi_descriptors : NULL;
}

static const struct usbd_class_api midi_api = {
    .request = midi_request,
    .enable = midi_enable,
    .disable = midi_disable,
    .suspended = midi_disable,
    .resumed = midi_enable,
    .feature_halt = midi_halt,
    .init = midi_init,
    .get_desc = midi_get_desc,
};
USBD_DEFINE_CLASS(sp66_midi, &midi_api, NULL, NULL);

/* Use the existing Zephyr development VID/PID, with a new device revision
 * for the composite interface layout. This is independent of the bootloader. */
USBD_DEVICE_DEFINE(sp66_usb, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0004);
USBD_DESC_LANG_DEFINE(sp66_lang);
USBD_DESC_MANUFACTURER_DEFINE(sp66_manufacturer, "SP-66");
USBD_DESC_PRODUCT_DEFINE(sp66_product, "SP-66");
USBD_DESC_SERIAL_NUMBER_DEFINE(sp66_serial);
USBD_CONFIGURATION_DEFINE(sp66_config, USB_SCD_SELF_POWERED, 125, NULL);

static void usb_message(struct usbd_context *context, const struct usbd_msg *message)
{
    ARG_UNUSED(context);
    if (message->type == USBD_MSG_VBUS_REMOVED)
        midi_disable(NULL);
}

int sp1_usb_start(void)
{
    struct usbd_desc_node *const descriptors[] = {
        &sp66_lang, &sp66_manufacturer, &sp66_product, &sp66_serial,
    };
    int rc;
    for (size_t i = 0; i < ARRAY_SIZE(descriptors); ++i) {
        rc = usbd_add_descriptor(&sp66_usb, descriptors[i]);
        if (rc != 0)
            return rc;
    }
    rc = usbd_add_configuration(&sp66_usb, USBD_SPEED_FS, &sp66_config);
    if (rc == 0)
        rc = usbd_register_class(&sp66_usb, "cdc_acm_0", USBD_SPEED_FS, 1);
    if (rc == 0)
        rc = usbd_register_class(&sp66_usb, "sp66_midi", USBD_SPEED_FS, 1);
    if (rc == 0)
        rc = usbd_device_set_bcd_device(&sp66_usb, 0x0101);
    if (rc == 0)
        rc = usbd_msg_register_cb(&sp66_usb, usb_message);
    if (rc == 0)
        rc = usbd_init(&sp66_usb);
    if (rc == 0)
        rc = usbd_enable(&sp66_usb);
    return rc;
}

void sp1_midi_poll(Tr66Sequencer *seq, bool audio_ready)
{
    ReceivedMidi message;
    for (unsigned i = 0; i < QUEUE_DEPTH; ++i) {
        if (k_msgq_get(&midi_events, &message, K_NO_WAIT) != 0)
            break;
        if (message.event.status == TR66_MIDI_NONE)
            tr66_seq_midi_disconnect(seq);
        else if (audio_ready)
            tr66_seq_midi_event(seq, message.event, message.received_ms);
    }
    tr66_seq_midi_poll(seq, k_uptime_get_32());
}
