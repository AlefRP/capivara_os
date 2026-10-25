# Variáveis de condição, semáforos, erros e eventos

## Cap. 30 · Condition Variables

**No livro.** Às vezes uma thread precisa **esperar** algo acontecer: uma fila
ter dado, outra thread terminar. Girar conferindo a condição desperdiça CPU. A
**variável de condição** deixa a thread dormir (`wait`) até alguém avisar
(`signal`). Duas regras do capítulo: segure o lock ao mudar a condição e ao
esperar, e confira a condição **num laço** (`while`, e não `if`), porque
quando a thread acorda a condição pode já ter mudado. O exemplo central é o
**produtor e consumidor** com um *buffer* limitado.

**No Capivara.** `ev_wait` (`kernel/sched/event.c`) é exatamente esse padrão,
com interrupções desligadas no lugar do lock:

```c
for (;;) {
    if (ev_poll(e)) return true;          /* a condição: tem evento na fila? */
    f = irq_save();                       /* "pega o lock" */
    if (!t->evq_count) {                  /* confere de novo, já protegido */
        t->waiting_event = true;
        t->state = TASK_BLOCKED;          /* "wait": dorme */
        task_yield();
        t->waiting_event = false;
    }
    irq_restore(f);
}
```

O `signal` está em `ev_post`: ao colocar um evento na fila, se a tarefa está
esperando (`waiting_event`), chama `task_wake`. A fila de eventos é um
produtor e consumidor: o compositor (ou uma interrupção) produz, o aplicativo
consome. Quando a fila enche (128 eventos), o evento novo é descartado, em vez
de o produtor esperar.

Por que conferir de novo depois do `irq_save`? Sem isso, um evento poderia
chegar entre o `ev_poll` e o bloqueio, e a tarefa dormiria com um evento na
fila: um "acordar perdido".

**Para pensar.**

- Onde está o `while` do livro no código acima?
- Descartar quando a fila enche é uma escolha. Leia `ev_post`: que eventos são
  "fundidos" para a fila encher menos? Por que isso é seguro para o movimento do
  mouse e não seria para um clique?

## Cap. 31 · Semaphores

**No livro.** O semáforo é um inteiro com duas operações: `wait` (diminui e,
se ficou negativo, dorme) e `post` (aumenta e acorda alguém). Com valor
inicial 1, ele é um lock; com 0, serve para esperar um evento; com N, conta
recursos (como lugares livres num *buffer*). O capítulo resolve com ele o
produtor e consumidor, o problema dos leitores e escritores e o jantar dos
filósofos, e mostra que dá para construir semáforos com lock e variável de
condição.

**No Capivara.** Não há semáforos. Construir um é um bom exercício, com as
peças que já existem:

- um contador e uma lista de tarefas esperando, dentro de uma estrutura;
- `irq_save` e `irq_restore` para proteger os dois;
- `task_block` para dormir e `task_wake` para acordar.

**Para pensar.**

- Escreva `sem_wait` e `sem_post` para o Capivara (em papel ou no código). Onde
  pode acontecer um acordar perdido, e como o `irq_save` evita?
- Com o seu semáforo, como o laboratório `corrida` poderia esperar as duas
  tarefas sem *polling*?

## Cap. 32 · Common Concurrency Problems

**No livro.** Um estudo de erros reais mostra dois tipos de erro que não são
*deadlock*: **violação de atomicidade** (um trecho que devia ser indivisível é
interrompido) e **violação de ordem** (uma thread assume que outra já fez algo).
O **deadlock** precisa de quatro condições juntas: exclusão mútua, segurar e
esperar, sem preempção e espera circular. Para evitar, basta quebrar uma
delas, por exemplo pegando os locks sempre na mesma ordem.

**No Capivara.** Três regras do projeto existem para evitar esses erros:

- **Nada de mutex dentro de interrupção.** Se uma interrupção tentasse pegar um
  mutex que a tarefa interrompida segura, ela esperaria para sempre: a tarefa
  só solta o mutex quando voltar a rodar, e ela só volta quando a interrupção
  terminar. É um deadlock com uma CPU só.
- **Nada de `kmalloc` dentro de interrupção.** O heap desliga interrupções, mas
  uma interrupção que chegasse no meio de um `kmalloc` da tarefa veria a lista
  pela metade (violação de atomicidade).
- **`kill` espera o `lock_depth` zerar**, para não matar uma tarefa que segura
  um lock.

**Para pensar.**

- Se o compositor pegasse o lock das janelas e depois o lock do sistema de
  arquivos, e o Explorador pegasse os dois na ordem contrária, o que poderia
  acontecer? Como a regra "sempre na mesma ordem" resolve?
- Ache, no `kernel/gui/wm.c`, um lugar onde o mutex é solto antes de chamar
  outra função. Por quê?

## Cap. 33 · Event-based Concurrency (Advanced)

**No livro.** Outra forma de concorrência, sem threads: um **laço de eventos**
espera algo acontecer (com `select` ou `poll`), trata o evento e volta a
esperar. Como só uma coisa roda por vez, não há locks. Em troca, o tratador
**não pode bloquear**: uma operação lenta trava o laço inteiro. Daí vêm as
operações assíncronas e o estado que precisa ser guardado entre um evento e
outro (a "continuação").

**No Capivara.** Cada aplicativo é um laço de eventos: `gui_wait` espera,
`switch` trata, e o app redesenha. O compositor (`kernel/gui/wm.c`) é o
"select" do sistema: recebe teclas e cliques das interrupções e entrega cada
um à fila do aplicativo certo.

O problema do tratador que bloqueia aparece de verdade: enquanto o Terminal
roda um comando demorado, ele não trata eventos, e a janela para de
responder. Um jogo faz o contrário: em vez de dormir em `gui_wait`, ele usa
`gui_poll`, que não bloqueia, e assim consegue desenhar 60 quadros por
segundo mesmo sem receber nenhum evento.

**Experimente.** Rode uma corrida longa e tente digitar no mesmo Terminal
enquanto ela roda:

```sh
corrida trava 300000000
```

**Para pensar.**

- O Terminal "travado" deixou o resto do sistema travado também? Por quê não?
- Como o Terminal poderia rodar comandos longos sem parar de responder? (Pense
  numa tarefa só para o comando, ou em quebrar o trabalho em pedaços.)
