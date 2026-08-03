/*
 * FalconOS Boot Header
 * Boot initialization functions and structures
 */

#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

// Boot initialization
int boot_init();
int cpu_init();
int console_init();
void console_print(const char* msg);

// Boot stages
extern int boot_stage;

#endif // BOOT_H
