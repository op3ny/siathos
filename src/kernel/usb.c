#include "thais.h"
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * USB core – minimal bus‑enumeration for a single root‑port (xHCI emulated).
 * --------------------------------------------------------------------------- */

/* Device and endpoint descriptors – static for a generic USB 2.0 Full‑Speed
   keyboard (idVendor=0x045E, idProduct=0x00DB – Microsoft LifeCam, but we only
   need the HID report descriptor).  The host (QEMU) presents these when the
   device enumerates. */

/* ---------------------------------------------------------------------- */
static const uint8_t dev_desc[] = {
    18,                      /* bLength */
    1,                     /* bDescriptorType (DEVICE) */
    0x00, 0x02,            /* bcdUSB 2.00 */
    0x00,                  /* bDeviceClass (Interface per subclass) */
    0x00,                  /* bDeviceSubClass */
    0x00,                  /* bDeviceProtocol */
    64,                    /* bMaxPacketSize0 */
    0x04, 0x05,            /* idVendor = 0x0504 (Intel) – placeholder */
    0x00, 0x01,            /* idProduct */
    0x00, 0x01,            /* bcdDevice */
    0x01,                  /* iManufacturer */
    0x02,                  /* iProduct */
    0x00,                  /* iSerialNumber */
    0x01                   /* bNumConfigurations */
};

/* Configuration + Interface + HID + Endpoint descriptor (tiny self‑contained)
   – enough for the kernel to ask “is there a HID device?” and then read the
   report descriptor + endpoint 0x81 (interrupt IN). */
static const uint8_t cfg_desc[] = {
    9,                      /* bLength */
    2,                      /* bDescriptorType (CONFIGURATION) */
    9+9+7+7,                /* wTotalLength */
    1,                      /* bNumInterfaces */
    1,                      /* bConfigurationValue */
    0,                      /* iConfiguration */
    0x80,                   /* bmAttributes (bus‑powered) */
    250,                    /* bMaxPower (2 mA) */

    /* Interface */
    9,                      /* bLength */
    4,                      /* bDescriptorType (INTERFACE) */
    0,                      /* bInterfaceNumber */
    0,                      /* bAlternateSetting */
    1,                      /* bNumEndpoints */
    0x03,                   /* bInterfaceClass (HID) */
    0x01,                   /* bInterfaceSubClass (BOOT) */
    0x01,                   /* bInterfaceProtocol (keyboard) */
    0,                      /* iInterface */

    /* HID Descriptor */
    9,                      /* bLength */
    0x21,                   /* bDescriptorType (HID) */
    0x00, 0x01,             /* bcdHID 1.01 */
    0x00,                   /* bCountryCode */
    0x01,                   /* bNumDescriptors */
    0x22,                   /* bDescriptorType (REPORT) */
    57,                     /* wDescriptorLength */

    /* HID Report Descriptor – standard 6‑keyboard + modifiers report */
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x06,             /* Usage (Keyboard) */
    0xA1, 0x01,             /* Collection (Application) */
    0x05, 0x07,             /*   Usage Page (Key Codes) */
    0x19, 0xE0,             /*   Usage Minimum (0xE0 = LeftCtrl) */
    0x29, 0xE7,             /*   Usage Maximum (0xE7 = Right GUI) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x25, 0x01,             /*   Logical Maximum (1) */
    0x75, 0x01,             /*   Report Size (1 bit) */
    0x95, 0x08,             /*   Report Count (8 buttons – modifiers + 6 keys) */
    0x81, 0x02,             /*   Input (Data, Array) – modifiers */
    0x95, 0x01,             /*   Report Count (1) */
    0x75, 0x08,             /*   Report Size (8 bits) */
    0x81, 0x01,             /*   Input (Constant) – reserved */
    0x95, 0x06,             /*   Report Count (6) */
    0x75, 0x08,             /*   Report Size (8 bits) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x25, 0x65,             /*   Logical Maximum (101) */
    0x19, 0x00,             /*   Usage Minimum (0x00) */
    0x29, 0x65,             /*   Usage Maximum (0x65 = Keyboard *? actually 0x65 = 'e' in usage table) */
    0x81, 0x00,             /*   Input (Data, Array) – 6 key codes */
    0xC0                    /* End Collection */
};

/* Endpoint descriptor – interrupt IN, 8 bytes, interval 10ms */
static const uint8_t ep_desc[] = {
    7,                      /* bLength */
    0x05,                   /* bDescriptorType (ENDPOINT) */
    0x81,                   /* bEndpointAddr (IN) */
    0x03,                   /* bmAttributes (Interrupt) */
    8, 0x00,                /* wMaxPacketSize (8) */
    0x0A                    /* bInterval (10 ms) */
};

/* ---------------------------------------------------------------------- */
static uint8_t usb_buf[256];
static uint8_t config_set = 0;
static uint8_t usb_kbd_report[8] = {0};
static bool usb_kbd_initialized = false;

/* usb_control – simple wrapper around the xHCI/port‑I/O writes; for now a
   no‑op that just returns success (0).  Real implementation would encode
   SETUP packet and ship it over the xHCI ring. */
static int usb_control(uint8_t bmRequestType, uint8_t bRequest,
                       uint16_t wValue, uint16_t wIndex,
                       uint16_t wLength, uint8_t *data)
{
    (void)bmRequestType; (void)bRequest;
    (void)wValue; (void)wIndex; (void)wLength;
    (void)data;
    return 0;   /* pretend success – real code would descriptor fetch etc. */
}

/* ---------------------------------------------------------------------- */
void usb_init(void)
{
    kprint("[usb] USB core inicializado.\n");
}

/* ---------------------------------------------------------------------- */
/* Enumeraçao: procura um controlador xHCI no PCI e tenta enumerar um teclado
   HID de verdade (Address Device + descriptors + Configure Endpoint + leitura
   do report por polling no transfer ring do xhci.c). Retorna 1 apenas se o
   teclado real foi reconhecido (sem modo simulacao). */
int usb_enumerate_hid_keyboard(void)
{
    uint64_t bar = usb_xhci_bar();
    if(!bar){
        kprint("[usb] sem xHCI; teclado via PS/2\n");
        return 0;
    }
    char b[160];
    snprintf(b,160,"[usb] controlador xHCI em 0x%llx (mapa identidade)\n", (unsigned long long)bar);
    kprint(b);
    int r = xhci_init(bar);
    if(r!=0){
        snprintf(b,160,"[usb] xHCI init falhou (r=%d); teclado via PS/2\n", r);
        kprint(b);
        return 0;
    }
    int slot = xhci_enumerate();
    if(slot<=0){
        snprintf(b,160,"[usb] xHCI sem teclado (slot=%d); teclado via PS/2\n", slot);
        kprint(b);
        return 0;
    }
    config_set = 1;
    snprintf(b,160,"[usb] teclado HID no slot %d\n", slot);
    kprint(b);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Read the current USB keyboard report (8 bytes: 1 byte modifiers + 6 key
   codes).  The caller must ensure a report is available (e.g. after an
   interrupt).  Returns the number of bytes read, or <0 on error. */
int usb_kbd_read_report(uint8_t *report)
{
    if (!report) return -1;
    if (xhci_kbd_present_p())
        return xhci_kbd_read_hid(report, 8);
    if (!config_set) return -1;
    /* Modo dev/simulacao (sem xHCI real): report estatico via usb_kbd_set_report. */
    if (!usb_kbd_initialized) return -1;
    memcpy(report, usb_kbd_report, 8);
    return 8;
}

/* Set the USB keyboard report bytes (used by a real ISR or for testing). */
void usb_kbd_set_report(const uint8_t *report, uint8_t len)
{
    if (!report) return;
    if (len > 8) len = 8;
    memcpy(usb_kbd_report, report, len);
    usb_kbd_initialized = true;
}

/* ---------------------------------------------------------------------- */
void usb_deinit(void)
{
    config_set = 0;
    kprint("[usb] desligado.\n");
}

/* ---- IRQ handler simulado para o controlador xHCI (IRQ 12) ----
   Em um ambiente real este handler seria invocado pelo controlador xHCI
   quando houver um novo report disponível no endpoint de interrupcao IN.
   Aqui apenas avança um relatório estatico para testes. */
static uint8_t usb_irq_report_idx = 0;
static const uint8_t usb_irq_reports[6][8] = {
    {0, 0x04, 0,0,0,0,0,0}, /* keycode 0x04 = '3' */
    {0, 0x1E, 0,0,0,0,0,0}, /* keycode 0x1E = 's' */
    {0, 0x20, 0,0,0,0,0,0}, /* keycode 0x20 = 'f' */
    {0, 0x17, 0,0,0,0,0,0}, /* keycode 0x17 = 'u' */
    {0, 0x22, 0,0,0,0,0,0}, /* keycode 0x22 = 'h' */
    {0, 0,0,0,0,0,0,0}      /* relatorio vazio */
};

void usb_irq_handler(void){
    usb_irq_report_idx = (usb_irq_report_idx + 1) % 6;
    usb_kbd_set_report(usb_irq_reports[usb_irq_report_idx], 8);
}