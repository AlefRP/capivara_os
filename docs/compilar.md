# Como compilar o Capivara OS em qualquer sistema

O resultado do build é sempre o mesmo arquivo, `build/capivara.iso`, que serve
para máquina virtual, CD e [pendrive](pendrive.md).

Há dois caminhos:

| Caminho | Precisa de | Funciona em |
|---|---|---|
| **Container** (recomendado) | só o Docker ou o Podman | Windows, macOS (Intel e Apple), qualquer Linux |
| **Nativo** | GCC, GNU Make, GRUB, xorriso, mtools, Python 3 | Linux x86_64 (inclusive WSL); macOS com Homebrew |

Os scripts escolhem sozinhos: se as ferramentas nativas estiverem instaladas,
usam elas. Se não estiverem, usam o container. Na primeira vez, o container
compila o GCC cruzado, o que leva uns 10 minutos.

## Windows (PowerShell)

Instale o [Docker Desktop](https://www.docker.com/products/docker-desktop/) e,
para rodar, o VirtualBox ou o QEMU.

```powershell
.\build.ps1          # gera build\capivara.iso
.\build.ps1 vbox     # compila e abre no VirtualBox
.\build.ps1 qemu     # compila e abre no QEMU
.\build.ps1 test     # testes automáticos sem tela
.\build.ps1 usb      # explica como gravar o pendrive
```

No WSL ou no Git Bash também dá para usar o `build.sh`, igual ao Linux.

## Linux, macOS e WSL (`build.sh`)

```sh
sh build.sh                 # gera build/capivara.iso
sh build.sh run             # compila e abre no QEMU (BIOS)
sh build.sh run-uefi        # compila e abre no QEMU com UEFI (precisa do OVMF)
sh build.sh test            # testes automáticos sem tela (screenshots em build/test/)
sh build.sh test tests/boot.txt --usb --uefi   # testa o boot como pendrive, em UEFI
sh build.sh usb             # grava a ISO num pendrive (veja pendrive.md)
sh build.sh doctor          # mostra quais ferramentas foram encontradas
sh build.sh clean
```

Para forçar um dos caminhos: `MODO=container sh build.sh` ou
`MODO=nativo sh build.sh`. Com o Podman no lugar do Docker, nada muda:
o script detecta sozinho, ou use `ENGINE=podman`.

O `make run` liga a placa de som AC'97 no QEMU, que usa a primeira saída de
áudio que funcionar. Para escolher outra: `make run QEMU_AUDIO=alsa` (ou `pa`,
`coreaudio`, `dsound`...).

Programas de fora do repositório entram no build pelo arquivo
`extras.local`. Veja [Programas extras](extras.md).

### Ferramentas nativas por sistema

Com estes pacotes, o `sh build.sh` compila direto, sem container. Num Linux
x86_64, o próprio `gcc` do sistema serve: o kernel não usa nada do Linux, nem
libc nem cabeçalhos do sistema. O Makefile desliga o que as distribuições
ligam por padrão (PIE, proteção de pilha, CET).

| Sistema | Comando |
|---|---|
| Ubuntu / Debian / Mint / WSL | `sudo apt install gcc make python3 grub-common grub-pc-bin grub-efi-amd64-bin xorriso mtools qemu-system-x86 ovmf` |
| Fedora | `sudo dnf install gcc make python3 grub2-tools grub2-tools-extra grub2-pc-modules grub2-efi-x64-modules xorriso mtools qemu-system-x86 edk2-ovmf` |
| Arch / Manjaro | `sudo pacman -S gcc make python grub libisoburn mtools qemu-full edk2-ovmf` |
| macOS (Homebrew) | `brew install x86_64-elf-gcc x86_64-elf-binutils x86_64-elf-grub xorriso mtools qemu` |

O build nativo foi testado em Ubuntu 24.04, Fedora 41 e Arch Linux, com GCC 13,
14 e 16. As ISOs geradas deram boot por BIOS e por UEFI.

O caminho com Homebrew no macOS não foi testado. Se a ISO não der boot por UEFI
(depende de como o GRUB do Homebrew foi compilado), use o container.

Em Linux que não é x86_64 (Raspberry Pi, por exemplo), o gcc do sistema gera
código ARM. Nesses casos, use o container ou instale um `x86_64-elf-gcc`.

### Detalhes que tornam o build portátil

- O nome do gerador de ISO muda entre sistemas (`grub-mkrescue`,
  `grub2-mkrescue` no Fedora, `x86_64-elf-grub-mkrescue` no Homebrew). O Makefile procura
  os três.
- O initrd é gerado por `tools/mkinitrd.py`, e não pelo `tar`, porque o `tar`
  do macOS/BSD e o GNU tar aceitam opções diferentes.
- Todos os arquivos usam final de linha LF (`.gitattributes`). Um `\r` no fim
  da linha quebra scripts `sh` no Linux.
- A imagem do container é sempre x86_64 (`--platform linux/amd64`), porque os
  módulos de BIOS do GRUB só existem para essa arquitetura. Num Mac com chip
  Apple ela roda emulada, mais devagar, mas funciona.
