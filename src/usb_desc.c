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
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0201, 0x01)
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

/* ========================================================================
 * Microsoft OS 1.0 descriptors (WCID)
 *
 * Windows binds WinUSB to the DFU runtime interface (0) automatically, so
 * dfu-util can claim it without a manual Zadig step -- the same mechanism
 * hpm_dfu_boot and candleLight_fw_hpm_port use.
 *
 * The CDC ACM interfaces (1-2) are deliberately NOT listed in the compatible
 * ID descriptor: Windows then keeps its inbox usbser.sys for them, so the
 * VCOM still enumerates as a COM port.
 *
 * Windows queries:
 *   GET_DESCRIPTOR(String, index 0xEE)      -> msos_string
 *   vendor request bRequest=0x20 wIndex=4   -> msos_compat_id
 *   vendor request bRequest=0x20 wIndex=5   -> msos_ext_prop (wValue = 0)
 * ======================================================================== */
#define APP_WINUSB_VENDOR_CODE 0x20U

/* DeviceInterfaceGUID of the app-side DFU runtime interface.  Kept identical
 * to candleLight's DFU runtime GUID so host tooling stays consistent. */
#define APP_DFU_INTERFACE_GUID "{3f8b2c47-9d15-4a6e-b2c8-5e0f7a4d1b93}"

/* Microsoft OS String Descriptor, index 0xEE ("MSFT100" + vendor code) */
static const uint8_t msos_string[] = {
    0x12, 0x03,
    'M', 0x00, 'S', 0x00, 'F', 0x00, 'T', 0x00,
    '1', 0x00, '0', 0x00, '0', 0x00,
    APP_WINUSB_VENDOR_CODE,
    0x00,
};

/* Compatible ID Feature Descriptor: one interface (0, DFU) -> WINUSB */
static const uint8_t msos_compat_id[] = {
    0x28, 0x00, 0x00, 0x00, /* dwLength = 16 + 24 * 1 */
    0x00, 0x01,             /* bcdVersion 1.0 */
    0x04, 0x00,             /* wIndex 0x0004 */
    0x01,                   /* bCount */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* reserved[7] */
    0x00,                   /* bFirstInterfaceNumber = 0 (DFU runtime) */
    0x01,                   /* reserved1 */
    0x57, 0x49, 0x4E, 0x55, /* compatibleID "WINUSB\0\0" */
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, /* subCompatibleID */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* reserved2[6] */
};

/*
 * Extended Properties Feature Descriptor (DeviceInterfaceGUIDs), assembled at
 * init time from the plain ASCII GUID above so the UTF-16LE conversion cannot
 * be mistyped.
 *   10 header + 4 dwPropertySize + 4 dwPropertyDataType
 *   + 2 wPropertyNameLength + 42 L"DeviceInterfaceGUIDs"
 *   + 4 dwPropertyDataLength + 80 REG_MULTI_SZ payload
 */
#define MSOS_EXT_PROP_LEN (0x92U)
/* D-cache is enabled and usb_dcache_clean() is a no-op here, so this RAM
 * buffer is handed to the controller from the non-cacheable section. */
static USB_NOCACHE_RAM_SECTION uint8_t msos_ext_prop[MSOS_EXT_PROP_LEN];

_Static_assert((10U + 4U + 4U + 2U + (sizeof("DeviceInterfaceGUIDs") * 2U) + 4U +
                ((sizeof(APP_DFU_INTERFACE_GUID) + 1U) * 2U)) == MSOS_EXT_PROP_LEN,
               "msos_ext_prop size mismatch");

/* Returned for wValue != 0: a valid but empty property set. */
static const uint8_t msos_ext_prop_empty[] = {
    0x0a, 0x00, 0x00, 0x00, /* dwLength = 10 */
    0x00, 0x01,             /* bcdVersion 1.0 */
    0x05, 0x00,             /* wIndex 0x0005 */
    0x00, 0x00,             /* bCount = 0 */
};

/* CherryUSB indexes this array with setup->wValue, so keep two entries. */
static const uint8_t *msos_ext_prop_list[2];

static const struct usb_msosv1_descriptor composite_msosv1 = {
    .string = msos_string,
    .vendor_code = APP_WINUSB_VENDOR_CODE,
    .compat_id = msos_compat_id,
    .comp_id_property = msos_ext_prop_list,
};

static void msos_ext_prop_build(void)
{
    static const char prop_name[] = "DeviceInterfaceGUIDs";
    static const char guid[] = APP_DFU_INTERFACE_GUID;
    const uint32_t name_bytes = (uint32_t)sizeof(prop_name) * 2U;      /* + NUL */
    const uint32_t data_bytes = ((uint32_t)sizeof(guid) + 1U) * 2U;    /* + 2 NUL */
    const uint32_t section_len = 4U + 4U + 2U + name_bytes + 4U + data_bytes;
    uint32_t p = 0U;
    uint32_t i;

    msos_ext_prop[p++] = (uint8_t)(10U + section_len);
    msos_ext_prop[p++] = (uint8_t)((10U + section_len) >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U; /* bcdVersion 1.0 */
    msos_ext_prop[p++] = 0x01U;
    msos_ext_prop[p++] = 0x05U; /* wIndex 0x0005 */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x01U; /* bCount = 1 */
    msos_ext_prop[p++] = 0x00U;

    msos_ext_prop[p++] = (uint8_t)(section_len);
    msos_ext_prop[p++] = (uint8_t)(section_len >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x07U; /* dwPropertyDataType: REG_MULTI_SZ */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = (uint8_t)(name_bytes);
    msos_ext_prop[p++] = (uint8_t)(name_bytes >> 8);

    for (i = 0U; i < (uint32_t)sizeof(prop_name); i++) {
        msos_ext_prop[p++] = (uint8_t)prop_name[i];
        msos_ext_prop[p++] = 0x00U;
    }

    msos_ext_prop[p++] = (uint8_t)(data_bytes);
    msos_ext_prop[p++] = (uint8_t)(data_bytes >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;

    for (i = 0U; i < (uint32_t)sizeof(guid); i++) {
        msos_ext_prop[p++] = (uint8_t)guid[i];
        msos_ext_prop[p++] = 0x00U;
    }
    /* REG_MULTI_SZ terminator */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;

    msos_ext_prop_list[0] = msos_ext_prop;
    msos_ext_prop_list[1] = msos_ext_prop_empty;
}

const struct usb_descriptor composite_descriptor = {
    .device_descriptor_callback         = device_descriptor_cb,
    .config_descriptor_callback         = config_descriptor_cb,
    .device_quality_descriptor_callback = device_quality_descriptor_cb,
    .other_speed_descriptor_callback    = other_speed_descriptor_cb,
    .string_descriptor_callback         = string_descriptor_cb,
    /* MS OS 1.0 (WCID) so Windows installs WinUSB for the DFU interface */
    .msosv1_descriptor = &composite_msosv1,
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
        hpm_reboot_to_boot();
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
    /* Assemble the WCID extended properties (DeviceInterfaceGUIDs) */
    msos_ext_prop_build();

    usbd_desc_register(busid, &composite_descriptor);

    /* DFU runtime interface — registered first => interface number 0 */
    dfu_intf.class_interface_handler = dfu_handler;
    usbd_add_interface(busid, &dfu_intf);

    /* CDC ACM VCOM — registered next => interfaces 1 and 2 */
    cdc_acm_register(busid);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}
