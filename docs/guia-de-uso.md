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
`~/Área de Trabalho` aparecem à direita. Clique duas vezes para abrir.

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
| <img src="img/icones/bloco-de-notas.png" width="32" alt=""> | Bloco de Notas | Ctrl+S salva, Ctrl+O abre, Ctrl+N cria um arquivo novo |
| <img src="img/icones/calculadora.png" width="32" alt=""> | Calculadora | aceita o teclado numérico e as teclas + − * / Enter |
| <img src="img/icones/paint.png" width="32" alt=""> | Paint | salva e abre imagens BMP |
| <img src="img/icones/fotos.png" width="32" alt=""> | Fotos | abre as capturas de tela e os desenhos do Paint |
| <img src="img/icones/gerenciador.png" width="32" alt=""> | Gerenciador de Tarefas | Ctrl+Shift+Esc. Mostra CPU e memória por tarefa e permite finalizar uma tarefa |
| <img src="img/icones/configuracoes.png" width="32" alt=""> | Configurações | Personalização, Contas, Sistema, Data e hora (teclado ABNT2/EUA) e Sobre |
| <img src="img/icones/lixeira.png" width="32" alt=""> | Lixeira | restaura ou esvazia. Também abre pelo Explorador |

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
| `notificar <texto>` | mostra uma notificação |
| `captura` | salva a tela em Imagens |
| `lixeira [esvaziar]` | lista ou esvazia a Lixeira |
| `bloquear` | tela de bloqueio |
| `reiniciar`, `desligar` | energia |
| `exit` | fecha o Terminal |

## ⌨️ Teclado

O layout padrão é o **ABNT2**, com acentos: ´ ` ~ ^ ¨ seguidos da letra. Se o
seu teclado for americano, use `teclado us` no Terminal ou Configurações ›
Data e hora.

## 🖱️ Mouse em máquina virtual

No QEMU e no VirtualBox, o mouse funciona em modo absoluto: entra e sai da
janela da VM sem precisar "capturar" o cursor. Se o mouse se comportar de um
jeito estranho, escolha **"modo seguro: sem mouse absoluto"** no menu do GRUB.
