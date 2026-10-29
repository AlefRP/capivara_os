# 🧭 Guia de uso

Como usar o Capivara OS: a tela, as janelas, os aplicativos e o Terminal.

> 💡 Tudo roda na memória RAM. Arquivos criados ou alterados **somem ao
> desligar**: o sistema ainda não tem driver de disco (está no
> [roteiro](../README.md#roteiro)).

## 🔒 Tela de bloqueio

O sistema começa na tela de bloqueio. Pressione **Enter** ou clique em
**Entrar**. No começo não há senha. Para criar uma, vá em Configurações ›
Contas. A senha é guardada como *hash* com sal, nunca em texto puro.

Para bloquear de novo: **Super+L**, o comando `bloquear` no Terminal ou o menu
da capivara › Bloquear.

<img src="img/telas/bloqueio.png" alt="Tela de bloqueio" width="640">

## 🖥️ A tela

<img src="img/telas/area-de-trabalho.png" alt="Área de trabalho" width="640">

**Barra de menus (topo).** Fica sempre visível, como no macOS.

- **Capivara (canto esquerdo):** o menu do sistema, com Sobre o Capivara OS,
  Configurações, Gerenciador de Tarefas, Bloquear, Reiniciar e Desligar.
- **Nome do app em foco e os menus dele** (Arquivo, Editar, Exibir...). Cada
  aplicativo coloca os próprios menus ali.
- **Direita:** os ícones de status, a Pesquisa, a **Central de Controle** e o
  relógio. Clicar no relógio abre a **Central de Notificações** com um
  calendário.

**Dock (embaixo).** Ao passar o mouse, os ícones crescem. Um ponto embaixo do
ícone indica que o app está aberto. Um clique abre o app ou traz a janela
dele para a frente. A Lixeira fica no fim.

**Área de trabalho.** O disco "Capivara HD" e os arquivos da pasta
`~/Área de Trabalho` aparecem à direita. Clique duas vezes para abrir. Um
arquivo `.app` é um **atalho**: guarda o id de um aplicativo (o mesmo do
comando `abrir`), aparece com o ícone do app e o abre no clique duplo. Por
exemplo, no Terminal, `cd "Área de Trabalho"` e depois
`echo calc > Calculadora.app` criam um atalho para a Calculadora.

## 🪟 Janelas

| Botão / gesto | O que faz |
|---|---|
| 🔴 vermelho | fecha |
| 🟡 amarelo | minimiza para o Dock |
| 🟢 verde | maximiza. **Parado em cima dele**, mostra os layouts de encaixe |
| arrastar o título | move a janela |
| arrastar a borda | redimensiona |
| clique duplo no título | maximiza / restaura |

**Encaixe pelo teclado:** Super+← / Super+→ encaixam a janela na metade da
tela. Depois de encaixar, Super+↑ / Super+↓ levam a janela para um quarto da
tela. Fora do encaixe, Super+↑ maximiza e Super+↓ restaura ou minimiza.
Super+Z abre os layouts.

**Alt+Tab** alterna entre as janelas, e **Alt+F4** fecha a janela em foco.

<img src="img/telas/aplicativos.png" alt="Explorador e Terminal encaixados lado a lado" width="640">

## 🚀 Launchpad e Pesquisa

- **Super** (a tecla Windows, sozinha) abre o **Launchpad**, com todos os
  aplicativos.
- **Super+Espaço** (ou Super+S) abre a **Pesquisa**. Digite parte do nome do
  app e tecle Enter.

## 🗂️ Mission Control e áreas de trabalho

**Super+Tab** mostra todas as janelas e, no topo, as áreas de trabalho.

| Atalho | O que faz |
|---|---|
| Super+Ctrl+D | cria uma área de trabalho |
| Super+Ctrl+← / → | troca de área de trabalho |
| Super+Ctrl+F4 | fecha a área de trabalho atual |
| Super+D | mostra a área de trabalho (minimiza tudo) |

<img src="img/telas/mission-control.png" alt="Mission Control" width="640">

## 🎛️ Central de Controle e notificações

A **Central de Controle** fica no ícone da barra de menus e tem estes atalhos:

- tema **Escuro**;
- modo **Silêncio**, que segura as notificações;
- **Captura** de tela;
- **Bloquear**;
- a cor de destaque e o papel de parede.

As notificações aparecem no canto superior direito. **Super+N** abre a
Central de Notificações, onde ficam as notificações antigas e o calendário.

## 📷 Captura de tela e área de transferência

| Atalho | O que faz |
|---|---|
| Print Screen | captura a tela inteira |
| Super+Shift+S | recorte: arraste para escolher a área |
| Super+V | histórico da área de transferência (os 10 últimos textos copiados) |

As capturas vão para `~/Imagens/Capturas de tela/`, em BMP, e abrem no app
Fotos.

## 🧰 Aplicativos

| | App | Dicas |
|:-:|---|---|
| <img src="img/icones/explorador.png" width="32" alt=""> | Explorador | Delete manda para a Lixeira; Shift+Delete apaga de vez. O menu Exibir mostra os arquivos ocultos |
| <img src="img/icones/terminal.png" width="32" alt=""> | Terminal | veja a lista de comandos abaixo. Setas ↑/↓ percorrem o histórico |
| <img src="img/icones/toca.png" width="32" alt=""> | Toca · Tatu DB | cliente SQL com editor, navegador de tabelas e grade; Ctrl+Enter executa. [Guia do banco](tatu.md) |
| <img src="img/icones/bloco-de-notas.png" width="32" alt=""> | Bloco de Notas | Ctrl+S salva, Ctrl+O abre, Ctrl+N cria um arquivo novo |
| <img src="img/icones/calculadora.png" width="32" alt=""> | Calculadora | aceita o teclado numérico e as teclas + − * / Enter |
| <img src="img/icones/paint.png" width="32" alt=""> | Paint | salva e abre imagens BMP |
| <img src="img/icones/fotos.png" width="32" alt=""> | Fotos | abre as capturas de tela e os desenhos do Paint |
| <img src="img/icones/gerenciador.png" width="32" alt=""> | Gerenciador de Tarefas | Ctrl+Shift+Esc. Mostra CPU e memória por tarefa e permite finalizar uma tarefa |
| <img src="img/icones/configuracoes.png" width="32" alt=""> | Configurações | Personalização, Contas, Sistema, Data e hora (teclado ABNT2/EUA) e Sobre |
| <img src="img/icones/lixeira.png" width="32" alt=""> | Lixeira | restaura ou esvazia. Também abre pelo Explorador |
| <img src="img/icones/estudos.png" width="32" alt=""> | Estudos | guia do livro OSTEP ([Estudando o OSTEP](ostep.md)). Clique num capítulo à esquerda; ← e → trocam de capítulo; clique duplo numa linha de comando abre o Terminal rodando ela |

Programas que ficam fora do repositório, como um jogo seu, podem entrar no
sistema que você compila em casa, com o ícone de controle de videogame. Veja
[Programas extras](extras.md).

## ⌨️ Terminal

Digite `ajuda` para ver a lista. Também existe o redirecionamento `>`, como em
`echo oi > nota.txt`.

| Comando | O que faz |
|---|---|
| `ls [-l] [-a] [caminho]` | lista arquivos (`-a` mostra os ocultos) |
| `cd [caminho]`, `pwd` | muda de pasta (`~` = pasta pessoal) / mostra a pasta atual |
| `cat <arquivo>` | mostra o conteúdo |
| `echo <texto> [> arq]` | escreve texto, ou grava num arquivo |
| `mkdir`, `touch` | cria pasta / arquivo |
| `rm`, `cp`, `mv` | apaga, copia, move |
| `tree [caminho]` | árvore de pastas |
| `abrir <app\|caminho>`, `apps` | abre um aplicativo ou arquivo / lista os aplicativos |
| `ps`, `kill <id>` | lista as tarefas / finaliza uma tarefa |
| `mem` | uso de memória |
| `calc <expressão>` | calcula, por exemplo `calc (2+3)*4` |
| `date`, `uptime` | data e tempo ligado |
| `sysinfo` | informações do sistema, com uma capivara em ASCII |
| `dmesg` | log do kernel |
| `history`, `clear` | comandos anteriores / limpa a tela |
| `teclado [abnt2\|us]` | mostra ou troca o layout do teclado |
| `tema [claro\|escuro]` | mostra ou troca o tema |
| `tatu [arquivo\|teste]` | abre o shell SQL do Tatu DB ou roda seus testes; `.quit` volta ao Terminal ([guia](tatu.md)) |
| `som [teste\|volume N]` | mostra a placa de som, toca dó-mi-sol ou muda o volume (0 a 100) |
| `notificar <texto>` | mostra uma notificação |
| `captura` | salva a tela em Imagens |
| `lixeira [esvaziar]` | lista ou esvazia a Lixeira |
| `bloquear` | tela de bloqueio |
| `reiniciar`, `desligar` | energia |
| `exit` | fecha o Terminal |

Os **laboratórios do OSTEP** também são comandos do Terminal. `ostep` mostra a
lista; o que cada um ensina está em [Estudando o OSTEP](ostep.md).

| Comando | O que faz |
|---|---|
| `ostep` | lista os laboratórios e os capítulos de cada um |
| `trocas` | trocas de contexto do escalonador, no total e por tarefa |
| `escalonar <política> [q=N] [s=N] [tarefas]` | simula FIFO, SJF, STCF, RR, MLFQ ou loteria |
| `heap [blocos [N] \| teste]` | o heap do kernel: blocos, fragmentação, divisão e junção |
| `traduzir [endereço]` | o caminho de um endereço virtual pelas tabelas de página |
| `paginas <política> [quadros] [refs]`, `paginas belady` | simula FIFO, LRU, OPT ou relógio |
| `corrida [trava] [N]` | duas tarefas somam num contador, sem ou com mutex |
| `prodcons <polling\|semaforo> [p=N] [c=N] [b=N] [n=N] [atraso=N]` | produtores e consumidores reais; compara espera por consultas com bloqueio, verifica os itens e mostra métricas |
| `prodcons teste` | dez regressões das primitivas reais de semáforo e join, com verificações de créditos, cancelamento e coleta de tarefas |
| `disco <política> [cabeça] [trilhas]` | simula FIFO, SSTF, SCAN ou C-SCAN |
| `stat <caminho>` | os metadados de um arquivo ou pasta |

No `prodcons`, `p` e `c` são produtores e consumidores (padrão 2, de 1 a 4),
`b` é a capacidade da fila (padrão 4, de 1 a 64), `n` é a quantidade de itens
**por produtor** (padrão 40, de 1 a 10 000) e `atraso` é a pausa antes de
publicar cada item em ms (padrão 1, de 0 a 20). Compare `prodcons polling` e
`prodcons semaforo`
com as mesmas opções; o total esperado é `p × n`. O guia do OSTEP explica
as verificações de integridade e as métricas de CPU e espera.
Durante a execução, finalizar o Terminal fica adiado até o laboratório
concluir; os trabalhadores ficam protegidos contra finalização parcial.

`prodcons teste` verifica correção das primitivas; os cenários completos estão
em [tests/prodcons.txt](../tests/prodcons.txt) e rodam no CI. Os testes exigem
integridade e totais corretos; tempo, CPU e trocas podem variar entre execuções.

## ⌨️ Teclado

O layout padrão é o **ABNT2**, com acentos: ´ ` ~ ^ ¨ seguidos da letra. Se o
seu teclado for americano, use `teclado us` no Terminal ou Configurações ›
Data e hora.

## 🔊 Som

O sistema tem um driver para a placa de som **AC'97**, a que o VirtualBox e o
QEMU emulam. O som sai em 48 kHz, estéreo.

- **VirtualBox:** Configurações da VM › Áudio › Controlador de áudio
  **ICH AC97**. O `.\build.ps1 vbox` já deixa a VM assim.
- **QEMU:** `-device AC97`. O `make run` já passa essa opção. Para escolher a
  saída, use `QEMU_AUDIO`, por exemplo `make run QEMU_AUDIO=alsa`.
- No Terminal, `som` mostra a placa, `som teste` toca três notas e
  `som volume 50` muda o volume.

PCs de hoje usam outra placa, a HD Audio (Intel HDA), que o sistema ainda não
conhece. Em hardware real, o sistema funciona sem som.

## 🖱️ Mouse em máquina virtual

No QEMU e no VirtualBox, o mouse funciona em modo absoluto: entra e sai da
janela da VM sem precisar "capturar" o cursor. Se o mouse se comportar de um
jeito estranho, escolha **"modo seguro: sem mouse absoluto"** no menu do GRUB.
