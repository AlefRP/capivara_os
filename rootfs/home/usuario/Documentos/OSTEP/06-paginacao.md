# Paginação, TLB e tabelas de vários níveis

## Cap. 18 · Paging: Introduction

**No livro.** A paginação divide o espaço virtual em **páginas** de tamanho fixo
e a memória física em **quadros** do mesmo tamanho. O endereço virtual vira
dois pedaços: o **número da página virtual** (VPN) e o **deslocamento**. Uma
**tabela de páginas** diz, para cada VPN, em que quadro físico ela está, com
bits extras: presente, permissão de escrita, usuário ou kernel, acessada,
suja. Sem fragmentação externa, mas com um custo: a tabela ocupa memória, e
cada acesso à memória precisa de outro acesso só para ler a tabela.

**No Capivara.** As páginas são de 4 KiB, o padrão do x86. Cada entrada da
tabela tem 8 bytes e os bits estão em `kernel/mm/vmm.h`: `PTE_PRESENT`,
`PTE_WRITE`, `PTE_USER`, `PTE_HUGE`, `PTE_GLOBAL`, e `PTE_ADDR` é a máscara do
endereço físico. `vmm_map` liga um endereço virtual a um físico, e
`vmm_unmap` desliga.

Os bits "acessada" (A, bit 5) e "suja" (D, bit 6) são ligados **pelo
hardware** quando a página é lida ou escrita. O Capivara não usa esses bits
ainda, mas eles aparecem no `traduzir`.

**Experimente.**

```sh
traduzir
```

Escolha um endereço do heap que apareceu acima e traduza só ele, por
exemplo (o seu número vai ser outro):

```sh
traduzir 0xffffa00000001000
```

**Para pensar.**

- No `traduzir`, ache uma entrada com o bit D. O que alguém escreveu naquela
  página?
- Por que o deslocamento tem 12 bits?

## Cap. 19 · Paging: Faster Translations (TLBs)

**No livro.** Para não ler a tabela a cada acesso, o processador guarda as
traduções recentes num cache, a **TLB**. Quase todo acesso acerta na TLB,
graças à localidade espacial e temporal. Mas, quando o sistema muda uma
tabela, a TLB pode guardar uma tradução velha: é preciso **invalidar** a
entrada. Na troca de processo, a TLB inteira ficaria errada; os processadores
resolvem isso apagando a TLB ou marcando cada entrada com o dono (ASID).

**No Capivara.** Três lugares mostram a TLB:

- `vmm_map` e `vmm_unmap` executam `invlpg(virt)` depois de mudar uma entrada:
  apagam da TLB só a tradução daquele endereço.
- `vmm_init`, ao apagar o mapeamento identidade do boot, faz
  `write_cr3(read_cr3())`. Escrever no CR3 esvazia a TLB inteira, menos as
  entradas **globais**.
- O heap e o physmap são mapeados com `PTE_GLOBAL`: no x86, o bit G diz "esta
  tradução vale para todos os espaços de endereçamento". Quando houver um CR3
  por processo, trocar de processo não vai apagar as traduções do kernel.

**Para pensar.**

- Se `vmm_unmap` esquecesse o `invlpg`, o que poderia acontecer no próximo
  acesso àquele endereço?
- O livro fala do ASID. O x86 tem algo parecido, o PCID. Por que o Capivara
  ainda não precisa dele?

## Cap. 20 · Paging: Smaller Tables

**No livro.** Uma tabela linear para 64 bits seria gigante. A solução mais
comum é a **tabela de vários níveis**: um diretório aponta para pedaços da
tabela, e pedaços vazios simplesmente não existem. O preço é um acesso extra à
memória por nível quando a TLB erra. Outra saída são **páginas maiores**, que
diminuem a tabela, mas podem desperdiçar memória dentro da página.

**No Capivara.** O x86 de 64 bits usa **4 níveis**: PML4, PDPT, PD e PT. Cada
tabela tem 512 entradas de 8 bytes, ou seja, exatamente uma página de 4 KiB, e
cada nível usa 9 bits do endereço virtual:

```sh
47      39 38      30 29      21 20      12 11         0
[ PML4   ][  PDPT   ][   PD    ][   PT    ][ deslocamento]
```

`vmm_map` desce os níveis com `next_level` e **cria a tabela que faltar**
(`pmm_alloc_zero`): tabela que nunca foi usada não ocupa memória. O
`vmm_walk` (usado pelo `traduzir`) faz a mesma descida que a MMU faz.

As páginas grandes também aparecem:

- O `boot.S` mapeia o primeiro 1 GiB com 512 páginas de **2 MiB** (o bit PS,
  `PTE_HUGE`, na entrada do PD). O código do kernel roda nelas.
- O physmap (`build_physmap` em `vmm.c`) mapeia toda a RAM com páginas de 2 MiB:
  para 4 GiB, bastam 2048 entradas de PD, em vez de um milhão de entradas de
  PT.

**Experimente.** Compare uma tradução de 4 níveis (heap) com uma de 3 níveis
(physmap):

```sh
traduzir
```

Um endereço não canônico (os bits de cima não repetem o bit 47):

```sh
traduzir 0x0000800000000000
```

**Para pensar.**

- Faça a conta do livro para o x86: quanta memória ocuparia uma tabela linear
  para 48 bits de endereço com páginas de 4 KiB e entradas de 8 bytes?
- Quantas páginas de tabela o physmap usa para 4 GiB de RAM? E se usasse
  páginas de 4 KiB?
- Pegue um endereço que o `traduzir` mostrou e calcule os índices à mão (divida
  os bits de 9 em 9). Bate?
