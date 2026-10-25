# Dispositivos de E/S, discos e RAID

## Cap. 36 · I/O Devices

**No livro.** Um dispositivo expõe uma **interface** (registradores de estado,
de comando e de dados) e esconde um **interior** (um chip, às vezes um
processador inteiro). O protocolo mais simples é: esperar o dispositivo ficar
livre, escrever os dados, escrever o comando e esperar terminar. Esperar
girando (*polling*) gasta CPU, então o dispositivo pode avisar com uma
**interrupção**. Copiar dados palavra por palavra pela CPU é a **E/S
programada** (PIO); para muitos dados, o **DMA** copia sozinho entre o
dispositivo e a memória. A CPU fala com os registradores por instruções
especiais de E/S (`in` e `out` no x86) ou por **E/S mapeada em memória**. O
código que conhece cada dispositivo é o **driver**.

**No Capivara.** Os drivers ficam em `kernel/dev/` e mostram todas essas
formas:

- **Instruções de E/S**: `inb`, `outb` e companhia em
  `kernel/arch/x86_64/cpu.h`. O controlador PS/2 (`ps2.c`) e a serial
  (`serial.c`) usam só isso.
- **Interrupções**: cada tecla gera a interrupção 1 (IRQ1). O tratador em
  `keyboard.c` lê o código da tecla, traduz e coloca o evento numa fila
  (`input_push`), sem fazer mais nada. Quem trata de verdade é o compositor,
  depois, fora da interrupção. É a regra "registre e saia".
- **Polling**: o mouse do VirtualBox (`vbox.c`) é lido 100 vezes por segundo
  pelo relógio, em vez de esperar a interrupção dele, porque o roteamento da
  interrupção PCI varia de máquina para máquina.
- **DMA**: a placa de som AC'97 (`ac97.c`). O driver escreve o som em pedaços
  da memória física e passa à placa uma lista com os endereços (a BDL). A placa
  lê a memória sozinha, enquanto a CPU faz outra coisa. O driver só confere
  até onde ela já tocou (o registrador CIV).
- **E/S mapeada em memória**: o framebuffer, a tela, é memória comum para a
  CPU. Escrever um pixel é escrever num endereço.
- **Descobrir dispositivos**: o barramento PCI (`pci.c`) é varrido no boot; cada
  dispositivo diz a classe dele (o som é a classe 04h, subclasse 01h) e os
  endereços dos seus registradores (os BARs).

**Experimente.** A placa de som funcionando por DMA:

```sh
som
```

```sh
som teste
```

O log mostra o que foi achado no boot:

```sh
dmesg
```

**Para pensar.**

- Por que o tratador do teclado não pode desenhar a tecla na tela direto?
  (Pense em quanto tempo uma interrupção pode demorar e no que ela interrompeu.)
- O driver do AC'97 não usa interrupção: quem escreve som consulta
  `audio_queued()`. Quais as vantagens e desvantagens disso, nos termos do
  livro?
- O AC'97 só enxerga endereços de 32 bits. Leia `audio_init`: o que o driver
  faz se a memória vier acima de 4 GiB?

## Cap. 37 · Hard Disk Drives

**No livro.** O disco gira, e um braço move a cabeça entre as trilhas. Ler um
setor custa o tempo de **busca** (mover o braço), a **rotação** (esperar o setor
passar) e a **transferência**. Para acesso aleatório, a busca e a rotação
dominam, por isso o acesso sequencial é muito mais rápido. Escalonar os
pedidos ajuda: **SSTF** atende o mais próximo (pode deixar pedidos distantes
esperando muito), **SCAN** (o elevador) varre de um lado ao outro, **C-SCAN**
varre só num sentido e volta ao começo, o que é mais justo com as pontas.

**No Capivara.** Ainda não há driver de disco (ATA ou AHCI): é o item 2 do
roteiro. O simulador `disco` move uma cabeça entre as trilhas 0 e 199 e soma o
deslocamento de cada política. O SCAN do simulador é a variante que vira no
último pedido (às vezes chamada de LOOK), e o C-SCAN conta a volta ao começo.

**Experimente.**

```sh
disco fifo 50
```

```sh
disco sstf 50
```

```sh
disco scan 50
```

```sh
disco cscan 50
```

Com pedidos seus: muitos perto do começo e um no fim do disco.

```sh
disco sstf 100 90 95 85 98 199 80 92
```

**Para pensar.**

- Qual política teve o menor deslocamento? E qual deixou algum pedido esperando
  mais?
- No último exemplo, quando o pedido da trilha 199 é atendido no SSTF? Isso é a
  "fome" que o livro descreve.
- O simulador só conta a busca. Como a rotação mudaria a escolha? (O livro chama
  isso de SPTF.)

## Cap. 38 · Redundant Arrays of Inexpensive Disks (RAIDs)

**No livro.** Vários discos juntos podem parecer um só, maior, mais rápido ou
mais confiável. **RAID 0** espalha os blocos em faixas (rápido, sem proteção);
**RAID 1** espelha (cada bloco em dois discos); **RAID 4** guarda a
**paridade** num disco extra, e **RAID 5** espalha a paridade por todos. O
capítulo compara capacidade, confiabilidade e desempenho de cada um.

**No Capivara.** Não há discos. Fica como exercício de papel.

**Para pensar.**

- A paridade do RAID 4 é um XOR. Com 4 discos de dados guardando os bytes 7, 2,
  9 e 4, qual é o byte de paridade? Se o disco do 9 morrer, como recuperá-lo?
  (Dá para conferir com `calc`, mas o `calc` não tem XOR: faça bit a bit.)
