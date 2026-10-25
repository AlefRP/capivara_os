# Além da memória física: troca e substituição

## Cap. 21 · Beyond Physical Memory: Mechanisms

**No livro.** Para os processos usarem mais memória do que existe, o sistema
guarda páginas pouco usadas no disco, na área de **troca** (*swap*). O bit
"presente" da entrada diz se a página está na memória. Se não estiver, o
hardware gera um **page fault**, e o sistema traz a página do disco, ajusta a
tabela e repete a instrução. Para ter sempre quadros livres, um processo de
fundo (o *swap daemon*) libera páginas quando a memória livre fica abaixo de um
limite.

**No Capivara.** Não há disco, então não há troca. O *page fault* existe: é a
exceção 14 do processador. Hoje, o tratador em `kernel/arch/x86_64/idt.c` não
tenta consertar nada: mostra a tela de erro com o endereço que falhou (o
registrador CR2, lido em `kernel/panic.c`) e para o sistema.

O caminho até a troca seria este: no *page fault*, ler o CR2, descobrir se a
página foi para o disco (anotando isso nos bits livres da entrada ausente),
trazer de volta, consertar a entrada, fazer `invlpg` e retornar da exceção para
repetir a instrução.

**Para pensar.**

- O mapeamento do endereço 0 foi apagado de propósito (`vmm_init`). O que
  acontece se um aplicativo usar um ponteiro nulo? Isso é um *page fault* "bom"
  ou "ruim"?
- Para retornar de um *page fault* e repetir a instrução, o que precisa estar
  salvo na pilha? Leia `isr.S`.

## Cap. 22 · Beyond Physical Memory: Policies

**No livro.** Quando a memória enche, qual página tirar? A política **ótima**
(OPT) tira a que vai ser usada mais tarde, mas exige conhecer o futuro: serve
de régua para comparar as outras. **FIFO** tira a mais antiga e pode até
piorar com mais memória (a **anomalia de Belady**). **LRU** tira a usada há
mais tempo, aproveitando a localidade, mas é cara de implementar exatamente. O
**relógio** aproxima o LRU com o bit de uso: um ponteiro gira pelos quadros,
apaga os bits ligados e tira a primeira página com o bit desligado. O
capítulo também fala de páginas sujas (que custam uma escrita no disco) e do
*thrashing*.

**No Capivara.** O simulador `paginas` segue as quatro políticas passo a passo.
Sem argumentos, ele usa a mesma sequência de referências dos exemplos do
capítulo, para você comparar com as tabelas do livro. "falta (1ª)" é a falta
obrigatória (*cold miss*), a primeira vez que a página aparece.

**Experimente.**

```sh
paginas opt 3
```

```sh
paginas fifo 3
```

```sh
paginas lru 3
```

```sh
paginas relogio 3
```

A anomalia de Belady:

```sh
paginas belady
```

Uma sequência sua, com localidade (as páginas 1 e 2 se repetem muito):

```sh
paginas lru 3 1 2 1 2 3 1 2 4 1 2 5 1 2
```

**Para pensar.**

- Na sequência padrão, qual é a taxa de acerto de cada política? Alguma ganha
  do OPT? Por quê?
- Por que a anomalia de Belady não acontece com LRU? (Pense no conjunto de
  páginas guardadas com k quadros e com k+1 quadros.)
- O relógio do simulador não olha o bit "suja". Como você mudaria a escolha para
  preferir páginas limpas?

## Cap. 23 · Complete Virtual Memory Systems

**No livro.** O capítulo junta tudo em dois sistemas reais, o VAX/VMS e o
Linux. Aparecem truques como dividir o espaço entre o processo (embaixo) e o
kernel (em cima, igual em todos), **copy-on-write** (o `fork` não copia nada até
alguém escrever), carregar páginas só quando forem usadas (*demand zeroing*) e
proteções contra ataques, como a aleatoriedade de endereços.

**No Capivara.** O kernel já fica em cima e é o mesmo para todos, como no Linux:
isso é o que vai permitir cada processo ter a sua metade de baixo. O
physmap também é ideia do Linux (lá, o *direct map*).

**Para pensar.**

- Para ter processos, cada um precisa de uma PML4 própria. Que entradas dessa
  PML4 seriam copiadas da PML4 do kernel? (Dica: os índices de 256 a 511 cobrem a
  metade de cima.)
- Como você implementaria *copy-on-write* com os bits que o `traduzir` mostra?
