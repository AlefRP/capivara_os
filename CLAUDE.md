# Capivara OS: instruções para o Claude

Sistema operacional didático de 64 bits **escrito do zero** em C e assembly,
com kernel próprio, interface no estilo do macOS e recursos do Windows. Não é
baseado em Linux. Visão geral no [README](README.md); funcionamento interno em
[docs/arquitetura.md](docs/arquitetura.md).

## Regras do projeto

- **Clean room.** Nunca copie código, ícones, fontes, sons ou imagens de Linux,
  Windows, macOS ou de qualquer outro projeto. Não abra, desmonte nem analise
  binários ou ISOs de terceiros. Imagens de referência que o usuário mandar são
  só inspiração: a arte final é própria e desenhada em código. Veja
  [docs/clean-room.md](docs/clean-room.md).
- **Português** em tudo que alguém lê: comentários, textos da interface, docs,
  mensagens de commit. Os identificadores seguem o código existente (nomes
  curtos em inglês: `win_create`, `gfx_fill`).
- **Sem libc.** O kernel é *freestanding*. Use `kernel/lib/` (`string.h`,
  `printf.h`, `utf8.h`) e `kmalloc`/`kcalloc`/`kfree` (`mm/heap.h`).
- **Float só na interface.** O núcleo (`arch/`, `boot/`, `mm/`, `sched/`,
  `dev/`, `fs/`, `lib/`, `main.c`) é compilado com `-mgeneral-regs-only`.
  Nada de `float`/`double` ali. Só `gui/` e `apps/` usam SSE, porque rodam como
  tarefa e o escalonador salva o estado SSE (FXSAVE) de cada uma.
- **Dentro de interrupção** (handlers de IRQ, ganchos do PIT): nada de mutex
  nem `task_sleep`, que bloqueiam. Evite também `kmalloc` e desenho: registre
  o dado (numa fila, por exemplo) e saia.
- **Estilo C:** gnu11, 4 espaços, chave na mesma linha, `static` para tudo que
  é interno ao arquivo. Comentários explicam o *porquê*, no tom didático dos
  arquivos vizinhos. O build não pode ter avisos (`-Wall -Wextra`).
- **Arte é código.** Ícones ficam em `kernel/gui/icons.c` (grade de 32×32
  unidades); o papel de parede da capivara, em `kernel/gui/wallpaper_pixel.c`.

## Comandos

| Para | Comando |
|---|---|
| gerar a ISO | `sh build.sh` (Linux/macOS/WSL/Git Bash) ou `.\build.ps1` (PowerShell) → `build/capivara.iso` |
| ver as ferramentas encontradas | `sh build.sh doctor` |
| testar | `sh build.sh test tests/gui.txt --out build/test/gui` (sem tela, no QEMU) |
| boot como pendrive / UEFI | `sh build.sh test tests/boot.txt --usb --uefi` |
| rodar com janela | `sh build.sh run` ou `.\build.ps1 vbox` |
| imagens da documentação | `make arte` (logo, ícones, banner) e `tests/readme.txt` (capturas) |

O `build.sh` usa as ferramentas do sistema se elas existirem; senão, usa o
container `capivara-toolchain` (Docker/Podman). Detalhes em
[docs/compilar.md](docs/compilar.md).

## Como testar uma mudança

1. Compile: zero avisos.
2. Rode um roteiro de `tests/` (ou escreva um; a skill `testar-no-qemu`
   explica a linguagem). O roteiro falha sozinho se o kernel entrar em pânico
   ou se o boot não terminar.
3. **Abra os PNGs** de `build/test/` e confira a tela de verdade. Compilar e
   não travar não prova que a interface ficou certa.
4. O log do kernel fica em `build/test/<pasta>/serial.log`.

O agente `testador-qemu` faz os passos 2 a 4 sem encher o contexto principal
de capturas de tela.

## Ao mudar algo visível

- Recurso novo ou alterado → atualize [docs/guia-de-uso.md](docs/guia-de-uso.md)
  e, se for o caso, as tabelas do [README](README.md).
- Ícone novo ou alterado → `make arte` (regera `docs/img/icones/`).
- Mudança grande na interface → regere as capturas com `tests/readme.txt`
  (instruções no próprio arquivo).
- Comando novo no Terminal → acrescente-o em `help()` (`kernel/apps/terminal.c`)
  e na tabela do guia de uso.

## No Windows

- No Git Bash, chame o Docker com `MSYS_NO_PATHCONV=1` e caminhos no formato
  `D:/...`. Rode Python dentro do container, porque o `python3` do Windows pode
  ficar esperando entrada.
- Com a VM do VirtualBox ligada, `build/capivara.iso` fica travada e o build
  falha ao gravá-la. Confira com `VBoxManage list runningvms`, não desligue a VM
  sem perguntar e, se precisar, compile para outro lugar:
  `make ISO=build/teste/capivara.iso`.

## CI (GitHub Actions)

- `.github/workflows/ci.yml`: compila nativamente em Ubuntu, Fedora e Arch, dá
  boot da ISO em 4 modos (CD/pendrive × BIOS/UEFI) e roda os roteiros da
  interface. As capturas viram artefatos da execução.
- `.github/workflows/toolchain.yml`: reconstrói o container de ferramentas
  quando `docker/` muda, e uma vez por mês.
- `.github/workflows/release.yml`: quando o CI termina verde num push para
  `main`, cria a tag `vX.Y.N` e publica a ISO numa Release. X.Y é
  `CAPIVARA_SERIE` em `kernel/version.h`; N é `git rev-list --count HEAD`.
  Ou seja: **todo push para `main` é público**. Mudar de série: skill
  `publicar-versao`.
