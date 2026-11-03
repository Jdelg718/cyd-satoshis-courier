#pragma once
#include <stdbool.h>
#include <stdint.h>
bool     sd_mount(void);
void     sd_unmount(void);
bool     sd_present(void);
uint64_t sd_capacity_mb(void);
const char *sd_name(void);
// Actually creates, writes, closes and deletes a file. Mounting proves nothing
// about whether writes work - filename rules and read-only media fail later.
bool     sd_writable(void);
