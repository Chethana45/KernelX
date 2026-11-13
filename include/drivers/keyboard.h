#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <kernel/types.h>

#define KEYBOARD_BUFFER_SIZE 256

void    keyboard_init(void);
bool    keyboard_has_char(void);
char    keyboard_getchar(void);
char    keyboard_getchar_nonblock(void);
uint8_t keyboard_get_last_scancode(void);

#endif /* DRIVERS_KEYBOARD_H */
