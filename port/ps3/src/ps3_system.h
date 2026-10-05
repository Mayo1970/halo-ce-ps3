/* PS3 system glue: UDP log, XMB exit, free memory, time and the data root. */

#ifndef __HALO_PS3_SYSTEM_H
#define __HALO_PS3_SYSTEM_H

#include <stdarg.h>

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

double ps3_time_seconds(void);

/* the folder with maps/ (Xbox d:\), on the HDD or a USB drive */
const char *ps3_data_root(void);

#endif
