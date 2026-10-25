# Gerência de espaço livre

## Cap. 17 · Free-Space Management

**No livro.** Um alocador como o `malloc` gerencia blocos de tamanho variável.
As peças são sempre as mesmas:

- uma **lista de livres**, que diz onde há espaço;
- **divisão** (*splitting*): um pedido pequeno corta um pedaço de um bloco
  livre grande, e o resto continua livre;
- **junção** (*coalescing*): ao liberar, blocos livres vizinhos viram um só,
  para a memória não virar uma coleção de migalhas;
- um **cabeçalho** antes de cada bloco, com o tamanho (é assim que o `free`
  sabe quanto liberar sem receber o tamanho) e, às vezes, um número mágico
  para detectar erros.

As políticas de escolha são *best fit* (o menor bloco que serve), *worst fit*
(o maior), *first fit* (o primeiro que serve) e *next fit* (o primeiro a partir
de onde parou a última busca). O capítulo termina com alocadores segregados,
*slab* e *buddy*.

**No Capivara.** O heap do kernel (`kernel/mm/heap.c`) é quase um exemplo do
capítulo:

- **First fit**: `kmalloc` percorre a lista desde o começo e pega o primeiro
  bloco livre grande o bastante.
- **Cabeçalho**: a estrutura `block_t`, com 32 bytes: tamanho, número mágico
  (`MAGIC_USED` ou `MAGIC_FREE`), a tarefa dona e ponteiros para o bloco
  anterior e o seguinte. Os 32 bytes mantêm os dados alinhados em 16.
- **Divisão**: `split` corta o bloco se sobrarem pelo menos 64 bytes
  (`MIN_SPLIT`); senão, entrega o bloco inteiro (um pouco de fragmentação
  interna).
- **Junção**: `kfree` funde o bloco com o seguinte e com o anterior, se estiverem
  livres.

Uma diferença: a lista do Capivara tem **todos** os blocos, livres e usados, em
ordem de endereço. A do livro guarda só os livres. A do Capivara é mais simples
e facilita a junção (os vizinhos estão nos ponteiros `prev` e `next`), mas a
busca passa pelos blocos usados também.

A memória **física** usa outra estrutura: um **bitmap** com um bit por página
de 4 KiB (`kernel/mm/pmm.c`). Como todas as páginas têm o mesmo tamanho, não há
divisão nem junção, e não existe fragmentação externa.

**Experimente.** Divisão e junção ao vivo, no heap de verdade:

```sh
heap teste
```

O estado do heap e a fragmentação:

```sh
heap
```

```sh
heap blocos 40
```

**Para pensar.**

- No `heap teste`, um `kmalloc(100)` ocupa 144 bytes. De onde vêm os 44 a mais?
- Depois do passo 3, por que o bloco livre tem 384 bytes, e não 300?
- Troque, de cabeça, a política para *best fit*: o que muda no laço de
  `kmalloc`? Ela seria mais lenta ou mais rápida? Fragmentaria mais ou menos?
- O bitmap de páginas físicas guarda um "palpite" (`next_hint`) de onde
  procurar. Que política do livro isso lembra?
