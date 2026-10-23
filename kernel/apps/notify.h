/* Notificações do sistema: qualquer tarefa pode avisar o usuário. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int id;
    char app[32];
    char title[64];
    char body[160];
    int icon;
    char path[256];        /* abrir ao clicar (opcional) */
    uint64_t time;
    bool shown;            /* já apareceu como "toast"? */
} notification_t;

#define NOTIFY_MAX 16

void notify(const char *app, const char *title, const char *body, int icon, const char *path);
int  notify_list(notification_t *out, int max);    /* mais recentes primeiro */
bool notify_next_toast(notification_t *out);       /* próxima ainda não exibida */
void notify_dismiss(int id);
void notify_clear(void);
int  notify_count(void);
