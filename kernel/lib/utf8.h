/* UTF-8: todo texto do sistema usa essa codificação (acentos ocupam 2 bytes). */
#pragma once
#include <stdint.h>
#include <stddef.h>

uint32_t utf8_decode(const char **s);                 /* lê um caractere e avança */
int      utf8_encode(uint32_t cp, char out[4]);       /* devolve nº de bytes */
size_t   utf8_prev(const char *s, size_t pos);        /* início do caractere anterior */
size_t   utf8_next(const char *s, size_t len, size_t pos);
size_t   utf8_count(const char *s, size_t bytes);     /* nº de caracteres */
