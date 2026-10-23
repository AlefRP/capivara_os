/* Controlador PS/2 (Intel 8042): porta 0x60 = dados, 0x64 = status/comando. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void    ps2_init(void);
bool    ps2_present(void);               /* existe um controlador 8042? */
void    ps2_cmd(uint8_t cmd);
void    ps2_write(uint8_t data);
bool    ps2_read(uint8_t *out);          /* com timeout */
bool    ps2_mouse_cmd(uint8_t data);     /* envia ao mouse e espera ACK (0xFA) */

void keyboard_init(void);
void mouse_init(bool try_vmmouse);
void mouse_feed_byte(uint8_t b);           /* byte do mouse lido em outro handler */
bool mouse_is_absolute(void);
void mouse_set_external_absolute(bool on);
uint8_t mouse_buttons(void);
