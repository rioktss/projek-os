#pragma once
#include <stddef.h>

void env_init(void);
const char *env_get(const char *name);
int env_set(const char *name, const char *value);
int env_unset(const char *name);
int env_slot_count(void);
const char *env_name_at(int index);
const char *env_value_at(int index);
void env_expand(const char *src, char *dst, size_t size);
