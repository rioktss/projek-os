#pragma once
#include <stddef.h>

void *mem_set(void *dst, int value, size_t n);
void *mem_copy(void *dst, const void *src, size_t n);
void *mem_move(void *dst, const void *src, size_t n);
int mem_cmp(const void *a, const void *b, size_t n);

size_t str_len(const char *s);
int str_cmp(const char *a, const char *b);
int str_ncmp(const char *a, const char *b, size_t n);
int str_casecmp(const char *a, const char *b);
char *str_copy(char *dst, const char *src, size_t size);
char *str_cat(char *dst, const char *src, size_t size);
char *str_chr(const char *s, int c);
char *str_rchr(const char *s, int c);
char *str_str(const char *haystack, const char *needle);
int str_starts_with(const char *s, const char *prefix);
int str_ends_with(const char *s, const char *suffix);
void str_lower(char *s);
void str_upper(char *s);
void str_trim(char *s);
int str_is_space(char c);
int str_is_digit(char c);
int str_to_int(const char *s, int *out);
int str_to_uint(const char *s, unsigned *out);
void int_to_str(int value, char *buf, size_t size);
void uint_to_str(unsigned value, char *buf, size_t size);
int str_glob(const char *pattern, const char *text);
int tokenize(char *line, char **argv, int max_args);
