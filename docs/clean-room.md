# Processo *clean room* do Capivara OS

*Clean room* (sala limpa) é um método para recriar um sistema sem copiar a
implementação original. O trabalho é dividido em duas etapas que não se misturam:

1. **Especificação** — alguém estuda o sistema de referência e descreve, com
   as próprias palavras, *o que* ele faz: comportamento, interfaces, conceitos.
   Nenhum trecho de código é transcrito.
2. **Implementação** — o código é escrito **somente** a partir das
   especificações e de documentação pública, sem olhar o código original.

## Como isso foi aplicado aqui

| Referência | Uso permitido | O que NÃO foi feito |
|---|---|---|
| Linux / Unix | Conceitos de arquitetura documentados publicamente (processos, escalonador, "tudo é arquivo", VFS, chamadas de sistema) descritos em `docs/spec/` | Nenhum arquivo do código-fonte do Linux foi lido ou copiado durante a implementação |
| Windows | Recursos que qualquer usuário observa: Alt+Tab, Super+setas, layouts de encaixe, Visão de Tarefas, Lixeira, notificações, captura de tela, Gerenciador de Tarefas | A ISO **não** foi aberta, descompilada nem analisada. Nenhum ícone, imagem, fonte, som ou código da Microsoft é usado |
| macOS | Convenções visuais: barra de menus global no topo, Dock com ampliação, botões vermelho/amarelo/verde à esquerda, Launchpad, Mission Control, Central de Controle | Nenhum ícone, logotipo, fonte, papel de parede ou código da Apple. No lugar da maçã está uma capivara; todos os ícones são desenhados por código, com arte própria |
| Hardware x86 | Manuais públicos da Intel/AMD, especificação Multiboot2 (GNU), especificação ACPI, documentação do controlador PS/2, PIT 8254, CMOS/RTC | — |
| Máquinas virtuais | Protocolos públicos de "integração do mouse" (porta VMware usada pelo QEMU; dispositivo VMMDev do VirtualBox, cujos adicionais para convidados são de código aberto) | Nenhum código dos adicionais para convidados foi copiado |

> **Por que não usar a ISO do Windows?** Engenharia reversa dos binários da
> Microsoft viola a licença deles e, no nosso caso, nem ajudaria: uma interface
> "parecida com" se descreve pelo comportamento visível, não pelo código
> interno. E se a mesma pessoa que lê a desmontagem escreve o código, o
> processo deixa de ser *clean room*.

## Componentes GNU usados

- **GCC** e **GNU binutils** (compilador cruzado `x86_64-elf`)
- **GNU Make**
- **GNU GRUB** (carregador de boot, protocolo Multiboot2)
- **GNU Unifont** (fonte bitmap 8×16, licença GPLv2+ com exceção para
  embutir fontes / SIL OFL 1.1) — veja `kernel/gui/font/LEIA-ME.md`

Todo o resto (kernel, drivers, interface gráfica, aplicativos) é código próprio.
