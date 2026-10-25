# Espaços de endereçamento, tradução e segmentação

## Cap. 13 · The Abstraction: Address Spaces

**No livro.** O espaço de endereçamento é a visão que um processo tem da
memória: o código embaixo, o heap crescendo para cima e a pilha crescendo para
baixo, tudo começando no endereço 0. Os endereços que o programa vê são
**virtuais**; o sistema, com ajuda do hardware, decide onde eles ficam na
memória física. Os objetivos são três: **transparência** (o programa não
percebe), **eficiência** (pouco custo) e **proteção** (um processo não alcança
a memória de outro).

**No Capivara.** Existe **um espaço de endereçamento só**, o do kernel, e todas
as tarefas o compartilham. O leiaute fica na "metade de cima" dos 64 bits:

- `0xFFFFFFFF80000000` + físico: o kernel (código e dados), que o GRUB carregou
  no endereço físico 1 MiB. O `kernel/linker.ld` define esse endereço.
- `0xFFFF800000000000` + físico: o **physmap**, toda a RAM mapeada de uma vez.
  Com ele, o kernel acessa qualquer endereço físico somando uma constante
  (`phys_to_virt` em `kernel/mm/vmm.c`).
- `0xFFFFA00000000000` em diante: o heap do kernel (`HEAP_BASE`). As pilhas das
  tarefas também saem dele.

A metade de baixo (onde ficariam os programas de usuário) está vazia. O
endereço 0 não está mapeado de propósito: um ponteiro nulo dá *page fault* em
vez de estragar memória em silêncio.

**Experimente.** Veja onde ficam um código, um bloco do heap, uma variável da
pilha e o physmap:

```sh
traduzir
```

```sh
mem
```

**Para pensar.**

- Desenhe o espaço de endereçamento do Capivara no estilo das figuras do livro.
  O que fica no lugar do "código, heap, pilha" de um processo?
- Por que o kernel fica no topo do espaço, e não embaixo? (Pense em como será
  quando cada processo tiver a sua metade de baixo.)

## Cap. 14 · Interlude: Memory API

**No livro.** Em C, `malloc` reserva memória no heap e `free` devolve. O
capítulo lista os erros clássicos: esquecer de reservar, reservar pouco,
esquecer de inicializar, nunca liberar (vazamento), usar depois de liberar,
liberar duas vezes e passar ao `free` um ponteiro errado. Também explica que
`malloc` e `free` são uma biblioteca, e que ela pede memória ao sistema com
chamadas como `brk` e `mmap`.

**No Capivara.** O kernel não tem biblioteca C. O equivalente é `kmalloc`,
`kcalloc`, `krealloc` e `kfree` em `kernel/mm/heap.c`. Dois cuidados do livro
aparecem no código:

- **Liberar duas vezes ou um ponteiro errado**: cada bloco tem um número mágico
  no cabeçalho. `kfree` confere o número e, se não bater, para o sistema com
  um *panic* ("ponteiro inválido ou liberado duas vezes") em vez de corromper o
  heap.
- **Vazamento**: cada bloco anota a tarefa que o reservou (`owner`), e o
  Gerenciador de Tarefas mostra quanto cada uma está usando.

O papel do `brk` é feito pela função `grow`: quando falta espaço, o heap pega
páginas físicas e as mapeia no fim dele.

**Experimente.** Abra e feche alguns aplicativos e veja a memória por tarefa no
Gerenciador de Tarefas (Ctrl+Shift+Esc). No Terminal:

```sh
heap
```

```sh
heap blocos 20
```

**Para pensar.**

- O `heap blocos` mostra o dono de cada bloco. Feche uma janela e rode de novo:
  os blocos daquela tarefa somem? Se algum ficar, é vazamento.
- `krealloc` em `heap.c` nunca encolhe um bloco. Por que isso é aceitável?

## Cap. 15 · Mechanism: Address Translation

**No livro.** O jeito mais simples de virtualizar a memória é a **base e
limite**: o hardware soma um registrador de base a cada endereço e confere se
ele passou do limite. Cada processo fica num pedaço contínuo da memória física.
Isso é rápido e protege, mas desperdiça o espaço entre o heap e a pilha
(fragmentação interna).

**No Capivara.** O x86 de 64 bits não tem base e limite; a tradução é toda
feita por paginação (caps. 18 a 20). Mas o physmap faz algo parecido, em
software: `phys_to_virt(p)` é só `PHYSMAP_BASE + p`, uma base fixa somada a
todo endereço físico.

**Para pensar.**

- Se o Capivara usasse base e limite para cada aplicativo, onde ficariam as
  pilhas das tarefas? E o que acontece quando um aplicativo precisa de mais
  heap e a memória logo acima já é de outro?

## Cap. 16 · Segmentation

**No livro.** A segmentação generaliza a base e limite: um par por **segmento**
(código, heap, pilha), cada um podendo ficar num lugar diferente da memória
física. Isso evita reservar o buraco entre heap e pilha, mas cria
**fragmentação externa**: sobram buracos de tamanhos variados entre os
segmentos.

**No Capivara.** O x86 ainda tem segmentos, herança dos processadores de 16 e
32 bits. A tabela deles, a GDT, está em `kernel/arch/x86_64/gdt.c`. No modo de 64
bits a base e o limite são ignorados: os segmentos de código e dados
(`0x08` e `0x10`) cobrem tudo. O que sobra de útil é o **nível de privilégio**
(DPL): os segmentos `0x18` e `0x20` têm DPL 3 e serão os do modo usuário.

**Para pensar.**

- Leia os valores de `gdt[1]` a `gdt[4]` em `gdt.c`. Onde fica o bit L (64
  bits)? E o DPL? (Use a tabela de descritores do manual da Intel ou da AMD.)
- A fragmentação externa do livro também aparece no heap do Capivara. Rode
  `heap` e veja a linha "fragmentação externa".
