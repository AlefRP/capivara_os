# Capivara OS — sistema de build (GNU Make)
#
# Uso (direto no Linux/macOS com as ferramentas instaladas, ou dentro do
# container de ferramentas: veja build.sh e build.ps1):
#   make          → build/capivara.iso
#   make run      → roda no QEMU com a saída serial no terminal
#   make run-uefi → idem, com firmware UEFI (OVMF)
#   make doctor   → mostra quais ferramentas foram encontradas
#   make clean

, := ,
# Procura o primeiro programa da lista que existe no PATH
find-tool =$(firstword $(foreach p,$(1),$(if $(shell command -v $(p) 2>/dev/null),$(p))))

# Compilador: de preferência o cruzado x86_64-elf. Num Linux x86_64 sem ele, o
# gcc do próprio sistema também serve: o kernel não usa nada do Linux (nem
# libc, nem cabeçalhos do sistema), e as opções abaixo desligam o que as
# distribuições ligam por padrão (PIE, proteção de pilha, CET).
ifeq ($(origin CROSS),undefined)
  CROSS := $(if $(call find-tool,x86_64-elf-gcc),x86_64-elf-,)
endif
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
# Fora de um Linux x86_64, o gcc do sistema não serve (no macOS ele é o clang).
# O aviso só aparece se for preciso compilar algo: clean, doctor e run funcionam.
ifeq ($(CROSS),)
  ifneq ($(shell uname -sm),Linux x86_64)
    CC = $(error Falta o compilador cruzado x86_64-elf-gcc (veja docs/compilar.md), ou use o build.sh, que compila dentro de um container)
  endif
endif
PYTHON  ?= $(or $(call find-tool,python3 python),python3)
# Debian/Ubuntu/Arch: grub-mkrescue; Fedora/openSUSE: grub2-mkrescue; Homebrew: x86_64-elf-grub-mkrescue
GRUB_MKRESCUE ?= $(or $(call find-tool,grub-mkrescue grub2-mkrescue x86_64-elf-grub-mkrescue),grub-mkrescue)
QEMU    ?= qemu-system-x86_64

BUILD   := build
OBJ     := $(BUILD)/obj
ISO     := $(BUILD)/capivara.iso
KERNEL  := $(BUILD)/kernel.elf
INITRD  := $(BUILD)/initrd.tar
FONT_C  := $(BUILD)/gen/font_data.c

CFLAGS_BASE := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fcf-protection=none -fno-stack-clash-protection \
	-mno-red-zone -mcmodel=kernel -fno-omit-frame-pointer \
	-fno-asynchronous-unwind-tables -fno-math-errno \
	-Ikernel -MMD -MP

# Número da versão (kernel/version.h). O release.yml passa VERSAO=X.Y.N;
# sem ele, o sistema se apresenta como "X.Y-dev".
ifdef VERSAO
  CFLAGS_BASE += -DCAPIVARA_VERSION='"$(VERSAO)"'
endif

# Núcleo: proibido usar registradores SSE, porque este código também roda dentro
# de interrupções (e não salvamos o estado SSE ao entrar numa interrupção).
CFLAGS_CORE := $(CFLAGS_BASE) -mgeneral-regs-only

# Interface gráfica e aplicativos: SSE liberado (contas com float para
# antisserrilhado). Só rodam em contexto de tarefa, e o escalonador salva o
# estado SSE de cada tarefa com FXSAVE/FXRSTOR.
CFLAGS_GUI  := $(CFLAGS_BASE) -msse2

LDFLAGS := -T kernel/linker.ld -nostdlib -z max-page-size=0x1000 -z noexecstack

CORE_SRC := $(wildcard kernel/*.c kernel/arch/x86_64/*.c kernel/lib/*.c \
	kernel/mm/*.c kernel/dev/*.c kernel/sched/*.c kernel/fs/*.c kernel/boot/*.c)
GUI_SRC  := $(wildcard kernel/gui/*.c kernel/apps/*.c)
ASM_SRC  := $(wildcard kernel/arch/x86_64/*.S)

CORE_OBJ := $(CORE_SRC:%.c=$(OBJ)/%.o)
GUI_OBJ  := $(GUI_SRC:%.c=$(OBJ)/%.o) $(OBJ)/gen/font_data.o
ASM_OBJ  := $(ASM_SRC:%.S=$(OBJ)/%.o)
ALL_OBJ  := $(ASM_OBJ) $(CORE_OBJ) $(GUI_OBJ)

QEMU_FLAGS := -m 512M -cdrom $(ISO) -serial stdio -rtc base=localtime \
	-no-reboot -no-shutdown

# Firmware UEFI para o QEMU: cada sistema guarda o OVMF num lugar diferente
OVMF ?= $(firstword $(wildcard \
	/usr/share/ovmf/OVMF.fd \
	/usr/share/edk2/x64/OVMF.4m.fd \
	/usr/share/edk2-ovmf/x64/OVMF.fd \
	/usr/share/edk2/ovmf/OVMF_CODE.fd \
	/usr/share/OVMF/OVMF_CODE_4M.fd \
	/usr/share/OVMF/OVMF_CODE.fd \
	/usr/share/qemu/ovmf-x86_64.bin \
	/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
	/usr/local/share/qemu/edk2-x86_64-code.fd))

.PHONY: all iso run run-headless run-uefi arte doctor clean
all: iso
iso: $(ISO)

$(ASM_OBJ): $(OBJ)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_CORE) -c $< -o $@

$(CORE_OBJ): $(OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_CORE) -c $< -o $@

$(OBJ)/kernel/%.o: kernel/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_GUI) -c $< -o $@

$(OBJ)/gen/font_data.o: $(FONT_C)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_GUI) -c $< -o $@

$(FONT_C): kernel/gui/font/unifont-subset.hex tools/unifont2c.py
	@mkdir -p $(@D)
	$(PYTHON) tools/unifont2c.py $< $@

$(KERNEL): $(ALL_OBJ) kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJ)

# Arquivos iniciais do sistema (empacotados num tar que o GRUB carrega como módulo)
$(INITRD): $(shell find rootfs -type f 2>/dev/null) tools/mkinitrd.py
	@mkdir -p $(@D)
	$(PYTHON) tools/mkinitrd.py rootfs $@

# A ISO é "híbrida": serve para CD/DVD, máquina virtual e também para gravar
# direto num pendrive, e dá boot tanto por BIOS quanto por UEFI.
$(ISO): $(KERNEL) $(INITRD) grub/grub.cfg
	@rm -rf $(BUILD)/isodir
	@mkdir -p $(BUILD)/isodir/boot/grub $(@D)
	cp $(KERNEL) $(INITRD) $(BUILD)/isodir/boot/
	cp grub/grub.cfg $(BUILD)/isodir/boot/grub/
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/isodir 2> $(BUILD)/grub-mkrescue.log || (cat $(BUILD)/grub-mkrescue.log; false)
	@echo "==> $@ pronta"

run: $(ISO)
	$(QEMU) $(QEMU_FLAGS)

run-headless: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -display none

# OVMF.fd (firmware + variáveis num arquivo só) entra com -bios; os arquivos
# *CODE* só com o firmware entram como memória flash somente leitura.
run-uefi: $(ISO)
	@test -n "$(OVMF)" || (echo "OVMF (firmware UEFI) não encontrado; instale o pacote ovmf/edk2-ovmf ou passe OVMF=/caminho"; false)
	$(QEMU) $(QEMU_FLAGS) $(if $(findstring CODE,$(OVMF))$(findstring code,$(OVMF)),-drive if=pflash$(,)format=raw$(,)readonly=on$(,)file=$(OVMF),-bios $(OVMF))

# Imagens da documentação (docs/img), desenhadas pelo próprio código gráfico
# do sistema compilado como programa comum (veja tools/arte/arte.c).
HOSTCC  ?= cc
ARTE_SRC := tools/arte/arte.c kernel/gui/gfx.c kernel/gui/icons.c kernel/gui/font.c \
	kernel/gui/wallpaper_pixel.c kernel/lib/utf8.c $(FONT_C)

arte: $(ARTE_SRC)
	@mkdir -p $(BUILD)/arte docs/img/icones
	$(HOSTCC) -O2 -w -Itools/arte/stub -Ikernel -o $(BUILD)/arte/arte $(ARTE_SRC) -lm
	$(BUILD)/arte/arte $(BUILD)/arte
	for f in $(BUILD)/arte/*.pam; do \
		n=$$(basename $$f .pam); \
		case $$n in banner|logo|papel-de-parede) d=docs/img;; *) d=docs/img/icones;; esac; \
		convert $$f -strip $$d/$$n.png; \
	done

doctor:
	@echo "compilador C : $(if $(shell command -v $(CROSS)gcc 2>/dev/null),$(CROSS)gcc,NÃO ENCONTRADO ($(CROSS)gcc))$(if $(CROSS),, (gcc do sistema: só serve em Linux x86_64))"
	@echo "ligador      : $(if $(shell command -v $(LD) 2>/dev/null),$(LD),NÃO ENCONTRADO ($(LD)))"
	@echo "python       : $(if $(shell command -v $(PYTHON) 2>/dev/null),$(PYTHON),NÃO ENCONTRADO)"
	@echo "grub-mkrescue: $(if $(shell command -v $(GRUB_MKRESCUE) 2>/dev/null),$(GRUB_MKRESCUE),NÃO ENCONTRADO)"
	@echo "xorriso      : $(if $(call find-tool,xorriso),ok,NÃO ENCONTRADO)"
	@echo "mtools       : $(if $(call find-tool,mformat),ok,NÃO ENCONTRADO (sem ele a ISO não dá boot por UEFI))"
	@echo "qemu         : $(if $(call find-tool,$(QEMU)),ok,não encontrado (opcional))"
	@echo "ovmf (UEFI)  : $(or $(OVMF),não encontrado (opcional))"

clean:
	rm -rf $(BUILD)

-include $(ALL_OBJ:.o=.d)
