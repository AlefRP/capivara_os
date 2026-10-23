/* Funções de string/memória: o kernel não tem libc, então escrevemos as nossas. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void  memset32(uint32_t *dst, uint32_t v, size_t count);
int   memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
char  *strcpy(char *dst, const char *src);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
const char *strcasestr_utf8(const char *h, const char *n);   /* ignora maiúsc./acentos */
char  *kstrdup(const char *s);

static inline bool isdigit(int c) { return c >= '0' && c <= '9'; }
static inline bool isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static inline bool isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline bool isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int  tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static inline int  toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

long strtol(const char *s, const char **end, int base);
