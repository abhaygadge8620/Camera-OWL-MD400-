#include "platform_compat.h"

#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t *g_run_flag = NULL;

static void platform_signal_handler(int signo)
{
    (void)signo;
    if (g_run_flag != NULL) {
        *g_run_flag = 0;
    }
}

int platform_install_signal_handlers(volatile sig_atomic_t *run_flag)
{
    struct sigaction sa;

    g_run_flag = run_flag;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = platform_signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGHUP, &sa, NULL) != 0) {
        return -1;
    }
    return 0;
}

int platform_strcasecmp(const char *lhs, const char *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        if (lhs == rhs) {
            return 0;
        }
        return (lhs == NULL) ? -1 : 1;
    }

    return strcasecmp(lhs, rhs);
}

void platform_sleep_ms(unsigned int ms)
{
    struct timespec req;
    struct timespec rem;

    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)((ms % 1000U) * 1000000UL);

    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            break;
        }
        req = rem;
    }
}

uint64_t platform_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }

    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}
