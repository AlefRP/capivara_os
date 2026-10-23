# Especificação 03 — Interface gráfica

Visual no estilo das áreas de trabalho do macOS, com os recursos de
produtividade conhecidos do Windows.

## Arquitetura

```
 teclado/mouse (IRQ) ──► fila de entrada ──► COMPOSITOR (tarefa "gui")
                                                │  decide quem recebe cada evento
                                                ▼
   app (tarefa) ◄── fila de eventos ◄───────────┘
      │ desenha no próprio buffer (canvas)
      └── win_invalidate() ──► compositor redesenha só a área suja
```

- **Janela** = buffer de pixels próprio, posição, estado (normal,
  minimizada, maximizada, encaixada), camada (fundo, normal, sempre no topo)
  e área de trabalho virtual.
- **Compositor**: ordem de empilhamento, decorações, sombras, cantos
  arredondados, cursor; redesenha só os retângulos alterados e pula o que está
  escondido atrás de janelas opacas (oclusão).
- **Shell** (tarefa comum) cria as janelas especiais: área de trabalho, barra
  de menus, Dock, Launchpad, Pesquisa, Central de Controle, Central de
  Notificações, banners e tela de bloqueio.
- **Menus globais**: o app publica os títulos (`win_set_menus("Arquivo|Editar")`);
  a barra de menus mostra os do app com foco e, ao clicar, envia `EV_MENU` ao
  app, que abre o menu na posição indicada.
- **Clique-através** (`WF_CLICKTHRU`): pixels totalmente transparentes de uma
  janela não recebem cliques. O Dock é uma janela larga e transparente; só a
  placa e os ícones são "sólidos".
- **Tela de bloqueio**: enquanto ativa, o compositor desenha e entrega
  entrada apenas a ela (nenhum atalho funciona).

## Comportamento esperado

| Ação | Resultado |
|---|---|
| Botões à esquerda da barra de título | vermelho fecha, amarelo minimiza, verde amplia; ao passar o mouse mostram ×, −, ↗ |
| Parar o mouse no botão verde | painel com 6 layouts de encaixe (metades, terços, quartos) |
| Arrastar a janela até a borda | metade da tela; no topo, maximiza |
| Dock | ampliação dos ícones perto do mouse, rótulo com o nome, ponto = app aberto |
| Super | Launchpad (todos os apps) |
| Super+Espaço / Super+S | Pesquisa de apps e arquivos |
| Alt+Tab · Alt+F4 | alternador de apps · fechar |
| Super+Tab | Mission Control: miniaturas e áreas de trabalho (criar, trocar, fechar, arrastar janela para outra) |
| Super+Ctrl+←/→, Super+Ctrl+D | trocar / criar área de trabalho |
| Super+setas | encaixar metades e quartos; maximizar; restaurar |
| Super+L | bloquear a tela |
| Super+V | histórico da área de transferência |
| Print Screen · Super+Shift+S | captura da tela inteira · recorte de uma área |
| Ctrl+Shift+Esc | Gerenciador de Tarefas |

## Visual

- Temas claro e escuro, seis cores de destaque, seis papéis de parede
  gerados por código. O padrão, "Capivara" (beira de rio com capivaras no fim de tarde), é pixel
  art: desenhado numa grade
  de 256 pixels de largura com paleta limitada e pontilhado ordenado (Bayer
  4x4) e ampliado 4x sem suavização.
- Ícones de app em "quadrado arredondado" com degradê, brilho e sombra;
  pastas azuis com símbolos "gravados".
- Fonte: GNU Unifont (bitmap 8×16), desenhada de forma proporcional; ampliada
  2× e 4× com o algoritmo EPX (suaviza diagonais).

## Sistema de arquivos (fase 1)

- **ramfs**: árvore de diretórios e arquivos na memória.
- O GRUB carrega um arquivo `tar` (USTAR) como módulo; o kernel o descompacta.
- **Lixeira**: excluir move o item para `/home/usuario/.lixeira` e guarda o
  caminho original em `/home/usuario/.lixeira-info` (para restaurar).
