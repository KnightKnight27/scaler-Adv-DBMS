#pragma once

/**
 * kprint.h
 * Minimal stdout writer using raw write syscall (fd=1).
 * No printf, no cout — zero standard library I/O.
 */

void kprint(const char *s);
