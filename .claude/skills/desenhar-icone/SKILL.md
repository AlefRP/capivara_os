---
name: desenhar-icone
description: Como desenhar ou redesenhar um ícone do Capivara OS em código (kernel/gui/icons.c), com o estilo do sistema, e conferir o resultado em tamanho grande nos fundos claro e escuro. Use quando pedirem um ícone novo, um ícone "mais bonito", ou mandarem uma imagem de referência para inspirar um ícone.
---

# Desenhar um ícone

Os ícones são **geometria desenhada em código**, sem nenhuma imagem externa.
Cada um é feito numa grade de **32×32 unidades** e escalado para o tamanho
pedido (32 px na lista, 48 a 78 px no Dock, 128 px no README).

## Clean room

Imagem de referência do usuário (por exemplo, um ícone do macOS) é
**inspiração**: aproveite a ideia geral, mas mude proporções, cores e
detalhes, e dê um toque próprio. O cursor laranja do Terminal e a capivara no
lugar da maçã são exemplos. Nunca copie traço por traço nem use arquivos de
terceiros.

## Onde mexer

- `kernel/gui/icons.h`: um `ICON_*` novo no enum, sempre **antes** de `ICON_COUNT`.
- `kernel/gui/icons.c`: um `case` em `icon_draw()`.

## Ferramentas disponíveis em icons.c

Todas usam unidades da grade (0–32), com antisserrilhado.

| Função | Para quê |
|---|---|
| `squircle(topo, base)` | fundo padrão de app: quadrado arredondado 3..29, com degradê, sombra e fio de luz |
| `rrect(x, y, w, h, raio, cor)` | retângulo arredondado chapado (também serve para sombras com `ARGB(0x20, 0)`) |
| `grad(x, y, w, h, raio, topo, base)` | retângulo arredondado com degradê vertical |
| `outline(...)` / `outline_top(..., ate, cor)` | contorno / fio de luz que some até a altura `ate` |
| `circle`, `ring`, `line`, `tri` | círculo, anel, linha (pontas redondas), triângulo |
| `poly(pontos, n, cor)` | polígono qualquer, até côncavo. **Use no lugar de vários `tri()` translúcidos**: triângulos emendados deixam uma "costura" visível |

Cores: `HEX(0xRRGGBB)` é opaca e `ARGB(alfa, 0xRRGGBB)` é translúcida.

## Estilo do sistema

- Apps: `squircle` ou um objeto inteiro com sombra embaixo (como a Calculadora
  "de pé"). Volume com degradê de cima para baixo, uma borda inferior mais
  escura (a "espessura" do objeto) e um fio de luz no topo.
- Sombra suave: dois `rrect` pretos translúcidos, um pouco maiores e mais
  baixos que a forma (`ARGB(0x14, 0)` e `ARGB(0x28, 0)`).
- Paleta da capivara: pelo `0xA97A50`, pelo escuro `0x86603F`, laranja
  `0xE98A2E`/`0xFF9F0A`. Azul do sistema: `0x0A84FF`.
- Pense no ícone em 32 px: detalhe que some nesse tamanho só atrapalha.

## Conferir em tamanho grande

O `make arte` compila o código gráfico do kernel como programa comum e
salva os ícones em PNG transparente, em `docs/img/icones/`. Para ver um ícone
novo, inclua-o na lista de `main()` em `tools/arte/arte.c`. Rode no container:

```sh
MSYS_NO_PATHCONV=1 docker run --rm --platform linux/amd64 -v "D:/Projetos/so:/src" -w /src \
  capivara-toolchain sh -c 'make arte && convert \
    \( -size 290x150 xc:"#d8dee6" \) \( -size 290x150 xc:"#0d1117" \) +append \
    docs/img/icones/terminal.png -geometry +10+10 -composite \
    docs/img/icones/terminal.png -geometry +300+10 -composite build/previa.png'
```

Abra `build/previa.png` e olhe nos dois fundos: costuras, bordas escuras,
sombra cortada. Depois confira no sistema de verdade, com a skill
`testar-no-qemu`: o Dock ampliado (`move` sobre o ícone, y ≈ 735) e o Launchpad.
