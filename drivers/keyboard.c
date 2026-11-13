#include <drivers/keyboard.h>
#include <arch/x86/isr.h>
#include <arch/x86/pic.h>
#include <arch/x86/io.h>

#define KBD_DATA_PORT 0x60

static char kbd_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t kbd_head = 0;
static volatile size_t kbd_tail = 0;

static volatile bool shift_pressed = false;
static volatile bool caps_lock = false;
static volatile bool ctrl_pressed = false;
static volatile bool alt_pressed = false;
static volatile uint8_t last_scancode = 0;

/* Standard Scan Code Set 1 lookup table (non-shifted) */
static const char scancode_ascii_normal[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,    0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
    0,    0,   0,    0,   0,   0,   0,   0
};

/* Standard Scan Code Set 1 lookup table (shifted) */
static const char scancode_ascii_shifted[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,    0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,
    0,    0,   0,    0,   0,   0,   0,   0
};

static void keyboard_callback(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(KBD_DATA_PORT);
    last_scancode = scancode;

    /* Key release has bit 7 set */
    if (scancode & 0x80) {
        uint8_t released_code = scancode & 0x7F;
        if (released_code == 0x2A || released_code == 0x36) {
            shift_pressed = false;
        } else if (released_code == 0x1D) {
            ctrl_pressed = false;
        } else if (released_code == 0x38) {
            alt_pressed = false;
        }
        return;
    }

    /* Key press */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    } else if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    } else if (scancode == 0x38) {
        alt_pressed = true;
        return;
    } else if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    /* Convert scancode to ASCII */
    if (scancode < 128) {
        char c = 0;
        char norm = scancode_ascii_normal[scancode];
        char shft = scancode_ascii_shifted[scancode];

        if (norm >= 'a' && norm <= 'z') {
            /* Alphabet character: influenced by both Shift and Caps Lock */
            bool uppercase = (shift_pressed ^ caps_lock);
            c = uppercase ? shft : norm;
        } else {
            /* Non-alphabet (numbers/symbols): influenced only by Shift */
            c = shift_pressed ? shft : norm;
        }

        if (c != 0) {
            size_t next_head = (kbd_head + 1) % KEYBOARD_BUFFER_SIZE;
            if (next_head != kbd_tail) {
                kbd_buffer[kbd_head] = c;
                kbd_head = next_head;
            }
        }
    }
}

void keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    shift_pressed = false;
    caps_lock = false;
    ctrl_pressed = false;
    alt_pressed = false;

    /* Register keyboard interrupt on IRQ1 (vector 33) */
    isr_register_handler(IRQ1, keyboard_callback);

    /* Unmask IRQ1 on 8259 PIC */
    pic_clear_mask(1);
}

bool keyboard_has_char(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getchar_nonblock(void) {
    if (kbd_head == kbd_tail) {
        return 0;
    }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

char keyboard_getchar(void) {
    while (!keyboard_has_char()) {
        __asm__ volatile ("hlt");
    }
    return keyboard_getchar_nonblock();
}

uint8_t keyboard_get_last_scancode(void) {
    return last_scancode;
}
