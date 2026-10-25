# 📚 Estudando o OSTEP com o Capivara

O livro **Operating Systems: Three Easy Pieces** (OSTEP), de Remzi e Andrea
Arpaci-Dusseau, é grátis em
[pages.cs.wisc.edu/~remzi/OSTEP](https://pages.cs.wisc.edu/~remzi/OSTEP/). O
Capivara traz um **guia de estudo** para ler junto com ele. Para cada capítulo,
o guia mostra onde a ideia aparece no código do sistema (ou por que ainda não
aparece) e propõe experimentos para rodar no próprio Capivara.

O guia é texto próprio: não reproduz o livro, só aponta os capítulos pelo
número e pelo título.

## 📖 Onde ler

- **Dentro do sistema**, no app **Estudos** (está no Dock). Um clique duplo numa
  linha de comando abre o Terminal já rodando ela.
- Na pasta **Documentos/OSTEP**, que abre no Bloco de Notas.
- Aqui no GitHub, em
  [rootfs/home/usuario/Documentos/OSTEP](../rootfs/home/usuario/Documentos/OSTEP/).

## 🗺️ Capítulos, código e laboratórios

| Capítulos do livro | Guia | Código do Capivara | Laboratórios |
|---|---|---|---|
| 2 · Introdução | [01](../rootfs/home/usuario/Documentos/OSTEP/01-introducao.md) | [main.c](../kernel/main.c) (`kmain`) | `dmesg`, `sysinfo` |
| 4-6 · Processos, API de processos, execução direta limitada | [02](../rootfs/home/usuario/Documentos/OSTEP/02-processos.md) | [task.c](../kernel/sched/task.c), [isr.S](../kernel/arch/x86_64/isr.S), [pit.c](../kernel/arch/x86_64/pit.c), [gdt.c](../kernel/arch/x86_64/gdt.c) | `ps`, `trocas` |
| 7-10 · Escalonamento: FIFO a MLFQ, loteria, várias CPUs | [03](../rootfs/home/usuario/Documentos/OSTEP/03-escalonamento.md) | [task.c](../kernel/sched/task.c) (`sched_switch`, `sched_tick`) | `escalonar` |
| 13-16 · Espaços de endereçamento, API de memória, tradução, segmentação | [04](../rootfs/home/usuario/Documentos/OSTEP/04-espacos-de-enderecamento.md) | [vmm.c](../kernel/mm/vmm.c), [linker.ld](../kernel/linker.ld), [gdt.c](../kernel/arch/x86_64/gdt.c) | `traduzir`, `mem` |
| 17 · Gerência de espaço livre | [05](../rootfs/home/usuario/Documentos/OSTEP/05-espaco-livre.md) | [heap.c](../kernel/mm/heap.c), [pmm.c](../kernel/mm/pmm.c) | `heap`, `heap teste` |
| 18-20 · Paginação, TLB, tabelas de vários níveis | [06](../rootfs/home/usuario/Documentos/OSTEP/06-paginacao.md) | [vmm.c](../kernel/mm/vmm.c) (`vmm_map`, `vmm_walk`), [boot.S](../kernel/arch/x86_64/boot.S) | `traduzir` |
| 21-23 · Troca com o disco e políticas de substituição | [07](../rootfs/home/usuario/Documentos/OSTEP/07-alem-da-memoria.md) | [idt.c](../kernel/arch/x86_64/idt.c), [panic.c](../kernel/panic.c) | `paginas`, `paginas belady` |
| 26-29 · Threads, condição de corrida, locks | [08](../rootfs/home/usuario/Documentos/OSTEP/08-concorrencia-e-locks.md) | [task.c](../kernel/sched/task.c) (`mutex_lock`), [ostep.c](../kernel/apps/ostep.c) (`race_worker`) | `corrida`, `corrida trava` |
| 30-33 · Variáveis de condição, semáforos, erros, eventos | [09](../rootfs/home/usuario/Documentos/OSTEP/09-condicoes-e-eventos.md) | [event.c](../kernel/sched/event.c) (`ev_wait`, `ev_post`), [wm.c](../kernel/gui/wm.c) | `corrida trava` |
| 36-38 · E/S, discos, RAID | [10](../rootfs/home/usuario/Documentos/OSTEP/10-dispositivos-e-discos.md) | [dev/](../kernel/dev/): [keyboard.c](../kernel/dev/keyboard.c), [ac97.c](../kernel/dev/ac97.c), [pci.c](../kernel/dev/pci.c) | `som teste`, `disco` |
| 39-45 · Arquivos, diretórios, implementação, FFS, journaling | [11](../rootfs/home/usuario/Documentos/OSTEP/11-arquivos.md) | [vfs.c](../kernel/fs/vfs.c), [mkinitrd.py](../tools/mkinitrd.py) | `stat`, `tree` |
| 47-50 e segurança | [12](../rootfs/home/usuario/Documentos/OSTEP/12-proximos-passos.md) | [session.c](../kernel/apps/session.c) | |

Os números de capítulo são os da edição online. Em outra edição podem mudar
um pouco: na dúvida, guie-se pelo título.

## 🧪 Os laboratórios

Ficam em [kernel/apps/ostep.c](../kernel/apps/ostep.c). Digite `ostep` no
Terminal para ver a lista.

| Comando | O que mostra | De verdade ou simulado |
|---|---|---|
| `trocas` | trocas de contexto por segundo e por tarefa | de verdade (contadores em `sched_switch`) |
| `escalonar <política> [q=N] [s=N] [tarefas]` | linha do tempo e métricas de FIFO, SJF, STCF, RR, MLFQ e loteria | simulado |
| `heap`, `heap blocos`, `heap teste` | blocos do heap, fragmentação, divisão e junção ao vivo | de verdade (`heap_snapshot`) |
| `traduzir [endereço]` | o caminho de um endereço pelas tabelas de página | de verdade (`vmm_walk`) |
| `paginas <política> [quadros] [refs]`, `paginas belady` | FIFO, LRU, OPT e relógio, passo a passo | simulado |
| `corrida [trava] [N]` | duas tarefas somando num contador, com e sem mutex | de verdade |
| `disco <política> [cabeça] [trilhas]` | FIFO, SSTF, SCAN e C-SCAN | simulado |
| `stat <caminho>` | os metadados de um arquivo (o vnode, que faz o papel do inode) | de verdade |

Sem argumentos, `escalonar` e `paginas` usam exemplos pequenos. O `paginas`
usa a mesma sequência de referências dos exemplos do cap. 22, para comparar
com as figuras do livro.

O roteiro [tests/ostep.txt](../tests/ostep.txt) roda todos eles no CI.
