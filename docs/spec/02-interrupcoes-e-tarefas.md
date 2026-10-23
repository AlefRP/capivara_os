# Especificação 02 — Interrupções, tarefas e dispositivos

## Interrupções

- **IDT** com 256 entradas. 0–31: exceções da CPU. 32–47: IRQs de hardware
  (o PIC 8259 é remapeado para 0x20–0x2F, pois os vetores originais colidem com
  as exceções). 0x81: "ceder a vez" (*yield*) do escalonador.
- Todo vetor passa por um *stub* em assembly que empilha os registradores numa
  estrutura `regs_t` e chama `isr_dispatch(regs)`.
- `isr_dispatch` **devolve** um ponteiro `regs_t*`. Se devolver o de outra
  tarefa, o stub restaura os registradores *dela*: é assim que a troca de
  contexto acontece (mesma ideia de "salvar o contexto na pilha de kernel"
  usada nos Unix).

## Tarefas e escalonador

- Cada tarefa tem: pilha de kernel própria, `regs_t` salvo, área FXSAVE de
  512 bytes (registradores SSE), estado e uma **fila de eventos**.
- Estados: `PRONTA`, `RODANDO`, `DORMINDO` (até um instante), `BLOQUEADA`
  (esperando evento), `MORTA`.
- **Round-robin preemptivo**: o PIT gera 1000 interrupções/s; a cada 10 ms a
  tarefa atual perde a CPU para a próxima pronta.
- A tarefa *idle* executa `hlt` quando ninguém mais quer rodar e recolhe as
  tarefas mortas (libera pilha e memória).
- **Mutex** simples: quem não consegue o lock cede a vez até ele ser liberado.
- **Finalizar tarefa**: marca um pedido; quando a vítima for escalonada fora de
  qualquer seção crítica, o contexto salvo dela é trocado para a rotina de
  saída (mesma ideia da entrega de um sinal `SIGKILL`).

> Fase 1: aplicativos são *threads* de kernel (como no início do Windows NT,
> em que a interface gráfica rodava dentro do kernel). Fase 2: processos em
> modo usuário (anel 3), cada um com seu espaço de endereçamento.

## Fila de eventos (mensagens)

Cada tarefa tem uma fila circular de `event_t` (mouse, teclado, timer,
redimensionar, fechar…). `ev_wait()` bloqueia até chegar um evento. Movimentos
de mouse consecutivos são fundidos para a fila não lotar.

## Dispositivos

| Dispositivo | Uso |
|---|---|
| Serial COM1 (16550) | log do kernel (`kprintf`) — essencial para depurar |
| PIT 8254 | relógio do sistema, 1 kHz |
| Controlador PS/2 (8042) | teclado (IRQ1) e mouse (IRQ12, com roda) |
| VMware *backdoor* (porta 0x5658) | mouse absoluto no QEMU, sem "capturar" o cursor |
| CMOS RTC | data e hora na inicialização |
| ACPI (FADT + objeto `\_S5`) | desligar o computador |
| PCI (portas 0xCF8/0xCFC) | localizar dispositivos pelo par fabricante:dispositivo |
| VirtualBox VMMDev (PCI 80EE:CAFE) | mouse absoluto no VirtualBox: o convidado escreve o endereço físico de uma "requisição" na porta do dispositivo |

### Teclado

- Scancodes do *set 1* (o controlador traduz). Prefixo `0xE0` = tecla estendida.
- Layout **ABNT2** com teclas mortas: agudo, crase, til, circunflexo e trema + vogal → caractere acentuado;
  `Ç` direto; AltGr+Q = `/`, AltGr+W = `?`.
- Layout alternativo **EUA** (sem teclas mortas), para teclados americanos:
  o hipervisor repassa a POSIÇÃO física da tecla, então o layout precisa
  corresponder ao teclado real.
- Cada evento leva: código da tecla, caractere Unicode e modificadores.
