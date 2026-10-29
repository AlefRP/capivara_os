# Variáveis de condição, semáforos, erros e eventos

## Cap. 30 · Condition Variables

**No livro.** Às vezes uma thread precisa **esperar** algo acontecer: uma fila
ter dado, outra thread terminar. Girar conferindo a condição desperdiça CPU. A
**variável de condição** deixa a thread dormir (`wait`) até alguém avisar
(`signal`). Duas regras do capítulo: segure o lock ao mudar a condição e ao
esperar, e confira a condição **num laço** (`while`, e não `if`), porque
quando a thread acorda a condição pode já ter mudado. O exemplo central é o
**produtor e consumidor** com um *buffer* limitado.

**No Capivara.** Ainda não há uma API geral de variáveis de condição.
`ev_wait` (`kernel/sched/event.c`) aplica o padrão de conferir a condição e
bloquear, com interrupções desligadas no lugar do lock:

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

**No livro.** Um modelo de semáforo usa um inteiro com duas operações:
`wait` diminui e, se ficou negativo, dorme; `post` aumenta e acorda alguém.
Nesse modelo, a contagem negativa representa tarefas esperando. Com valor
inicial 1, ele é um lock; com 0, serve para esperar um evento; com N, conta
recursos (como lugares livres num *buffer*). O capítulo resolve com ele o
produtor e consumidor, o problema dos leitores e escritores e o jantar dos
filósofos, e mostra que dá para construir semáforos com lock e variável de
condição.

**No Capivara.** `sem_init`, `sem_wait` e `sem_post`, em
`kernel/sched/task.c`, implementam semáforos para tarefas reais. Aqui o
contador representa os recursos disponíveis e nunca fica negativo:
`sem_wait` consome um recurso quando há algum; caso contrário, bloqueia a
tarefa numa fila. Se há alguém esperando, `sem_post` entrega o recurso
diretamente ao primeiro da fila e o acorda. Sem esperadores, aumenta a
contagem disponível. Assim, uma tarefa nova não toma o recurso já entregue
a quem foi acordado; a contagem pode continuar zero durante esse despertar.
A contagem e a fila são protegidas por `irq_save`/`irq_restore`, numa CPU.
Essa entrega de recurso difere da sinalização de uma variável de condição,
na qual a tarefa acordada precisa reavaliar a condição num laço.
Se uma tarefa for finalizada antes de concluir a espera, um recurso que já
lhe tenha sido entregue é devolvido ao semáforo ou ao próximo esperador.

O laboratório `prodcons` usa uma fila circular limitada. O semáforo de vagas
começa em `b`; o de itens, em zero. Um produtor espera uma vaga, insere um
item e publica um item disponível; um consumidor faz o caminho inverso.
A alteração da fila também precisa de exclusão mútua: contar itens e vagas
não protege sozinho a cabeça, a cauda e os dados do buffer.

**Experimente.** Antes de rodar, calcule o total de itens: `p × n`.

```sh
prodcons polling p=2 c=2 b=4 n=40 atraso=1
prodcons semaforo p=2 c=2 b=4 n=40 atraso=1
```

São dois produtores, dois consumidores, buffer de quatro posições e 40
itens por produtor: 80 itens no total. O atraso é uma pausa de 1 ms dos
produtores antes de publicar cada item. Em `polling`, a tarefa consulta a
fila e chama
`task_yield` enquanto espera; continua pronta para ser escalonada. Em
`semaforo`, ela fica bloqueada até ser acordada. Os dois modos usam tarefas
reais e protegem a fila. `task_join` aguarda o término dos trabalhadores.

Confira que cada ID foi consumido uma vez, que os totais são 80, que a
ocupação nunca ultrapassou quatro e que a fila terminou vazia. Compare
tempo, CPU, vezes na CPU, yields, bloqueios e consultas sem progresso.
CPU é uma estimativa por ticks de 1 ms; zero pode significar uma tarefa
rápida demais para ser amostrada. Tempos e ordem de execução variam; a
integridade deve permanecer correta.
As métricas de CPU, yields e bloqueios somam os trabalhadores, excluindo a
espera pela partida e a tarefa que coordena o laboratório.

Repita mudando só `b=1`, depois a quantidade de trabalhadores e o atraso.
Os limites são `p` e `c` de 1 a 4, `b` de 1 a 64, `n` de 1 a 10 000 itens
por produtor e `atraso` de 0 a 20 ms. Sem opções, os valores são os do
exemplo acima. O experimento roda com uma CPU e tarefas no anel 0.
Os trabalhadores ficam protegidos contra finalização durante a execução.
Um pedido para finalizar o Terminal espera o laboratório concluir e recolher
as tarefas; matar só um trabalhador poderia deixar os outros sem progresso.

Para verificar semáforos e join separadamente, rode:

```sh
prodcons teste
```

São dez regressões em `kernel/apps/ostep_sync.c`: conservação de créditos,
esperas, cancelamento, ciclos de join e coleta de tarefas. O roteiro
`tests/prodcons.txt` também exercita nove configurações da fila no QEMU e no
CI. As verificações exigem correção; tempo e métricas de desempenho variam.

**Para pensar.**

- Leia `sem_wait` e `sem_post`. Onde poderia acontecer um acordar perdido,
  e como o trecho protegido evita que o recurso chegue antes do bloqueio?
- Por que `sem_post` pode acordar uma tarefa mantendo a contagem em zero?
  Como a entrega direta impede que outra tarefa tome esse recurso?
- Com buffer de uma posição, o que impede dois produtores de sobrescrever
  o mesmo item? Qual é o papel dos semáforos e qual é o da região crítica?
- Por que terminar rápido e consumir o total certo ainda não basta para
  provar que nenhum ID foi perdido ou duplicado?

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
