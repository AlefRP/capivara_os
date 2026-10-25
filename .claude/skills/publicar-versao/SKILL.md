---
name: publicar-versao
description: Como as versões do Capivara OS chegam ao GitHub (cada commit em main vira uma Release vX.Y.N com a ISO, pelo workflow release.yml), como começar uma série nova (0.2, 1.0) e o que fazer quando uma Release não sai. Use quando o usuário perguntar de versões, releases ou tags, ou pedir para lançar uma versão "grande".
---

# Versões e Releases

**Todo commit enviado para `main` vira uma versão pública.** Quando o workflow
`CI` termina verde num push para `main`, o `.github/workflows/release.yml`:

1. calcula o número `vX.Y.N`: X.Y é `CAPIVARA_SERIE` em `kernel/version.h`
   e N é o número do commit (`git rev-list --count HEAD`);
2. compila a ISO com `VERSAO=X.Y.N` (é esse número que aparece em `uname -a`,
   no `sysinfo`, em Configurações › Sobre e no "Sobre" de cada app);
3. testa o boot como pendrive (UEFI) e como CD (BIOS);
4. cria a tag e a Release com `capivara-os-vX.Y.N.iso`, o `.sha256` e a
   lista de commits desde a Release anterior.

Se vários commits chegam num push só, sai uma Release só, a do último.
Pull requests não publicam nada. Num build local, sem `VERSAO`, o sistema se
apresenta como `X.Y-dev`.

Como o push já publica, **só faça `git push` para `main` quando o usuário
pedir**, e avise que vai sair uma Release.

## Começar uma série nova

Para um recurso grande, mude a série em `kernel/version.h`
(`#define CAPIVARA_SERIE "0.2"`) e faça commit: `Série 0.2`. O N continua
contando os commits, então a primeira versão da série nova é algo como
`v0.2.58`. Confirme o número da série com o usuário antes.

## Publicar de novo, à mão

Na aba Actions, workflow `Release`, botão *Run workflow* (ou
`gh workflow run release.yml`, se o `gh` estiver logado). Ele publica o
último commit de `main`; se a Release desse commit já existe, não faz nada.

## Se der errado

- O CI falhou: nada é publicado. Corrija e envie outro commit.
- O teste de boot do release falhou: nada é publicado. Baixe o artefato
  `telas-release` da execução, olhe as capturas e o `serial.log`, e corrija.
- Para apagar uma Release (só se o usuário pedir):
  `gh release delete vX.Y.N --cleanup-tag`.
- Link das versões: `https://github.com/AlefRP/capivara_os/releases`.
