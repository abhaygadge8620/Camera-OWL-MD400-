#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <signal.h>
#include <stdint.h>

int platform_install_signal_handlers(volatile sig_atomic_t *run_flag);
int platform_strcasecmp(const char *lhs, const char *rhs);
void platform_sleep_ms(unsigned int ms);
uint64_t platform_monotonic_ms(void);

#endif
