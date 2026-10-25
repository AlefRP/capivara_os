# Arquivos, diretórios e sistemas de arquivos

## Cap. 39 · Interlude: Files and Directories

**No livro.** Um **arquivo** é uma sequência de bytes com um nome de baixo nível,
o número do **inode**. Um **diretório** é um arquivo especial que guarda pares
(nome, número do inode). A API do Unix: `open` devolve um **descritor**, `read`
e `write` andam a partir de uma posição atual, `lseek` muda a posição, `fsync`
força a gravação no disco. `stat` mostra os metadados (tamanho, dono, datas,
quantos nomes apontam para o inode). `link` cria outro nome para o mesmo
inode (*hard link*), e `unlink` apaga um nome: o arquivo só some quando não
sobra nenhum.

**No Capivara.** A API está em `kernel/fs/vfs.h`, mais simples que a do Unix:

- não há descritores nem posição atual: `vfs_read(arquivo, deslocamento, buf,
  n)` recebe o deslocamento toda vez, como o `pread` do Unix;
- `vfs_lookup` transforma um caminho no nó (`vnode_t`), andando pela árvore
  nome por nome, como o Unix faz com os diretórios;
- não há *hard links*: cada nó tem um pai só (`parent`). Os diretórios guardam
  os filhos numa lista (`children`, `next`), e não pares nome e inode;
- não há `fsync`, porque não há disco.

**Experimente.**

```sh
stat Documentos/Bem-vindo.txt
```

```sh
stat Documentos
```

```sh
tree
```

Crie um arquivo, veja os metadados, aumente o arquivo e veja de novo:

```sh
echo uma linha > teste.txt
```

```sh
stat teste.txt
```

**Para pensar.**

- O `stat` mostra "reservado" maior que o "tamanho". Por quê? Leia `reserve`
  em `vfs.c`: de quanto em quanto o arquivo cresce, e por que dobrar é melhor
  do que crescer só o necessário?
- Como você implementaria *hard links* no ramfs? O que teria que sair do
  `vnode_t` e virar um "inode" separado?

## Cap. 40 · File System Implementation

**No livro.** Um sistema de arquivos simples (o *vsfs* do livro) divide o disco
em blocos: um **superbloco** (os parâmetros do sistema), **bitmaps** de inodes
e de blocos livres, a **tabela de inodes** e os **blocos de dados**. Cada inode
aponta para os blocos do arquivo com ponteiros diretos, indiretos e duplamente
indiretos (ou com *extents*). O capítulo segue o caminho de um `open` e de um
`read` e mostra quantas leituras de disco cada um custa, e por que os caches
de páginas e de diretórios são tão importantes.

**No Capivara.** O *ramfs* troca "blocos no disco" por memória:

- o `vnode_t` faz o papel do inode: tipo, tamanho, data de modificação e um
  ponteiro para os dados;
- os dados de um arquivo ficam num **bloco só** do heap (`data`, com
  `capacity` bytes): é como um único *extent*. Para crescer, `reserve` dobra a
  capacidade com `krealloc`, que pede um bloco maior e copia;
- o "bitmap de livres" é o próprio heap (`kmalloc`);
- não há superbloco: a raiz é uma variável (`vfs_root`).

O conteúdo inicial vem de um **tar** (`initrd.tar`), que o GRUB carrega junto
com o kernel. O tar é um formato simples de verdade: cada arquivo é um
cabeçalho de 512 bytes (nome, tamanho em octal, tipo) seguido dos dados,
arredondados para múltiplos de 512. Leia `vfs_load_initrd` em `vfs.c`; o tar é
gerado por `tools/mkinitrd.py` a partir da pasta `rootfs/` do projeto.

**Para pensar.**

- Desenhe o leiaute de disco do *vsfs* e, ao lado, onde cada parte "mora" no
  ramfs do Capivara.
- Se o Capivara tivesse disco, quantas leituras custaria abrir
  `/home/usuario/Documentos/Bem-vindo.txt` sem cache nenhum?

## Cap. 41 · Locality and The Fast File System

**No livro.** O primeiro sistema de arquivos do Unix espalhava os dados pelo
disco e ficava lento. O **FFS** dividiu o disco em **grupos de cilindros** e
passou a guardar perto os inodes e os dados de um mesmo diretório,
aproveitando a localidade. Também tratou o caso dos arquivos grandes, que não
podem encher um grupo sozinhos.

**No Capivara.** Sem disco, não há "perto" ou "longe". Mas a ideia volta no
heap: dados usados juntos, alocados juntos, ficam juntos na memória e no cache
do processador.

## Cap. 42 · Crash Consistency: FSCK and Journaling

**No livro.** Gravar um arquivo mexe em vários lugares (bitmap, inode, dados).
Se a energia cai no meio, o disco fica inconsistente. O **fsck** confere e
conserta tudo depois do boot, mas é lento. O **journaling** escreve antes, num
**diário**, o que vai fazer; depois de uma queda, basta refazer as transações
completas do diário. O journaling de metadados (o modo *ordered* do ext3 e do
ext4) grava os dados primeiro e só registra os metadados.

**No Capivara.** Nada a perder numa queda: está tudo na memória. É o problema
central do item 2 do roteiro.

**Para pensar.**

- Liste, em ordem, as escritas de disco para acrescentar um bloco a um arquivo
  no *vsfs*. Para cada ponto em que a energia pode cair, o que fica errado?

## Caps. 43 a 45 · LFS, SSDs e integridade de dados

**No livro.** O **LFS** escreve tudo em sequência, como um diário só, porque
escrita sequencial é rápida; o preço é limpar o lixo depois. Os **SSDs** não
têm braço nem rotação, mas só apagam em blocos grandes, o que leva à camada de
tradução (FTL), ao *wear leveling* e ao *garbage collection*. Para a
**integridade**, discos de verdade corrompem dados em silêncio, e os sistemas
usam *checksums* para detectar isso.

**No Capivara.** Um pedaço de integridade já existe, no heap: o número mágico
de cada bloco é uma checagem barata, que detecta parte das corrupções (um
`kfree` com ponteiro errado, por exemplo) antes que elas se espalhem.

**Para pensar.**

- Por que o número mágico do heap não é um *checksum*? O que ele detecta e o
  que deixa passar?
