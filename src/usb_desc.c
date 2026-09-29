/*
 * Composite USB device: DFU runtime (interface 0) + CDC ACM VCOM (interfaces 1-2).
 *
 * - DFU runtime responds to DFU_DETACH (dfu-util -e) and reboots into the bootloader
 *   by writing the DFU trigger magic to a retention register (BGPR / PDGO).
 * - CDC ACM provides a virtual serial port; received data is echoed back (loopback).
 *
 * The device descriptor is composite (IAD / misc class) so both functions coexist,
 * and there is a single usbd_initialize() call.
 */
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "hpm_dfu_trigger.h"
#include "usb_config.h"

/* CDC ACM endpoint addresses */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

/* DFU runtime interface + functional descriptor length */
#define DFU_IF_LEN (9 + 9)

/* CDC ACM class descriptor length (from usb_cdc.h) */
#ifndef CDC_ACM_DESCRIPTOR_LEN
#define CDC_ACM_DESCRIPTOR_LEN (8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7)
#endif

#define USB_CONFIG_SIZE (9 + DFU_IF_LEN + CDC_ACM_DESCRIPTOR_LEN)

/* ---------- Descriptors ---------- */

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0200, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    /* DFU runtime interface (interface 0) */
    0x09, 0x04, 0x00, 0x00, 0x00,
    0xFE, 0x01, 0x01,             /* App-Specific, DFU, DFU runtime */
    0x04,                         /* iInterface = 4 -> "DFU Runtime" */
    /* DFU Functional Descriptor */
    0x09, 0x21,
    0x0B,                         /* bitCanDnload|bitCanUpload|bitManifestationTolerant|bitWillDetach */
    0xFF, 0x00,                   /* wDetachTimeout = 255ms */
    0x00, 0x10,                   /* wTransferSize = 4096 */
    0x1A, 0x01,                   /* bcdDFU = 1.1a */
    /* CDC ACM (interfaces 1-2) */
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x05),
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    0x09, 0x04, 0x00, 0x00, 0x00,
    0xFE, 0x01, 0x01,
    0x04,
    0x09, 0x21,
    0x0B,
    0xFF, 0x00,
    0x00, 0x10,
    0x1A, 0x01,
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x05),
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01)
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    0x09, 0x04, 0x00, 0x00, 0x00,
    0xFE, 0x01, 0x01,
    0x04,
    0x09, 0x21,
    0x0B,
    0xFF, 0x00,
    0x00, 0x10,
    0x1A, 0x01,
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x05),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    0x09, 0x04, 0x00, 0x00, 0x00,
    0xFE, 0x01, 0x01,
    0x04,
    0x09, 0x21,
    0x0B,
    0xFF, 0x00,
    0x00, 0x10,
    0x1A, 0x01,
    CDC_ACM_DESCRIPTOR_INIT(0x01, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x05),
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "HPMicro",                    /* Manufacturer */
    "HPM DFU App",                /* Product */
    "2026062900",                 /* Serial Number */
    "DFU Runtime",                /* iInterface 4 */
    "CDC ACM",                    /* iInterface 5 */
};

static const uint8_t *device_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_cb(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return config_descriptor_fs;
    }
    return NULL;
}

static const uint8_t *device_quality_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_descriptor_cb(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return other_speed_config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return other_speed_config_descriptor_fs;
    }
    return NULL;
}

static const char *string_descriptor_cb(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor composite_descriptor = {
    .device_descriptor_callback         = device_descriptor_cb,
    .config_descriptor_callback         = config_descriptor_cb,
    .device_quality_descriptor_callback = device_quality_descriptor_cb,
    .other_speed_descriptor_callback    = other_speed_descriptor_cb,
    .string_descriptor_callback         = string_descriptor_cb,
    .msosv2_descriptor = NULL,
    .bos_descriptor    = NULL,
};

/* ---------- Minimal DFU class handler (runtime) ---------- */

enum {
    DFU_DETACH    = 0,
    DFU_DNLOAD    = 1,
    DFU_UPLOAD    = 2,
    DFU_GETSTATUS = 3,
    DFU_CLRSTATUS = 4,
    DFU_GETSTATE  = 5,
    DFU_ABORT     = 6,
};

static int dfu_handler(uint8_t busid, struct usb_setup_packet *setup,
                       uint8_t **data, uint32_t *len)
{
    (void)busid;
    switch (setup->bRequest) {
    case DFU_DETACH:
        hpm_dfu_reboot_to_dfu();
        return 0;
    case DFU_GETSTATUS: {
        static uint8_t status[6] = { 0, 0, 0, 0, 0, 0 }; /* OK, appIDLE */
        *data = status;
        *len = sizeof(status);
        return 0;
    }
    case DFU_GETSTATE: {
        static uint8_t state = 0; /* appIDLE */
        *data = &state;
        *len = 1;
        return 0;
    }
    default:
        return 0;
    }
}

/* ---------- CDC ACM VCOM (loopback / echo) ---------- */

/* endpoint callbacks (defined below, declared here for the endpoint structs) */
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes);
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes);

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t read_buffer[2][512];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t write_buffer[2048];

volatile uint8_t read_buffer_index;
volatile bool ep_tx_busy_flag;
volatile bool dtr_enable;

static struct usbd_interface intf0;
static struct usbd_interface intf1;
static struct usbd_interface dfu_intf;

struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static void cdc_acm_register(uint8_t busid)
{
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);
}

static void cdc_acm_start_read(uint8_t busid)
{
    read_buffer_index = 0;
    usbd_ep_start_read(busid, CDC_OUT_EP, &read_buffer[0][0], usbd_get_ep_mps(busid, CDC_OUT_EP));
}

/* Bulk OUT: echo received data back on IN, then re-arm OUT for the next packet. */
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    uint8_t index = read_buffer_index;

    read_buffer_index = (index == 0) ? 1 : 0;
    usbd_ep_start_write(busid, CDC_IN_EP, &read_buffer[index][0], nbytes);
    usbd_ep_start_read(busid, ep, &read_buffer[read_buffer_index][0], usbd_get_ep_mps(busid, ep));
}

/* Bulk IN: send ZLP when a full MPS packet was written, otherwise mark idle. */
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;

    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        usbd_ep_start_write(busid, ep, NULL, 0); /* zero-length packet */
    } else {
        ep_tx_busy_flag = false;
    }
}

/* DTR state reported by the host. */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;

    if (dtr) {
        dtr_enable = 1;
    } else {
        dtr_enable = 0;
    }
}

/* ---------- USB init ---------- */

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        /* start the first CDC OUT transfer once configured */
        cdc_acm_start_read(busid);
        break;
    default:
        break;
    }
}

void app_usb_init(uint8_t busid, uintptr_t reg_base)
{
    usbd_desc_register(busid, &composite_descriptor);

    /* DFU runtime interface — registered first => interface number 0 */
    dfu_intf.class_interface_handler = dfu_handler;
    usbd_add_interface(busid, &dfu_intf);

    /* CDC ACM VCOM — registered next => interfaces 1 and 2 */
    cdc_acm_register(busid);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}
