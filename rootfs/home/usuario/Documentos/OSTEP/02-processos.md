# Processos e execução direta limitada

## Cap. 4 · The Abstraction: The Process

**No livro.** Um processo é um programa em execução. O sistema guarda, para
cada um, o estado da máquina que ele usa: registradores (incluindo o contador
de programa e a pilha), a memória e os arquivos abertos. Um processo passa por
estados: **pronto**, **rodando** e **bloqueado** (esperando E/S, por exemplo).
O sistema mantém uma lista de processos com essas informações.

**No Capivara.** O papel do processo é feito pela **tarefa**, a estrutura
`task_t` em `kernel/sched/task.h`. Ela guarda:

- `regs`: os registradores salvos da última vez que a tarefa parou;
- `stack`: a pilha dela, 64 KiB tirados do heap;
- `fpu`: os 512 bytes do estado SSE, salvos com a instrução FXSAVE;
- `state`: pronta, rodando, dormindo, esperando ou morta;
- `evq`: a fila de eventos (teclas, cliques) que chegam para ela.

A diferença para o livro: a tarefa **não tem memória própria**. Todas
enxergam o mesmo espaço de endereçamento, então se parecem mais com as
*threads* do cap. 26 do que com processos.

Os estados do livro estão no enum `task_state_t`. "Dormindo" e "esperando" são
dois tipos de bloqueado: um espera o tempo passar (`task_sleep`), o outro espera
alguém acordar a tarefa (`task_block` e `task_wake`).

**Experimente.** Liste as tarefas e o estado de cada uma. Abra a Calculadora e
rode de novo:

```sh
ps
```

```sh
abrir calc
```

## Cap. 5 · Interlude: Process API

**No livro.** No Unix, um processo cria outro com `fork()`, que duplica o
processo inteiro. O filho troca de programa com `exec()`, e o pai espera o
filho terminar com `wait()`. Separar criar de trocar de programa é o que
permite ao shell redirecionar a saída antes do `exec`.

**No Capivara.** Não há `fork` nem `exec`: não existe programa separado do
kernel para carregar. Uma tarefa nova nasce com `task_create(nome, função,
argumento)`, que se parece mais com `pthread_create` do cap. 27. O "programa"
é uma função do kernel, e o `app_launch` em `kernel/apps/apps.c` faz isso
para os aplicativos.

Veja o truque de `task_create`: ele **fabrica** um `regs_t` no topo da pilha
nova, como se a tarefa tivesse sido interrompida no começo da função. Quando o
escalonador escolher essa tarefa pela primeira vez, restaurar esses
registradores faz a função começar. O endereço de retorno falso
(`task_trampoline_exit`) faz a tarefa terminar sozinha se a função retornar.

O Terminal tem redirecionamento com `>`, mas ele é feito dentro do próprio
comando `echo`, e não com `fork` e troca de saída padrão.

**Experimente.** Uma tarefa nova aparece no log do kernel:

```sh
abrir paint
```

```sh
dmesg
```

**Para pensar.**

- O que precisaria existir no Capivara para implementar `fork()`? Pense na
  memória: o que significa "duplicar" um espaço de endereçamento que é
  compartilhado por todo mundo?
- `kill` no Capivara não mata a tarefa na hora. Leia `task_kill` e
  `apply_kill`. Por que esperar a tarefa não estar segurando nenhum mutex?

## Cap. 6 · Mechanism: Limited Direct Execution

**No livro.** Para ser rápido, o programa roda **direto na CPU**. Para o
sistema não perder o controle, a execução é **limitada** de duas formas. O
programa roda em **modo usuário**, sem poder fazer operações perigosas, e pede
serviços ao kernel com uma *trap* (chamada de sistema). E um **timer**
interrompe a CPU de tempos em tempos, devolvendo o controle ao kernel, que
pode trocar de processo. Essa troca, a **troca de contexto**, salva os
registradores de um e restaura os de outro.

**No Capivara.** A metade do timer existe. A metade do modo usuário, não:

- O PIT (`kernel/arch/x86_64/pit.c`) interrompe a CPU **1000 vezes por
  segundo**. A cada interrupção, `sched_tick` conta o tempo; a cada 10, a fatia
  da tarefa acaba.
- A troca de contexto está em `kernel/arch/x86_64/isr.S` e em `sched_switch`.
  O stub em assembly empilha todos os registradores da tarefa interrompida e
  chama `isr_dispatch`. Se ela devolver o ponteiro para os registradores de
  **outra** tarefa, o stub troca a pilha e restaura os registradores dessa
  outra. O `iretq` final "volta" para a outra tarefa.
- Ceder a vez voluntariamente (`task_yield`) usa o mesmo caminho: a instrução
  `int $0x81` provoca uma interrupção de software.
- Não existe modo usuário: as tarefas rodam no anel 0 e podem tudo. A GDT
  (`kernel/arch/x86_64/gdt.c`) já tem os segmentos de usuário (DPL 3) e a TSS,
  preparados para quando o modo usuário existir.

**Experimente.** Conte as trocas de contexto de verdade, por tarefa:

```sh
trocas
```

Agora deixe a CPU ocupada: abra um segundo Terminal (Super+T) e rode nele a
corrida abaixo, que leva alguns segundos. Enquanto ela roda, rode `trocas` no
primeiro e compare as trocas involuntárias.

```sh
corrida trava 200000000
```

**Para pensar.**

- No `trocas`, a tarefa "ocioso" fica com quase todo o tempo de CPU. O que ela
  faz? (Leia `task_idle_loop`.)
- Por que o estado SSE (`fpu`) é salvo só quando a próxima tarefa é diferente
  da atual?
- O livro mostra o kernel trocando de pilha ao entrar no modo kernel. No
  Capivara isso acontece? Procure `tss_set_kernel_stack`: para que ela vai
  servir quando houver modo usuário?
