/* See LICENSE.dwm file for copyright and license details. */
#ifndef SWL_UTIL_H
#define SWL_UTIL_H

#include <stddef.h>

void die(const char *fmt, ...);
[[nodiscard]] void *ecalloc(size_t nmemb, size_t size);
int fd_set_nonblock(int fd);

#endif
