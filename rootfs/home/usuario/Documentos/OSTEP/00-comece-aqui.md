# Estudando o OSTEP com o Capivara OS

O livro **Operating Systems: Three Easy Pieces** (OSTEP), de Remzi e Andrea
Arpaci-Dusseau, é grátis na internet, em pages.cs.wisc.edu/~remzi/OSTEP. Ele
divide o assunto em três "peças fáceis": **virtualização**, **concorrência** e
**persistência**.

Este guia não substitui o livro e não copia nada dele. É um caderno de
laboratório: para cada capítulo, ele diz onde aquela ideia aparece no código
do Capivara (ou por que ainda não aparece) e dá experimentos para rodar no
próprio sistema.

## Como usar

1. Leia o capítulo no livro.
2. Abra a parte correspondente aqui: no app **Estudos** ou nos arquivos da
   pasta Documentos/OSTEP.
3. Rode os experimentos. No app Estudos, clique duas vezes numa linha de
   comando para abrir o Terminal já com ela.
4. Leia a função citada no código. O código do Capivara está em
   github.com/AlefRP/capivara_os, na pasta kernel.
5. Responda o "Para pensar" com as suas palavras.

Antes de rodar cada experimento, anote o resultado que espera obter. Depois
compare a previsão com os números, confira as condições de correção e mude
uma opção por vez. Repita: prever, executar, medir, explicar e alterar.

## O que o Capivara é, e o que ainda não é

- Um kernel de 64 bits, monolítico, para **uma CPU**.
- **Tudo roda no anel 0** (modo kernel). Os aplicativos são *tarefas do
  kernel*, parecidas com threads: dividem o mesmo espaço de endereçamento e não
  existem chamadas de sistema.
- Ou seja: a CPU é virtualizada (há um escalonador preemptivo de verdade), mas a
  memória não (todo mundo enxerga tudo). Comparar isso com o livro é um ótimo
  exercício, e cada capítulo aqui aponta essa diferença.
- O que falta está no roteiro do projeto: modo usuário com processos, disco com
  sistema de arquivos persistente e rede.

## Mapa do guia

- **01-introducao**: cap. 2, o que um sistema operacional faz.
- **02-processos**: caps. 4 a 6, processos, a API de processos e a execução
  direta limitada. Laboratórios: `ps`, `trocas`, `kill`.
- **03-escalonamento**: caps. 7 a 10, FIFO, SJF, STCF, RR, MLFQ, loteria e
  várias CPUs. Laboratório: `escalonar`.
- **04-espacos-de-enderecamento**: caps. 13 a 16, espaço de endereçamento, API de
  memória, tradução de endereços e segmentação. Laboratórios: `traduzir`, `mem`.
- **05-espaco-livre**: cap. 17, como um alocador acha e devolve blocos.
  Laboratório: `heap`.
- **06-paginacao**: caps. 18 a 20, paginação, TLB e tabelas de vários níveis.
  Laboratório: `traduzir`.
- **07-alem-da-memoria**: caps. 21 a 23, troca com o disco e políticas de
  substituição. Laboratório: `paginas`.
- **08-concorrencia-e-locks**: caps. 26 a 29, threads, condição de corrida e
  locks. Laboratório: `corrida`.
- **09-condicoes-e-eventos**: caps. 30 a 33, variáveis de condição, semáforos,
  erros de concorrência e programação por eventos. Laboratório: `prodcons`.
- **10-dispositivos-e-discos**: caps. 36 a 38, E/S, discos e RAID.
  Laboratórios: `som teste`, `disco`.
- **11-arquivos**: caps. 39 a 45, arquivos, diretórios e como um sistema de
  arquivos é feito. Laboratórios: `stat`, `tree`.
- **12-proximos-passos**: sistemas distribuídos, segurança e projetos para
  levar o Capivara adiante.

## Os laboratórios

Digite no Terminal:

```sh
ostep
```

Uns olham o sistema de verdade: `trocas` (troca de contexto), `traduzir`
(tabelas de página), `heap` (espaço livre), `corrida` (locks), `prodcons`
(semáforos, espera e integridade de uma fila) e `stat`
(arquivos). Outros são simuladores para o que o Capivara não tem:
`escalonar`, `paginas` e `disco`. Os simuladores seguem as regras dos capítulos,
passo a passo, para você conferir com as figuras do livro.

## Sobre a numeração

Os números de capítulo são os da edição online do livro. Em outra edição eles
podem mudar um pouco: na dúvida, guie-se pelo título.
