#include "kprint.h"
#include <sys/syscall.h>
#include <unistd.h>

/**
 * kprint.cpp
 * Writes a null-terminated string to stdout using raw SYS_write.
 * Computes length manually — no strlen, no stdio.
 */
void kprint(const char *s) {
    unsigned long len = 0;
    while (s[len]) len++;
    syscall(SYS_write, 1, s, len);
}
