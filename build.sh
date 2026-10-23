#!/bin/sh
# Compila e executa o Capivara OS no Linux, macOS, WSL ou Git Bash.
# (No Windows também existe o build.ps1, para o PowerShell.)
#
#   sh build.sh                gera build/capivara.iso
#   sh build.sh run            compila e abre no QEMU (BIOS)
#   sh build.sh run-uefi       compila e abre no QEMU com firmware UEFI
#   sh build.sh test [ROTEIRO] [--uefi] [--usb]
#                              testes automáticos sem tela (screenshots em build/test/)
#   sh build.sh usb [DISCO]    grava a ISO num pendrive (sem DISCO, lista os discos)
#   sh build.sh doctor         mostra quais ferramentas foram encontradas
#   sh build.sh clean          apaga a pasta build
#   sh build.sh toolchain      (re)constrói o container de ferramentas
#
# Onde compilar: se as ferramentas GNU estiverem instaladas no sistema (veja
# docs/compilar.md), usa elas; senão usa um container (Docker ou Podman) que
# já tem tudo. Para escolher:  MODO=nativo sh build.sh   ou   MODO=container sh build.sh
set -eu
cd "$(dirname "$0")"

IMAGE=capivara-toolchain
ISO=build/capivara.iso
MODO=${MODO:-auto}

die() { echo "erro: $*" >&2; exit 1; }
has() { command -v "$1" >/dev/null 2>&1; }
cpus() { getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2; }

# No Git Bash/Cygwin o Docker para Windows quer caminhos "D:/..." e não "/d/..."
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) SRC=$(cygpath -m "$(pwd)"); export MSYS_NO_PATHCONV=1 ;;
    *) SRC=$(pwd) ;;
esac

# Dá para compilar sem container? Precisa de um compilador para x86_64 "pelado"
# (o cruzado x86_64-elf-gcc, ou o gcc de um Linux x86_64), GRUB, xorriso,
# mtools e Python.
native_ok() {
    has make && has xorriso && has mformat || return 1
    has python3 || has python || return 1
    has grub-mkrescue || has grub2-mkrescue || has x86_64-elf-grub-mkrescue || return 1
    has x86_64-elf-gcc && return 0
    [ "$(uname -sm)" = "Linux x86_64" ] && has gcc && has ld
}

use_native() {
    case "$MODO" in
        nativo) return 0 ;;
        container) return 1 ;;
        *) native_ok ;;
    esac
}

engine() {
    if [ -n "${ENGINE:-}" ]; then echo "$ENGINE"
    elif has docker && docker info >/dev/null 2>&1; then echo docker
    elif has podman; then echo podman
    elif has docker; then die "o Docker está instalado, mas não está rodando"
    else return 1
    fi
}

ensure_image() {
    if [ "${1:-}" = force ] || ! $ENG image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "==> Construindo as ferramentas GNU (só na primeira vez, ~10 min)..."
        $ENG build --platform linux/amd64 -t "$IMAGE" docker
    fi
}

# Roda um comando dentro do container, com o projeto montado em /src
container() {
    ENG=$(engine) || die "não achei as ferramentas nativas nem Docker/Podman. Veja docs/compilar.md"
    ensure_image
    vol="$SRC:/src"
    opts=""
    if [ "$ENG" = podman ]; then
        vol="$SRC:/src:Z"        # :Z = rótulo do SELinux (Fedora); o podman sem root já mapeia o usuário
    elif [ "$(uname -s)" = Linux ]; then
        # no Linux, os arquivos gerados ficam com o seu usuário, e não com o root
        opts="--user $(id -u):$(id -g) -e HOME=/tmp"
    fi
    # shellcheck disable=SC2086
    $ENG run --rm --platform linux/amd64 $opts -v "$vol" -w /src "$IMAGE" sh -c "$*"
}

build() {
    if use_native; then
        make -j"$(cpus)"
    else
        echo "==> Compilando no container ($(engine))"
        container 'make -j$(nproc)'
    fi
}

run_qemu() {
    has qemu-system-x86_64 || die "QEMU não encontrado. Instale (apt install qemu-system-x86 | dnf install qemu-system-x86 | pacman -S qemu-full | brew install qemu) ou abra $ISO no VirtualBox"
    make "$1"
}

# ---------------------------------------------------------------------------
# Pendrive. Gravar apaga TUDO no disco escolhido, então o script confere que
# é um disco USB/removível inteiro e pede para digitar o nome dele de novo.
# ---------------------------------------------------------------------------
confirm() {
    printf '\nTODO o conteúdo de %s será APAGADO.\nPara confirmar, digite o nome do disco (%s): ' "$1" "$1"
    read -r resposta
    [ "$resposta" = "$1" ] || die "cancelado"
}

sudo_cmd() { if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi; }

usb_linux() {
    if [ -z "${1:-}" ]; then
        echo "Discos encontrados (TRAN=usb ou RM=1 indicam pendrive):"
        lsblk -dpo NAME,SIZE,TRAN,RM,MODEL -e 7
        echo
        echo "Uso: sh build.sh usb /dev/sdX"
        return
    fi
    dev=$1
    [ -b "$dev" ] || die "$dev não existe ou não é um disco"
    info=$(lsblk -dnPo TYPE,TRAN,RM "$dev")
    case "$info" in *'TYPE="disk"'*) ;; *) die "$dev não é um disco inteiro (use /dev/sdX, e não /dev/sdX1)" ;; esac
    case "$info" in
        *'TRAN="usb"'* | *'RM="1"'*) ;;
        *) die "$dev não é USB nem removível; por segurança, nada foi feito" ;;
    esac
    if lsblk -nro MOUNTPOINT "$dev" | grep -Eq '^(/|/boot.*|/home|\[SWAP\])$'; then
        die "$dev tem partições do sistema em uso; por segurança, nada foi feito"
    fi
    lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINT "$dev"
    confirm "$dev"
    lsblk -nrpo NAME,MOUNTPOINT "$dev" | while read -r part mnt; do
        if [ -n "$mnt" ]; then sudo_cmd umount "$part"; fi
    done
    sudo_cmd dd if="$ISO" of="$dev" bs=4M conv=fsync status=progress
    sync
    echo "==> Pronto! Pode tirar o pendrive."
}

usb_macos() {
    if [ -z "${1:-}" ]; then
        diskutil list external physical
        echo "Uso: sh build.sh usb /dev/diskN"
        return
    fi
    dev=$1
    case "$dev" in
        /dev/disk[0-9]*s[0-9]*) die "use o disco inteiro (/dev/diskN), e não uma partição" ;;
        /dev/disk[0-9]*) ;;
        *) die "use o formato /dev/diskN (veja: sh build.sh usb)" ;;
    esac
    diskutil info "$dev" | grep -Eq 'Device Location: +External' ||
        die "$dev não é um disco externo; por segurança, nada foi feito"
    diskutil list "$dev"
    confirm "$dev"
    diskutil unmountDisk "$dev"
    sudo dd if="$ISO" of="/dev/r${dev#/dev/}" bs=4m     # rdisk: acesso direto, bem mais rápido
    diskutil eject "$dev"
    echo "==> Pronto! Pode tirar o pendrive."
}

usb() {
    [ -f "$ISO" ] || build
    case "$(uname -s)" in
        Linux) usb_linux "$@" ;;
        Darwin) usb_macos "$@" ;;
        *)
            echo "Neste sistema, grave $ISO com o balenaEtcher ou, no Windows, com o Rufus"
            echo "(no Rufus, quando perguntar, escolha \"Gravar em modo imagem DD\")."
            ;;
    esac
}

acao=${1:-iso}
if [ $# -gt 0 ]; then shift; fi
case "$acao" in
    iso) build; echo "ISO: $ISO" ;;
    run) build; run_qemu run ;;
    run-uefi) build; run_qemu run-uefi ;;
    test)
        roteiro=tests/gui.txt
        case "${1:-}" in "" | --*) ;; *) roteiro=$1; shift ;; esac
        build
        if use_native && has qemu-system-x86_64 && has convert; then
            python3 tools/qemu-test.py "$roteiro" "$@"
        else
            container "python3 tools/qemu-test.py $roteiro $*"
        fi
        echo "Screenshots em build/test/"
        ;;
    usb) usb "$@" ;;
    doctor)
        make doctor || true
        if native_ok; then echo "=> dá para compilar sem container"
        elif ENG=$(engine 2>/dev/null); then echo "=> vai compilar no container ($ENG)"
        else echo "=> faltam ferramentas: instale as da lista acima ou o Docker/Podman (docs/compilar.md)"
        fi
        ;;
    clean) rm -rf build ;;
    toolchain) ENG=$(engine) || die "Docker ou Podman não encontrado"; ensure_image force ;;
    *) sed -n '2,19p' "$0"; exit 1 ;;
esac
