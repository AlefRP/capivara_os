---
name: publicar-versao
description: Publica uma versão do Capivara OS no GitHub (Release com a ISO pronta para baixar e gravar em pendrive), via tag vX.Y.Z e o workflow release.yml. Use só quando o usuário pedir para lançar, publicar ou soltar uma versão.
---

# Publicar uma versão

Publicar é uma ação **pública e difícil de desfazer**: a Release fica visível
para todo mundo. Só faça isso quando o usuário pedir, e confirme o número da
versão com ele antes de criar a tag.

## Passos

1. **CI verde em `main`.** Confira a última execução do workflow `CI` (aba
   Actions do repositório, ou `gh run list --workflow ci.yml --limit 1` se o
   `gh` estiver logado).
2. **Número da versão** em `kernel/version.h` (`CAPIVARA_VERSION`). Ele aparece
   em `uname -a`, no `sysinfo`, em Configurações › Sobre e no "Sobre" de cada
   app. Use versionamento semântico:
   - correção → `0.1.1`;
   - recurso novo → `0.2.0`.
3. Faça commit da mudança de versão: `Versão 0.2.0`.
4. Crie e envie a tag, com o mesmo número e um `v` na frente:

   ```sh
   git tag -a v0.2.0 -m "Capivara OS 0.2.0"
   git push origin main v0.2.0
   ```

5. O workflow `.github/workflows/release.yml` faz o resto:
   - confere que a tag bate com `kernel/version.h` (se não bater, ele falha);
   - compila a ISO;
   - testa o boot como pendrive (UEFI) e como CD (BIOS);
   - publica a Release com `capivara-os-v0.2.0.iso`, o `.sha256` e as notas
     geradas a partir dos commits.
6. Mande ao usuário o link da Release:
   `https://github.com/AlefRP/capivara_os/releases/tag/v0.2.0`.

## Se der errado

- Se a tag não bater com a versão, o workflow falha antes de publicar. Apague a
  tag (`git push --delete origin v0.2.0` e `git tag -d v0.2.0`), corrija e
  repita.
- Se o teste de boot falhar, nada é publicado. Baixe o artefato
  `telas-release` da execução, olhe as capturas e o `serial.log`, e corrija.
