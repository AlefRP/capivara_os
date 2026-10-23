# Bancada de ferramentas do Capivara OS.
#
# O container NÃO faz parte do sistema: é só o lugar onde ficam as ferramentas
# GNU usadas para compilar o kernel. Com ele, o mesmo build funciona no
# Windows, no macOS e em qualquer Linux sem instalar nada além do Docker
# (ou Podman).
#
#   - binutils + GCC compilados como *cross-compiler* para o alvo x86_64-elf
#     (um compilador que gera código "pelado", sem supor Linux nem libc)
#   - GNU GRUB + xorriso + mtools para montar a ISO inicializável
#   - QEMU e OVMF (firmware UEFI) para testes automáticos sem tela
#
# Construa sempre para x86_64 (docker build --platform linux/amd64, como fazem
# o build.sh e o build.ps1): os módulos de BIOS do GRUB só existem para essa
# arquitetura. Em Macs com chip Apple a imagem roda emulada (mais lenta).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev \
        wget ca-certificates xz-utils python3 make \
        grub-common grub-pc-bin grub-efi-amd64-bin xorriso mtools \
        qemu-system-x86 imagemagick \
    && rm -rf /var/lib/apt/lists/*

ARG BINUTILS_VERSION=2.43.1
ARG GCC_VERSION=14.2.0
ARG TARGET=x86_64-elf
ARG PREFIX=/opt/cross

WORKDIR /tmp/toolchain
RUN wget -q https://ftpmirror.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz \
    && tar xf binutils-${BINUTILS_VERSION}.tar.xz \
    && mkdir build-binutils && cd build-binutils \
    && ../binutils-${BINUTILS_VERSION}/configure --target=${TARGET} --prefix=${PREFIX} \
         --with-sysroot --disable-nls --disable-werror \
    && make -j"$(nproc)" && make install \
    && cd /tmp/toolchain && rm -rf build-binutils binutils-*

RUN wget -q https://ftpmirror.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz \
    && tar xf gcc-${GCC_VERSION}.tar.xz \
    && mkdir build-gcc && cd build-gcc \
    && ../gcc-${GCC_VERSION}/configure --target=${TARGET} --prefix=${PREFIX} \
         --disable-nls --enable-languages=c --without-headers --disable-hosted-libstdcxx \
    && make -j"$(nproc)" all-gcc && make install-gcc \
    && cd / && rm -rf /tmp/toolchain

ENV PATH=${PREFIX}/bin:${PATH}

# Firmware UEFI para testar o boot por UEFI (o mesmo dos PCs modernos)
RUN apt-get update && apt-get install -y --no-install-recommends ovmf \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
