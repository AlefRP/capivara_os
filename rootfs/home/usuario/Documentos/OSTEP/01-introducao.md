# Introdução: o que um sistema operacional faz

## Cap. 2 · Introduction to Operating Systems

**No livro.** O capítulo apresenta o sistema operacional como a camada que
torna o hardware fácil e seguro de usar. Ele faz isso de três jeitos, que dão
nome às três partes do livro. **Virtualiza** recursos: cada programa acha que
tem a CPU e a memória só para ele. Lida com **concorrência**: várias coisas
acontecem ao mesmo tempo, e o sistema precisa evitar que uma atrapalhe a
outra. Garante **persistência**: os dados sobrevivem quando a máquina desliga.
O capítulo também conta, rapidamente, como os sistemas evoluíram de
bibliotecas simples até os sistemas com proteção entre programas.

**No Capivara.** Dá para ver as três peças, em tamanhos diferentes:

- **Virtualização da CPU**: o escalonador em `kernel/sched/task.c` divide a CPU
  entre as tarefas, 10 ms por vez. Cada janela aberta tem uma tarefa.
- **Virtualização da memória**: ainda não existe de verdade. Há paginação
  (`kernel/mm/vmm.c`), mas um espaço de endereçamento só, compartilhado por
  todas as tarefas. É o primeiro item do roteiro.
- **Concorrência**: as tarefas são interrompidas a qualquer momento pelo
  relógio, então dados compartilhados precisam de mutex (`mutex_lock` em
  `kernel/sched/task.c`).
- **Persistência**: ainda não existe. Os arquivos ficam num *ramfs*
  (`kernel/fs/vfs.c`) e somem ao desligar. O conteúdo inicial vem de um
  arquivo tar que o GRUB carrega junto com o kernel.

O boot mostra a ordem em que essas peças são montadas. Leia `kmain` em
`kernel/main.c`: memória física, paginação, heap, interrupções, relógio,
escalonador, arquivos, drivers e, por último, a interface gráfica.

**Experimente.** O log do boot conta essa história em ordem:

```sh
dmesg
```

```sh
sysinfo
```

**Para pensar.**

- Quais mensagens do `dmesg` correspondem a cada uma das três peças?
- O Capivara não protege um aplicativo do outro. Um bug no Paint pode derrubar
  o sistema inteiro? Por quê? O que o livro diz que é preciso para evitar isso?
- Por que a memória física e a paginação precisam vir antes do heap no
  `kmain`?
