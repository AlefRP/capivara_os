# Escalonamento da CPU

O escalonador do Capivara é simples: **round-robin** com fatia de 10 ms. Para
as outras políticas dos capítulos, use o simulador `escalonar`. Ele segue as
regras de cada política uma unidade de tempo por vez, desenha quem rodou em
cada instante e calcula as métricas do livro.

Cada tarefa é escrita como `duração`, `chegada:duração` ou
`chegada:duração:bilhetes`. As métricas são:

- **retorno** (*turnaround*): fim menos chegada;
- **resposta** (*response*): primeira vez na CPU menos chegada;
- **espera**: retorno menos duração, ou seja, o tempo parado na fila.

## Cap. 7 · Scheduling: Introduction

**No livro.** Com hipóteses simplificadoras (todo trabalho chega junto, dura o
mesmo e não faz E/S), o capítulo compara políticas. **FIFO** sofre com o
*efeito comboio*: um trabalho longo na frente atrasa os curtos. **SJF** (o mais
curto primeiro) resolve isso quando todos chegam juntos. **STCF** (menor tempo
restante, com preempção) resolve quando eles chegam em tempos diferentes.
Essas políticas são boas para o **retorno**, mas ruins para a **resposta**,
que importa em sistemas interativos. Para a resposta, o **round-robin** dá uma
fatia a cada um, em rodízio. Ele é justo, mas piora o retorno. Fatias muito
pequenas também custam caro, porque cada troca de contexto tem um preço.

**No Capivara.** `sched_switch` em `kernel/sched/task.c` é um round-robin
puro: procura, a partir da tarefa atual, a próxima tarefa **pronta** da lista.
A "ociosa" só roda se não houver mais ninguém. A fatia (`TIMESLICE_MS`) é de 10
ms, e o `sched_tick` a desconta a cada milissegundo. Como quase todas as
tarefas passam o tempo esperando eventos, a fatia raramente acaba: elas cedem
a CPU antes, ao chamar `gui_wait`.

**Experimente.** O comboio do FIFO, e como o SJF o desfaz:

```sh
escalonar fifo 0:100 0:10 0:10
```

```sh
escalonar sjf 0:100 0:10 0:10
```

Chegadas diferentes: compare SJF e STCF.

```sh
escalonar sjf 0:100 10:10 10:10
```

```sh
escalonar stcf 0:100 10:10 10:10
```

Resposta contra retorno no round-robin:

```sh
escalonar rr q=1 0:5 0:5 0:5
```

```sh
escalonar rr q=5 0:5 0:5 0:5
```

**Para pensar.**

- No FIFO com 100, 10 e 10, qual é o retorno médio? E no SJF? Confira com o
  simulador e com as contas do livro.
- Com quantum 1, a resposta é ótima. Por que ninguém usa um quantum de 1 ms
  num sistema de verdade? (Pense no custo de cada troca de contexto.)
- O Capivara tem fatia de 10 ms. Leia `sched_tick`: o que acontece com a fatia
  de uma tarefa que acorda no meio da fatia de outra?

## Cap. 8 · Scheduling: The Multi-Level Feedback Queue

**No livro.** A MLFQ tenta ser boa em retorno e em resposta sem saber a duração
dos trabalhos. Ela tem várias filas com prioridades diferentes e **aprende**
com o comportamento: quem usa a fatia inteira desce de fila (parece um trabalho
longo, de CPU); quem cede a CPU antes fica em cima (parece interativo). Para
ninguém passar fome nas filas de baixo, de tempos em tempos todos voltam para o
topo (o *boost*). E, para não enganarem o sistema, a cota de cada fila conta o
tempo total gasto nela, e não só cada rodada.

**No Capivara.** Não há prioridades: todas as tarefas são iguais no
round-robin. O simulador usa 3 filas; o quantum é `q`, `2q` e `4q` da fila 0
(a do topo) para a 2. Tarefa nova entra no topo, quem gasta a cota da fila
desce um nível, e uma tarefa numa fila mais alta toma a CPU de quem está numa
mais baixa. O parâmetro `s` é o intervalo do boost (0 desliga).

**Experimente.** Um trabalho longo e um curto que chega depois. O curto
começa na hora, porque entra na fila do topo:

```sh
escalonar mlfq q=2 0:60 10:6
```

Três longos e o boost:

```sh
escalonar mlfq q=2 0:40 0:40 0:40
```

```sh
escalonar mlfq q=2 s=20 0:40 0:40 0:40
```

**Para pensar.**

- Sem boost, o que acontece com um trabalho longo quando chegam trabalhos
  curtos sem parar? Simule com vários trabalhos chegando em tempos seguidos.
- O livro discute um programa que cede a CPU um pouco antes do fim da fatia
  para ficar sempre no topo. A regra de "cota total por fila" evita isso como?
- Para dar prioridade a um aplicativo no Capivara (um jogo, por exemplo), o que
  você mudaria em `task_t` e em `sched_switch`?

## Cap. 9 · Scheduling: Proportional Share

**No livro.** Em vez de otimizar retorno ou resposta, estas políticas dividem a
CPU em **proporção**. No escalonamento por **loteria**, cada processo tem
bilhetes, e a cada fatia o sistema sorteia um: quem tem 75% dos bilhetes
roda, em média, 75% do tempo. O **stride** faz a mesma divisão sem sorteio,
com um contador por processo. O capítulo também mostra o CFS do Linux, que
procura sempre quem rodou menos (o menor *vruntime*).

**No Capivara.** Não há bilhetes, mas o Gerenciador de Tarefas mostra a fatia
de CPU que cada tarefa usou no último segundo (o campo `cpu_percent`,
recalculado em `sched_tick`). O simulador `escalonar loteria` sorteia a cada
unidade de tempo, com um gerador de números pseudoaleatórios (xorshift). A
`semente` muda o sorteio.

**Experimente.** Mesma duração, bilhetes diferentes:

```sh
escalonar loteria 0:40:75 0:40:25
```

```sh
escalonar loteria semente=7 0:40:75 0:40:25
```

**Para pensar.**

- Com trabalhos curtos, a divisão sai longe de 75/25. Por quê? Aumente as
  durações e veja a proporção se aproximar.
- O livro mede a "justiça" pela razão entre os tempos de fim de dois trabalhos
  iguais. Calcule essa razão para algumas sementes.

## Cap. 10 · Multiprocessor Scheduling (Advanced)

**No livro.** Com várias CPUs aparecem problemas novos. Cada CPU tem um cache
próprio, então é melhor um processo continuar na mesma CPU (**afinidade**). Uma
fila única para todas as CPUs é simples, mas precisa de lock e espalha os
processos. Filas separadas por CPU escalam melhor, mas precisam equilibrar a
carga, por exemplo uma CPU "roubando" trabalho da outra.

**No Capivara.** Uma CPU só. Suportar várias é um item do roteiro e mexe em
quase tudo: os outros núcleos precisam ser acordados (pelo APIC), cada um
precisa da sua tarefa atual e da sua fatia, e o truque de **desligar
interrupções** para proteger dados (usado em todo o kernel) deixa de
funcionar, porque a outra CPU continua rodando.

**Para pensar.**

- Procure `irq_save` no código do escalonador e do heap. Com duas CPUs, essas
  partes ficariam corretas? O que precisaria entrar no lugar? (Volte aqui
  depois do cap. 28.)
