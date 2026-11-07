/*
 * Log to stderr and to <program>.log next to the executable (the app manager
 * starts apps with an arbitrary working directory). The examples keep stderr
 * working with lgnc_stdio.h, see there for why printf & co. stop working.
 */
#pragma once

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int nc4log_fd = -1;

static inline void nc4log_open(const char *argv0)
{
    char exe[PATH_MAX], path[PATH_MAX + 8];
    if (realpath(argv0, exe) == NULL)
        return;
    snprintf(path, sizeof(path), "%s.log", exe);
    nc4log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

__attribute__((format(printf, 1, 2)))
static inline void nc4log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int) sizeof(buf) - 2)
        n = sizeof(buf) - 2;
    if (n == 0 || buf[n - 1] != '\n')
        buf[n++] = '\n';
    if (write(STDERR_FILENO, buf, n) < 0) {
        /* nothing to do */
    }
    if (nc4log_fd >= 0 && write(nc4log_fd, buf, n) < 0) {
        /* nothing to do */
    }
}
