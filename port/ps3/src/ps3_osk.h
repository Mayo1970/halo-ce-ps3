/* PS3_OSK.H: the system's on-screen keyboard (sysutil OSK) for one line of text: text chat's SELECT and CROSS
(xinput_ps3.c). Fixed-width types only: the Xbox layer calls it with the game's long. */

#ifndef __HALO_PS3_OSK_H
#define __HALO_PS3_OSK_H

#include <stdint.h>

/* the keyboard opened over the game with its title, for at most maximum_length characters: 1 if it opens */
int ps3_osk_open(const char *title, int maximum_length);
/* the keyboard is up (opening, open or closing) */
int ps3_osk_active(void);
/* the line entered, once the keyboard has closed with it: 1 with it (printable ASCII, NUL-terminated), once */
int ps3_osk_take_text(char *text, int size);
/* the system's OSK events (ps3_system.c's sysutil callback) */
void ps3_osk_event(uint64_t status);

#endif
