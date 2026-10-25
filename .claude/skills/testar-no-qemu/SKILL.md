---
name: testar-no-qemu
description: Roda o Capivara OS no QEMU sem tela, dirigido por um roteiro (teclas, cliques, capturas), para conferir uma mudança de verdade. Use depois de mexer no kernel, na interface ou num aplicativo, para escrever um roteiro novo em tests/, ou para testar o boot como pendrive e por UEFI.
---

# Testar no QEMU

O `tools/qemu-test.py` liga o sistema no QEMU sem janela, executa um roteiro
linha a linha e salva capturas em PNG. Ele **falha** (código de saída 1) se o
kernel entrar em pânico ou se um `waitlog` não for atendido.

## Rodar

```sh
sh build.sh test tests/gui.txt --out build/test/gui          # compila antes, se precisar
sh build.sh test tests/boot.txt --usb --uefi --out build/test/pendrive
```

- `--usb`: a ISO vira um pendrive USB virtual, o mesmo caminho de um pendrive real.
- `--uefi`: firmware UEFI (OVMF) no lugar da BIOS.
- `--iso CAMINHO`: testa outra ISO (útil quando a do `build/` está travada pela VM).
- `--som`: grava a saída da placa de som (AC'97) em `som.wav`, na pasta de
  saída. Sem ele, a placa existe, mas o som vai para lugar nenhum. Para saber
  se tocou algo, leia o WAV com o módulo `wave` do Python, no container.

O QEMU do container não tem aceleração por hardware: o sistema roda várias
vezes mais devagar que no VirtualBox. Bom para ver se funciona, ruim para
medir velocidade (um jogo, por exemplo, cai para uns 30 quadros por segundo).

No Windows (Git Bash), o `build.sh` usa o container sozinho. Para chamar direto:

```sh
MSYS_NO_PATHCONV=1 docker run --rm --platform linux/amd64 -v "D:/Projetos/capivara_os:/src" -w /src \
  capivara-toolchain python3 tools/qemu-test.py tests/gui.txt --out build/test/gui
```

## Conferir o resultado

1. A última linha deve ser `OK: roteiro concluído sem erro do kernel`.
2. **Abra as capturas** (`build/test/<pasta>/*.png`) com a ferramenta de
   leitura de imagens e olhe a tela de verdade. Para comparar várias de uma
   vez, junte-as com o ImageMagick do container:
   `convert a.png b.png +append -resize 50% lado-a-lado.png`.
3. O log do kernel (o mesmo do `dmesg`) fica em `build/test/<pasta>/serial.log`.

## A linguagem do roteiro

Uma ação por linha. Linhas com `#` são comentários.

| Ação | Exemplo | O que faz |
|---|---|---|
| `waitlog TEXTO [s]` | `waitlog kmain: pronto` | espera o texto na serial (padrão: 120 s) |
| `sleep S` | `sleep 2.5` | espera |
| `shot NOME` | `shot 01-calc` | salva `NOME.png` na pasta de saída |
| `key COMBO` | `key ctrl-shift-esc` | teclas com nomes *qcode* do QEMU, unidas por `-` |
| `hold COMBO S` | `hold right-x 1.5` | segura as teclas por S segundos (jogos) |
| `type TEXTO` | `type sysinfo` | digita (ASCII; o mapa é do teclado ABNT2) |
| `move X Y` | `move 512 735` | move o mouse (tela de 1024×768) |
| `click X Y [right]` | `click 30 13` | clica |
| `dclick X Y` | `dclick 960 120` | clique duplo |
| `drag X1 Y1 X2 Y2` | `drag 300 40 600 200` | arrasta com o botão esquerdo |
| `wheel X Y N` | `wheel 500 400 -3` | roda do mouse (N>0 sobe) |

Nomes de teclas úteis: `ret`, `esc`, `tab`, `spc`, `backspace`, `delete`,
`up`/`down`/`left`/`right`, `f4`, `print`, `meta_l` (Super), `alt`, `ctrl`,
`shift`.

## Começo padrão de um roteiro

```text
waitlog kmain: pronto      # o kernel terminou de iniciar
sleep 4                    # a tela de bloqueio aparece
key ret                    # entra (sem senha)
sleep 3
key alt-f4                 # fecha o Bem-vindo.txt que abre no 1º login
sleep 1
```

## Pontos conhecidos da tela (1024×768)

| Onde | Coordenada / atalho |
|---|---|
| menu da capivara | `click 30 13` |
| Central de Controle | `click 876 13` |
| relógio → notificações | `click 960 13` |
| Dock (altura dos ícones) | y ≈ 735 |
| abrir app pela pesquisa | `key meta_l-spc`, `type calc`, `key ret` |
| Terminal / Explorador | `key meta_l-t` / `key meta_l-e` |
| Launchpad / Mission Control | `key meta_l` / `key meta_l-tab` |

Depois de abrir um app, espere de 2 a 3 s antes da captura. O QEMU sem KVM é
mais lento que o PC.

## Roteiros que já existem

- `tests/boot.txt`: só o boot; use com `--usb`/`--uefi`.
- `tests/gui.txt`: passeio geral (Dock, Launchpad, pesquisa, apps, Mission Control).
- `tests/recursos.txt`: layouts de encaixe, Central de Controle, notificações, menus.
- `tests/terminal.txt`: arquivos, `ps`/`kill`, `tree`.
- `tests/readme.txt`: as capturas da documentação (`docs/img/telas`).

Ao criar um roteiro, comece com um comentário dizendo o que ele cobre.
