#include "thais.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * HID parser for a standard USB keyboard (6‑key + modifiers report).
 * The report format (from the HID descriptor in usb.c):
 *   Byte 0 : modifiers (bit 0 = LeftCtrl, 1 = LeftShift, 2 = LeftAlt,
                3 = Left GUI, 4‑7 reserved)
 *   Bytes 1‑6 : key codes (0 = empty)
 *
 * We map usage codes (0x04‑0x65) to ASCII characters for the US QWERTY layout.
 * --------------------------------------------------------------------------- */

/* Map from USB Usage Page (0x07 = Key Codes) + Usage code to ASCII.
   Only a subset is included; the rest map to 0 (ignored). */
static const uint8_t usb_key_to_ascii[128] = {
    /* usage 0x00 */ 0,
    /* 0x01 */ 0, 0,         /* 0x01 = Error rollover, not a key – treat as 0 */
    /* 0x02 */ '1',          /* 0x02 */
    /* 0x03 */ '2',          /* 0x03 */
    /* 0x04 */ '3',          /* 0x04 */
    /* 0x05 */ '4',          /* 0x05 */
    /* 0x06 */ '5',          /* 0x06 */
    /* 0x07 */ '6',          /* 0x07 */
    /* 0x08 */ '7',          /* 0x08 */
    /* 0x09 */ '8',          /* 0x09 */
    /* 0x0A */ '9',          /* 0x0A */
    /* 0x0B */ '0',          /* 0x0B */
    /* 0x0C */ '-',          /* 0x0C (minus) */
    /* 0x0D */ '=',          /* 0x0D */
    /* 0x0E */ '\b',         /* 0x0E Backspace – we treat specially */
    /* 0x0F */ 'q',          /* 0x0F */
    /* 0x10 */ 'w',          /* 0x10 */
    /* 0x11 */ 'e',          /* 0x11 */
    /* 0x12 */ 'r',          /* 0x12 */
    /* 0x13 */ 't',          /* 0x13 */
    /* 0x14 */ 'y',          /* 0x14 */
    /* 0x15 */ 'u',          /* 0x15 */
    /* 0x16 */ 'i',          /* 0x16 */
    /* 0x17 */ 'o',          /* 0x17 */
    /* 0x18 */ 'p',          /* 0x18 */
    /* 0x19 */ '[',          /* 0x19 */
    /* 0x1A */ ']',          /* 0x1A */
    /* 0x1B */ '\n',         /* 0x1B Enter */
    /* 0x1C */ 0,            /* 0x1C Left Control – modifier, not a key code used here */
    /* 0x1D */ 'a',          /* 0x1D */
    /* 0x1E */ 's',          /* 0x1E */
    /* 0x1F */ 'd',          /* 0x1F */
    /* 0x20 */ 'f',          /* 0x20 */
    /* 0x21 */ 'g',          /* 0x21 */
    /* 0x22 */ 'h',          /* 0x22 */
    /* 0x23 */ 'j',          /* 0x23 */
    /* 0x24 */ 'k',          /* 0x24 */
    /* 0x25 */ 'l',          /* 0x25 */
    /* 0x26 */ ';',          /* 0x26 */
    /* 0x27 */ '\'',         /* 0x27 */
    /* 0x28 */ '`',         /* 0x28 */
    /* 0x29 */ 0,            /* 0x29 */
    /* 0x2A */ '\\',         /* 0x2A */
    /* 0x2B */ 'z',          /* 0x2B */
    /* 0x2C */ 'x',          /* 0x2C */
    /* 0x2D */ 'c',          /* 0x2D */
    /* 0x2E */ 'v',          /* 0x2E */
    /* 0x2F */ 'b',          /* 0x2F */
    /* 0x30 */ 'n',          /* 0x30 */
    /* 0x31 */ 'm',          /* 0x31 */
    /* 0x32 */ ',',          /* 0x32 */
    /* 0x33 */ '.',          /* 0x33 */
    /* 0x34 */ '/',          /* 0x34 */
    /* 0x35 */ 0,            /* 0x35 – extra */
    /* 0x36 */ '*',          /* 0x36 Right Alt – treated as special */
    /* 0x37 */ ' ',          /* 0x37 Space */
    /* 0x38 */ '<',           /* 0x38 – some layouts; we map to 0 */
    /* 0x39 */ '>',           /* 0x39 */
    /* 0x3A */ '?',          /* 0x3A */
    /* 0x3B */ 0,            /* 0x3B */
    /* 0x3C */ 0,            /* 0x3C */
    /* 0x3D */ 0,            /* 0x3D */
    /* 0x3E */ 0,            /* 0x3E */
    /* 0x3F */ 0,            /* 0x3F */
    /* 0x40 */ 0,            /* 0x40 */
    /* 0x41 */ 0,            /* 0x41 */
    /* 0x42 */ 0,            /* 0x42 */
    /* 0x43 */ 0,            /* 0x43 */
    /* 0x44 */ 0,            /* 0x44 */
    /* 0x45 */ 0,            /* 0x45 */
    /* 0x46 */ 0,            /* 0x46 */
    /* 0x47 */ 0,            /* 0x47 */
    /* 0x48 */ 0,            /* 0x48 */
    /* 0x49 */ 0,            /* 0x49 */
    /* 0x4A */ 0,            /* 0x4A */
    /* 0x4B */ 0,            /* 0x4B */
    /* 0x4C */ 0,            /* 0x4C */
    /* 0x4D */ 0,            /* 0x4D */
    /* 0x4E */ 0,            /* 0x4E */
    /* 0x4F */ 0,            /* 0x4F */
    /* 0x50 */ 0,            /* 0x50 */
    /* 0x51 */ 0,            /* 0x51 */
    /* 0x52 */ 0,            /* 0x52 */
    /* 0x53 */ 0,            /* 0x53 */
    /* 0x54 */ 0,            /* 0x54 */
    /* 0x55 */ 0,            /* 0x55 */
    /* 0x56 */ 0,            /* 0x56 */
    /* 0x57 */ 0,            /* 0x57 */
    /* 0x58 */ 0,            /* 0x58 */
    /* 0x59 */ 0,            /* 0x59 */
    /* 0x5A */ 0,            /* 0x5A */
    /* 0x5B */ 0,            /* 0x5B */
    /* 0x5C */ 0,            /* 0x5C */
    /* 0x5D */ 0,            /* 0x5D */
    /* 0x5E */ 0,            /* 0x5E */
    /* 0x5F */ 0,            /* 0x5F */
    /* 0x60 */ 0,            /* 0x60 */
    /* 0x61 */ 0,            /* 0x61 */
    /* 0x62 */ 0,            /* 0x62 */
    /* 0x63 */ 0,            /* 0x63 */
    /* 0x64 */ 0,            /* 0x64 */
    /* 0x65 */ 0,            /* 0x65 */
};

/* Helper: convert a single 8‑bit key code (0‑0x65) to ASCII, considering
   the current modifier state (shift).  For the most common keys we supply
   shifted characters; for the rest we just return the base value. */
static char usb_keycode_to_ascii(uint8_t keycode, bool shift)
{
    if (keycode == 0) return 0;
    if (keycode > 0x65) return 0;
    char base = usb_key_to_ascii[keycode];
    if (base == 0) return 0;
    if (!shift) return base;
    /* Shift mapping for the most common keys – this is a small subset;
       a full layout would have a complete shifted table. */
    switch (base) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case '`': return '~';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default:
        /* toggle case for alphabetic characters */
        if (base >= 'a' && base <= 'z') return base - 'a' + 'A';
        if (base >= 'A' && base <= 'Z') return base - 'A' + 'a';
        return base;
    }
}

/* ---------------------------------------------------------------------------
 * Parse an 8‑byte USB keyboard report and return the first printable character,
   or 0 if none.  The report layout is:
   byte 0 = modifiers (we ignore most)
   bytes 1‑6 = key codes (usage 0x00‑0x65)
   --------------------------------------------------------------------------- */
int usb_kbd_parse_report(const uint8_t *report, char *out_char)
{
    if (!report || !out_char) return -1;

    /* modifiers byte – bit 1 = Left Shift */
    bool shift = (report[0] & (1 << 1)) != 0;

    /* check the six key codes, left‑to‑right; first non‑zero wins */
    int i;
    for (i = 1; i <= 6; i++) {
        char c = usb_keycode_to_ascii(report[i], shift);
        if (c != 0) {
            *out_char = c;
            return 0;
        }
    }
    *out_char = 0;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Exported interface used by the terminal/console.
 * Report USB e numero 8 bytes (mods + 6 esta no naipe "held"). Como o report
 * e por estado (level-triggered), fazemos deteccao de borda: so emite uma
 * tecla quando um usage NOVO aparece; teclas seguras/ainda pressionadas e as
 * release (report vazio) nao geram caractere.
 * --------------------------------------------------------------------------- */
int usb_kbd_getc(char *c)
{
    static uint8_t prev[7] = {0};   /* ultimos 6 key codes vistos */
    uint8_t report[8];
    int r = usb_kbd_read_report(report);
    if (r < 0) return -1;
    if (r > 8) r = 8;
    if (r < 1) return -1;

    bool shift = (report[0] & (1 << 1)) != 0;
    int i, j;
    for (i = 1; i <= 6; i++) {
        uint8_t k = report[i];
        if (k == 0) continue;
        for (j = 1; j <= 6; j++) if (prev[j] == k) break;
        if (j <= 6) continue;               /* ja estava pressionada */
        char ch = usb_keycode_to_ascii(k, shift);
        memcpy(prev, &report[1], 6);
        if (ch == 0) { *c = 0; return 0; }  /* nova tecla nao mapeada: engole */
        *c = ch;
        return 0;
    }
    memcpy(prev, &report[1], 6);
    *c = 0;
    return 0;
}