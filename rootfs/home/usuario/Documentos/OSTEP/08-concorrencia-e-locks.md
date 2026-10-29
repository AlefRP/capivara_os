# Concorrência: threads, condição de corrida e locks

## Cap. 26 · Concurrency: An Introduction

**No livro.** Uma *thread* é como um processo que divide a memória com outras
threads do mesmo programa: cada uma tem os seus registradores e a sua pilha,
mas o heap e as variáveis globais são de todas. O capítulo mostra o exemplo
clássico: duas threads somando 1 num contador compartilhado muitas vezes. O
resultado sai errado, e diferente a cada execução, porque `contador = contador +
1` são três passos (ler, somar, escrever). Se a troca de contexto cai no meio,
uma soma se perde. Isso é uma **condição de corrida**, o trecho que mexe no
dado compartilhado é a **região crítica**, e o que se quer é **exclusão
mútua**: uma thread por vez lá dentro.

**No Capivara.** As tarefas **são** threads nesse sentido: todas dividem a
memória do kernel. O laboratório `corrida` cria duas tarefas ("corrida A" e
"corrida B") que fazem exatamente o exemplo do livro. Veja `race_worker` em
`kernel/apps/ostep.c`: a soma está escrita em dois passos, `v = contador` e
`contador = v + 1`, para deixar a região crítica à vista.

**Experimente.** Rode algumas vezes e veja o resultado mudar:

```sh
corrida
```

```sh
corrida
```

Com o mutex, o resultado fica sempre certo, mas demora mais:

```sh
corrida trava
```

Com poucas somas, cada tarefa termina dentro de uma fatia de 10 ms, e a corrida
quase nunca aparece:

```sh
corrida 100000
```

**Para pensar.**

- Quando uma corrida acontece, perde-se uma soma só ou milhares de uma vez? Por
  quê? (Pense no que a outra tarefa faz durante os 10 ms de fatia dela.)
- Com uma CPU só, por que existe corrida? O que precisa acontecer entre ler e
  escrever?

## Cap. 27 · Interlude: Thread API

**No livro.** A API POSIX: `pthread_create` cria uma thread, `pthread_join`
espera ela terminar, `pthread_mutex_lock` e `unlock` fazem a exclusão mútua, e
`pthread_cond_wait` e `signal` esperam e avisam condições.

**No Capivara.** Os equivalentes:

- `task_create(nome, função, argumento)` cria;
- `task_create_joinable` cria uma tarefa cujo término pode ser aguardado por
  `task_join`; ela conserva o estado final até que ele seja recolhido;
- `task_join(tarefa, info)` bloqueia quem espera e devolve uma cópia das
  métricas finais. O laboratório `prodcons` usa essa API;
- o `corrida` conserva a espera por um contador `done`, somado com uma
  instrução atômica (`__atomic_add_fetch`), e `task_sleep(5)` entre consultas,
  para comparar esse *polling* com o bloqueio do `prodcons`;
- `mutex_lock` e `mutex_unlock` em `kernel/sched/task.c`;
- para esperar uma condição, `task_block` e `task_wake` (cap. 30).

**Para pensar.**

- Esperar com `task_sleep(5)` num laço é *polling*. Leia `task_join` e
  `task_exit`: onde quem espera bloqueia, e onde é acordado?
- Por que uma tarefa criada com `task_create_joinable` precisa preservar o
  estado depois de terminar, mesmo que `task_join` ainda não tenha sido chamado?

## Cap. 28 · Locks

**No livro.** Um lock precisa ser **correto** (exclusão mútua), **justo**
(ninguém espera para sempre) e **rápido**. Opções:

- **desligar interrupções** na região crítica: funciona com uma CPU, mas é
  perigoso nas mãos de programas e não serve com várias CPUs;
- instruções atômicas do hardware: **test-and-set**, **compare-and-swap**,
  *load-linked/store-conditional*, *fetch-and-add* (que dá o *ticket lock*,
  justo);
- **spin lock**: girar até o lock soltar; bom se a espera é curta, péssimo com
  uma CPU só (quem gira impede o dono de rodar);
- **ceder a vez** (*yield*) em vez de girar, ou dormir numa fila até ser
  acordado.

**No Capivara.** O `mutex_lock` (`kernel/sched/task.c`) junta duas ideias do
capítulo:

1. Testa e marca o lock **com as interrupções desligadas** (`irq_save` e
   `irq_restore`). Com uma CPU só, sem interrupção não há troca de contexto, e
   o "testar e marcar" vira atômico.
2. Se o lock está ocupado, chama `task_yield()`: cede a CPU em vez de girar.

É a versão "lock com yield" do livro. Ela é correta numa CPU, mas não é justa
(uma tarefa pode sempre perder a vez) e não serve para várias CPUs. O mutex
também conta quantos locks cada tarefa segura (`lock_depth`): o `kill` só mata a
tarefa quando ela não segura nenhum, para não deixar um lock preso para sempre.

O resto do kernel usa direto o truque 1: `irq_save` aparece no heap, no
escalonador e na fila de eventos. Dentro de uma interrupção, mutex é
proibido (regra do projeto): a interrupção não pode ceder a vez, porque não é
uma tarefa.

**Experimente.** Compare os tempos com e sem lock. O mutex custa caro porque
desliga e religa as interrupções a cada soma:

```sh
corrida 20000000
```

```sh
corrida trava 20000000
```

**Para pensar.**

- Reescreva o `mutex_lock` com `__atomic_test_and_set` (o test-and-set do GCC)
  em vez de desligar interrupções. Ele continuaria correto com uma CPU? E com
  duas?
- Por que um *spin lock* puro, sem yield, seria uma péssima ideia no Capivara?
- O livro mostra o *ticket lock*. Que problema dele o mutex do Capivara tem?

## Cap. 29 · Lock-based Concurrent Data Structures

**No livro.** Para tornar uma estrutura de dados segura com threads, o jeito
simples é um lock grande para ela toda. Ele é correto, mas não escala. As
alternativas são locks mais finos: um contador "aproximado" com contadores
locais por CPU, listas com um lock por nó (*hand-over-hand*), filas com um lock
na cabeça e outro na cauda, tabelas *hash* com um lock por balde.

**No Capivara.** Quase tudo usa um lock grande:

- o sistema de arquivos tem `vfs_lock` e `vfs_unlock`, um mutex para a árvore
  inteira;
- o compositor de janelas (`kernel/gui/wm.c`) protege a lista de janelas com um
  mutex só;
- o heap e a fila de eventos de cada tarefa desligam as interrupções.

Com uma CPU, um lock grande é razoável: não há duas tarefas rodando de fato ao
mesmo tempo.

**Para pensar.**

- A fila de eventos (`kernel/sched/event.c`) é um *buffer* circular. Quem
  escreve (`ev_post`) pode ser uma interrupção; quem lê (`ev_poll`) é a tarefa.
  Por que aqui não dá para usar mutex?
