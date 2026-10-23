/*
 * Sessão do usuário.
 *
 * A senha nunca é guardada em texto: guardamos um "sal" aleatório e o hash
 * (FNV-1a de 64 bits, repetido várias vezes) de sal + senha. Para conferir,
 * calculamos o hash da tentativa e comparamos. Um sistema de verdade usaria
 * um algoritmo feito para senhas (bcrypt, scrypt, Argon2), mas a ideia é a mesma.
 */
#include "apps/session.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"

static char user[32] = "Usuário";
static uint64_t salt, hash;
static bool has_password;

static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static uint64_t hash_password(uint64_t s, const char *pw) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (int round = 0; round < 1000; round++) {
        h = fnv(h, &s, sizeof(s));
        h = fnv(h, pw, strlen(pw));
    }
    return h;
}

const char *session_user(void) { return user; }

void session_set_user(const char *name) {
    if (name && *name) strlcpy(user, name, sizeof(user));
}

bool session_has_password(void) { return has_password; }

bool session_check_password(const char *pw) {
    return !has_password || hash_password(salt, pw) == hash;
}

void session_set_password(const char *pw) {
    if (!pw || !*pw) {
        has_password = false;
        return;
    }
    salt = rdtsc() * 0x9E3779B97F4A7C15ULL;     /* "aleatório" o bastante aqui */
    hash = hash_password(salt, pw);
    has_password = true;
}
