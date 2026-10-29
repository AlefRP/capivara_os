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
| 26-29 · Threads, condição de corrida, locks | [08](../rootfs/home/usuario/Documentos/OSTEP/08-concorrencia-e-locks.md) | [task.c](../kernel/sched/task.c) (`mutex_lock`, `task_join`), [ostep.c](../kernel/apps/ostep.c) (`race_worker`) | `corrida`, `corrida trava`, `prodcons` |
| 30-33 · Variáveis de condição, semáforos, erros, eventos | [09](../rootfs/home/usuario/Documentos/OSTEP/09-condicoes-e-eventos.md) | [task.c](../kernel/sched/task.c) (`sem_wait`, `sem_post`), [event.c](../kernel/sched/event.c) (`ev_wait`, `ev_post`), [wm.c](../kernel/gui/wm.c) | `prodcons polling`, `prodcons semaforo` |
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
| `prodcons <polling\|semaforo> [p=N] [c=N] [b=N] [n=N] [atraso=N]` | produtores e consumidores numa fila limitada, integridade dos itens e custo da espera | de verdade (tarefas, semáforos e `task_join`) |
| `prodcons teste` | dez regressões de semáforos e join: créditos, bloqueio, cancelamento, ciclos e coleta de tarefas | de verdade |
| `disco <política> [cabeça] [trilhas]` | FIFO, SSTF, SCAN e C-SCAN | simulado |
| `stat <caminho>` | os metadados de um arquivo (o vnode, que faz o papel do inode) | de verdade |

Sem argumentos, `escalonar` e `paginas` usam exemplos pequenos. O `paginas`
usa a mesma sequência de referências dos exemplos do cap. 22, para comparar
com as figuras do livro.

O roteiro [tests/ostep.txt](../tests/ostep.txt) cobre os laboratórios gerais
no CI. [tests/prodcons.txt](../tests/prodcons.txt) executa `prodcons teste`
e nove cenários de produtores/consumidores, incluindo buffer de uma posição,
quantidades assimétricas de trabalhadores e atraso zero.

## Produtores e consumidores: prever, medir e conferir

O `prodcons` liga os capítulos 27, 30 e 31 a tarefas reais do escalonador.
Dois modos fazem o mesmo trabalho: `polling` consulta a fila e cede a CPU com
`task_yield` quando não pode avançar; `semaforo` bloqueia até existir um item
ou uma vaga. Ambos protegem as alterações na fila. O modo semáforo usa um
contador de vagas, inicialmente igual à capacidade, e outro de itens,
inicialmente zero. `task_join` espera o término dos trabalhadores sem ficar
consultando um contador.

```sh
prodcons polling p=2 c=2 b=4 n=40 atraso=1
prodcons semaforo p=2 c=2 b=4 n=40 atraso=1
```

Os valores acima são os padrões: `p` produtores e `c` consumidores (1 a 4),
`b` posições no buffer (1 a 64), `n` itens **por produtor** (1 a 10 000) e
`atraso` em milissegundos antes de publicar cada item (0 a 20). O atraso cria
intervalos em que os consumidores precisam esperar; ele faz parte da carga
medida.

Antes de executar, preveja o total: `p × n`. Depois confira os itens
produzidos e consumidos, IDs ausentes ou repetidos, limite de ocupação e fila
vazia ao final. O tempo sozinho não prova que a sincronização está correta.
Compare também as tentativas de polling, as vezes em que as tarefas cedem a
CPU ou bloqueiam e quantas vezes são escalonadas. O tempo de CPU usa ticks
de 1 ms; trabalhos muito curtos podem aparecer como zero. As métricas de CPU,
yields e bloqueios somam os trabalhadores, excluindo a espera pela partida;
não incluem a tarefa que coordena o laboratório.

Mude uma opção por vez: comece com `b=1`, aumente o buffer e depois varie
produtores, consumidores e atraso. Repita com as mesmas opções e explique
as diferenças. O escalonamento e os tempos podem variar entre execuções;
a integridade dos itens deve permanecer correta.

Para verificar as primitivas separadamente, execute:

```sh
prodcons teste
```

Os dez casos de `kernel/apps/ostep_sync.c` verificam conservação de créditos,
despertar sem crédito, cancelamento de esperas, exclusividade e ciclos de
join, métricas finais e coleta automática após detach. O roteiro de QEMU
exige as mensagens de sucesso e os totais esperados; os tempos e contadores
de desempenho variáveis não são usados como resultado exato de regressão.

O experimento usa uma CPU e tarefas do kernel (anel 0). Não implementa
variáveis de condição nem isolamento de processos. As funções de
sincronização ficam em `kernel/sched/task.c`; o uso delas está em
`kernel/apps/ostep.c`.

Durante o experimento, os trabalhadores ficam protegidos contra finalização.
Um pedido para finalizar o Terminal é adiado até o laboratório terminar e
recolher as tarefas, para que uma finalização parcial não deixe a fila sem
produtores ou consumidores.
