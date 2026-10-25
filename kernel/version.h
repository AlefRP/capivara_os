/* Versão do Capivara OS. Cada commit enviado para main vira uma Release
 * vX.Y.N no GitHub (.github/workflows/release.yml): X.Y é a série abaixo e N
 * é o número do commit (git rev-list --count HEAD). O workflow compila com
 * "make VERSAO=X.Y.N"; num build local, sem VERSAO, a versão é "X.Y-dev".
 * Para começar uma série nova (recurso grande), mude CAPIVARA_SERIE. */
#pragma once
#define CAPIVARA_SERIE "0.1"
#ifndef CAPIVARA_VERSION
#define CAPIVARA_VERSION CAPIVARA_SERIE "-dev"
#endif
