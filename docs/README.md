<div align="center">
<img src="img/logo.png" width="96" alt="Logo do Capivara OS: uma capivara com uma laranjinha na cabeça">

# 📚 Documentação do Capivara OS
</div>

| | Documento | Para quem |
|:-:|---|---|
| 🧭 | [Guia de uso](guia-de-uso.md) | quem quer **usar**: tela, janelas, atalhos, aplicativos e comandos do Terminal |
| 🏗️ | [Arquitetura](arquitetura.md) | quem quer **entender**: camadas, boot, memória, interrupções, o caminho de um clique |
| 🛠️ | [Como compilar](compilar.md) | quem quer **gerar a ISO** no Windows, no Linux ou no macOS |
| 💾 | [Pendrive bootável](pendrive.md) | quem quer **dar boot num PC de verdade** |
| 🦔 | [Tatu DB e Toca](tatu.md) | banco SQL nativo, cliente gráfico, formato e limites de persistência |
| 🧼 | [Clean room](clean-room.md) | como o sistema foi feito **sem copiar** código, ícones ou imagens |

## 📐 Especificações

Foram escritas antes do código, no estilo *clean room*: descrevem **o que**
cada parte faz, com palavras próprias.

1. [Boot e memória](spec/01-boot-e-memoria.md): Multiboot2, modo longo, paginação, heap
2. [Interrupções, tarefas e dispositivos](spec/02-interrupcoes-e-tarefas.md): IDT, escalonador, filas de eventos, drivers
3. [Interface gráfica](spec/03-interface-grafica.md): compositor, janelas, Dock, temas, papel de parede

## 🖼️ Imagens

Tudo em [img/](img/) foi gerado pelo próprio sistema:

- `banner.png`, `logo.png`, `papel-de-parede.png` e `icones/` saem do código
  gráfico do kernel compilado como programa comum: `make arte`
  ([tools/arte/arte.c](../tools/arte/arte.c)).
- `telas/` são capturas do sistema rodando no QEMU:
  `sh build.sh test tests/readme.txt --out docs/img/telas`.

<p align="center"><img src="img/papel-de-parede.png" width="640" alt="O papel de parede padrão: beira de brejo ao pôr do sol com capivaras, em pixel art"></p>
