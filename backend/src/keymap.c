/* RDP keyboard input to Linux: scancodes (PC set 1, E0-extended) to evdev
 * keycodes, and Windows keyboard layout ids to xkb layouts. */
#include <linux/input-event-codes.h>
#include <stddef.h>

#include "backend.h"

uint32_t rdp_scancode_to_evdev(int scancode, bool extended)
{
    if (!extended) {
        /* Set 1 scancodes 0x01-0x58 are numbered like evdev keycodes */
        if (scancode >= 0x01 && scancode <= 0x58)
            return scancode;
        switch (scancode) {
        case 0x59: return KEY_KPEQUAL;
        case 0x5c: return KEY_KPJPCOMMA;
        case 0x64: return KEY_F13;
        case 0x65: return KEY_F14;
        case 0x66: return KEY_F15;
        case 0x67: return KEY_F16;
        case 0x68: return KEY_F17;
        case 0x69: return KEY_F18;
        case 0x6a: return KEY_F19;
        case 0x6b: return KEY_F20;
        case 0x6c: return KEY_F21;
        case 0x6d: return KEY_F22;
        case 0x6e: return KEY_F23;
        case 0x70: return KEY_KATAKANAHIRAGANA;
        case 0x73: return KEY_RO;
        case 0x76: return KEY_F24;
        case 0x79: return KEY_HENKAN;
        case 0x7b: return KEY_MUHENKAN;
        case 0x7d: return KEY_YEN;
        case 0x7e: return KEY_KPCOMMA;
        case 0xf1: return KEY_HANJA;
        case 0xf2: return KEY_HANGEUL;
        }
        return 0;
    }

    switch (scancode) {
    case 0x10: return KEY_PREVIOUSSONG;
    case 0x19: return KEY_NEXTSONG;
    case 0x1c: return KEY_KPENTER;
    case 0x1d: return KEY_RIGHTCTRL;
    case 0x20: return KEY_MUTE;
    case 0x21: return KEY_CALC;
    case 0x22: return KEY_PLAYPAUSE;
    case 0x24: return KEY_STOPCD;
    case 0x2e: return KEY_VOLUMEDOWN;
    case 0x30: return KEY_VOLUMEUP;
    case 0x32: return KEY_HOMEPAGE;
    case 0x35: return KEY_KPSLASH;
    case 0x37: return KEY_SYSRQ;
    case 0x38: return KEY_RIGHTALT;
    case 0x46: return KEY_PAUSE; /* Ctrl+Break */
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PAGEUP;
    case 0x4b: return KEY_LEFT;
    case 0x4d: return KEY_RIGHT;
    case 0x4f: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PAGEDOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x5b: return KEY_LEFTMETA;
    case 0x5c: return KEY_RIGHTMETA;
    case 0x5d: return KEY_COMPOSE;
    case 0x5e: return KEY_POWER;
    case 0x5f: return KEY_SLEEP;
    case 0x63: return KEY_WAKEUP;
    case 0x65: return KEY_SEARCH;
    case 0x66: return KEY_BOOKMARKS;
    case 0x67: return KEY_REFRESH;
    case 0x68: return KEY_STOP;
    case 0x69: return KEY_FORWARD;
    case 0x6a: return KEY_BACK;
    case 0x6b: return KEY_COMPUTER;
    case 0x6c: return KEY_MAIL;
    case 0x6d: return KEY_MEDIA;
    }
    return 0;
}

static const struct {
    uint32_t id;
    const char *layout;
    const char *variant;
} layouts[] = {
    {0x00000401, "ara", NULL},     {0x00000405, "cz", NULL},     {0x00000406, "dk", NULL},
    {0x00000407, "de", NULL},      {0x00000408, "gr", NULL},     {0x00000409, "us", NULL},
    {0x0000040a, "es", NULL},      {0x0000040b, "fi", NULL},     {0x0000040c, "fr", NULL},
    {0x0000040d, "il", NULL},      {0x0000040e, "hu", NULL},     {0x0000040f, "is", NULL},
    {0x00000410, "it", NULL},      {0x00000411, "jp", NULL},     {0x00000412, "kr", NULL},
    {0x00000413, "nl", NULL},      {0x00000414, "no", NULL},     {0x00000415, "pl", NULL},
    {0x00000416, "br", NULL},      {0x00000418, "ro", NULL},     {0x00000419, "ru", NULL},
    {0x0000041a, "hr", NULL},      {0x0000041b, "sk", NULL},     {0x0000041d, "se", NULL},
    {0x0000041f, "tr", NULL},      {0x00000422, "ua", NULL},     {0x00000424, "si", NULL},
    {0x00000425, "ee", NULL},      {0x00000426, "lv", NULL},     {0x00000427, "lt", NULL},
    {0x00000807, "ch", NULL},      {0x00000809, "gb", NULL},     {0x0000080a, "latam", NULL},
    {0x0000080c, "be", NULL},      {0x00000813, "be", NULL},     {0x00000816, "pt", NULL},
    {0x00000c0a, "es", NULL},      {0x00000c0c, "ca", "fr"},     {0x00001009, "ca", NULL},
    {0x0000100c, "ch", "fr"},      {0x00010409, "us", "dvorak"}, {0x00020409, "us", "intl"},
    {0x00010419, "ru", "typewriter"},
};

const char *rdp_layout_to_xkb(uint32_t keylayout, const char **variant)
{
    *variant = NULL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        if (layouts[i].id == keylayout) {
            *variant = layouts[i].variant;
            return layouts[i].layout;
        }
    }
    /* Unknown: match on the language id alone */
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        if ((layouts[i].id & 0xffff) == (keylayout & 0xffff) && !layouts[i].variant)
            return layouts[i].layout;
    }
    return NULL;
}
