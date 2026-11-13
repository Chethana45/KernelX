#include <drivers/vga.h>
#include <arch/x86/io.h>

#define VGA_MEMORY ((uint16_t*)0xB8000)
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static size_t vga_row = 0;
static size_t vga_column = 0;
static uint8_t vga_color = 0;
static uint16_t* const vga_buffer = VGA_MEMORY;

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xC0) | cursor_start);

    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, (inb(VGA_CRTC_DATA) & 0xE0) | cursor_end);
}

void vga_disable_cursor(void) {
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, 0x20);
}

void vga_set_cursor(size_t x, size_t y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);

    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_color);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = blank;
        }
    }
    vga_row = 0;
    vga_column = 0;
    vga_set_cursor(vga_column, vga_row);
}

void vga_init(void) {
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
    vga_enable_cursor(14, 15);
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    vga_color = vga_entry_color(fg, bg);
}

static void vga_scroll(void) {
    uint16_t blank = vga_entry(' ', vga_color);

    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    vga_row = VGA_HEIGHT - 1;
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_column = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
        }
    } else if (c == '\r') {
        vga_column = 0;
    } else if (c == '\t') {
        /* Advance to next 4-space tabstop */
        size_t next_tab = (vga_column + 4) & ~3;
        while (vga_column < next_tab && vga_column < VGA_WIDTH) {
            vga_buffer[vga_row * VGA_WIDTH + vga_column] = vga_entry(' ', vga_color);
            vga_column++;
        }
        if (vga_column >= VGA_WIDTH) {
            vga_column = 0;
            if (++vga_row == VGA_HEIGHT) {
                vga_scroll();
            }
        }
    } else if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            vga_buffer[vga_row * VGA_WIDTH + vga_column] = vga_entry(' ', vga_color);
        } else if (vga_row > 0) {
            vga_row--;
            vga_column = VGA_WIDTH - 1;
            vga_buffer[vga_row * VGA_WIDTH + vga_column] = vga_entry(' ', vga_color);
        }
    } else {
        const size_t index = vga_row * VGA_WIDTH + vga_column;
        vga_buffer[index] = vga_entry((unsigned char)c, vga_color);
        if (++vga_column == VGA_WIDTH) {
            vga_column = 0;
            if (++vga_row == VGA_HEIGHT) {
                vga_scroll();
            }
        }
    }

    vga_set_cursor(vga_column, vga_row);
}

void vga_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putc(data[i]);
    }
}

void vga_write_string(const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putc(str[i]);
    }
}
