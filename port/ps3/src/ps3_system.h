/* PS3 system glue: UDP log, XMB exit, free memory, time and the data root. */

#ifndef __HALO_PS3_SYSTEM_H
#define __HALO_PS3_SYSTEM_H

#include <stdarg.h>
#include <stdint.h>

void ps3_system_init(void);
void ps3_system_shutdown(void);

/* UDP datagrams to PS3_LOG_IP:18194 (nc -ul 18194 on the PC) */
int ps3_log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int ps3_log_vprintf(const char *format, va_list ap);

/* nonzero once the XMB asked the game to quit */
int ps3_exit_requested(void);

/* largest lv2 memory container that fits, in MB: the free user memory */
unsigned ps3_memory_free_mb(void);
void ps3_memory_report(const char *label);
/* lv2's user memory in bytes (syscall 352); it reserves nothing, so unlike the probe any thread can
call it at any time. 0 on success */
int ps3_memory_user(uint32_t *total, uint32_t *available);
/* the free user memory at its lowest: the display thread samples it (ps3_main.c) */
void ps3_memory_sample(void);
void ps3_memory_lowest(uint32_t *bytes, double *seconds);

double ps3_time_seconds(void);

/* the folder with maps/ (Xbox d:\), on the HDD or a USB drive */
const char *ps3_data_root(void);

#endif
