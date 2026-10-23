/*
 * Central de notificações: uma lista circular protegida por irq_save (pode ser
 * usada por qualquer tarefa). Ao chegar uma nova, o shell é avisado para
 * mostrar o "toast" no canto da tela.
 */
#include "apps/notify.h"
#include "gui/wm.h"
#include "dev/rtc.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"

static notification_t list[NOTIFY_MAX];
static int count, next_id = 1;

void notify(const char *app, const char *title, const char *body, int icon, const char *path) {
    notification_t n;
    memset(&n, 0, sizeof(n));
    strlcpy(n.app, app ? app : "Sistema", sizeof(n.app));
    strlcpy(n.title, title ? title : "", sizeof(n.title));
    strlcpy(n.body, body ? body : "", sizeof(n.body));
    strlcpy(n.path, path ? path : "", sizeof(n.path));
    n.icon = icon;
    n.time = time_now();

    uint64_t f = irq_save();
    n.id = next_id++;
    if (count == NOTIFY_MAX) {
        memmove(&list[0], &list[1], sizeof(list[0]) * (NOTIFY_MAX - 1));
        count--;
    }
    list[count++] = n;
    irq_restore(f);
    wm_shell_command(SHELL_NOTIFY);
}

int notify_list(notification_t *out, int max) {
    uint64_t f = irq_save();
    int n = 0;
    for (int i = count - 1; i >= 0 && n < max; i--) out[n++] = list[i];
    irq_restore(f);
    return n;
}

bool notify_next_toast(notification_t *out) {
    uint64_t f = irq_save();
    for (int i = 0; i < count; i++) {
        if (!list[i].shown) {
            list[i].shown = true;
            *out = list[i];
            irq_restore(f);
            return true;
        }
    }
    irq_restore(f);
    return false;
}

void notify_dismiss(int id) {
    uint64_t f = irq_save();
    for (int i = 0; i < count; i++) {
        if (list[i].id != id) continue;
        memmove(&list[i], &list[i + 1], sizeof(list[0]) * (count - i - 1));
        count--;
        break;
    }
    irq_restore(f);
}

void notify_clear(void) {
    uint64_t f = irq_save();
    count = 0;
    irq_restore(f);
}

int notify_count(void) { return count; }
