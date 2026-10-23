/* Sessão do usuário: nome e senha (para a tela de bloqueio). */
#pragma once
#include <stdbool.h>

const char *session_user(void);
void        session_set_user(const char *name);
bool        session_has_password(void);
bool        session_check_password(const char *pw);
void        session_set_password(const char *pw);     /* "" remove a senha */
