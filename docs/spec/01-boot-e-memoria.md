# Especificação 01 — Boot e memória

## Boot (Multiboot2)

- O GRUB carrega o kernel (ELF64) e entra no nosso código em **modo protegido
  32 bits**, com `EAX = 0x36D76289` e `EBX` = endereço físico da estrutura de
  informações de boot.
- O cabeçalho Multiboot2 do kernel pede um **framebuffer linear de
  1024×768×32 bits**. A interface gráfica desenha direto nessa memória.
- A estrutura de informações é uma lista de *tags* (alinhadas em 8 bytes).
  Tags usadas: mapa de memória (6), framebuffer (8), módulos (3), linha de
  comando (1), nome do carregador (2) e ponteiro ACPI RSDP (14/15).

### Passagem para 64 bits

1. Montar tabelas de página mínimas com páginas de 2 MiB:
   - identidade `0..1 GiB → 0..1 GiB` (necessária no instante da troca);
   - metade superior `0xFFFFFFFF80000000 → 0` (onde o kernel é linkado).
2. Ligar PAE (CR4.5), o bit LME do registrador EFER, e paginação (CR0.31).
3. Carregar uma GDT com segmento de código 64 bits e fazer um salto longo.
4. Pular para a metade superior, trocar a pilha e chamar `kmain`.

Esse "kernel na metade superior" é o mesmo modelo dos Unix modernos: o
espaço baixo fica livre para os processos de usuário.

## Mapa de endereços virtuais

| Faixa | Conteúdo |
|---|---|
| `0x0000000000000000 …` | (futuro) processos em modo usuário |
| `0xFFFF800000000000 …` | *physmap*: toda a memória física mapeada linearmente |
| `0xFFFFA00000000000 …` | heap do kernel (`kmalloc`), cresce sob demanda |
| `0xFFFFFFFF80000000 …` | imagem do kernel (1 GiB inicial) |

O *physmap* permite que o kernel acesse qualquer endereço físico somando uma
constante, conceito idêntico ao "direct map" descrito na documentação do Linux.

## Gerência de memória física (PMM)

- **Bitmap**: 1 bit por página de 4 KiB (1 = ocupada).
- Começa tudo ocupado; libera as regiões que o mapa de memória marca como
  "disponível"; reocupa o primeiro 1 MiB, o kernel, os módulos e o próprio bitmap.
- `pmm_alloc()` devolve uma página física livre; `pmm_free()` devolve à lista.

## Gerência de memória virtual (VMM)

- Tabelas de 4 níveis (PML4 → PDPT → PD → PT).
- `vmm_map(virt, phys, flags)` cria as tabelas intermediárias que faltarem.
- Depois do *physmap* pronto, o mapeamento identidade é removido: qualquer
  acesso a ponteiro nulo passa a gerar *page fault* (útil para achar bugs).

## Heap do kernel

- Lista de blocos contíguos com cabeçalho `{tamanho, livre, próximo, anterior}`.
- *First fit*: procura o primeiro bloco livre que caiba, divide se sobrar espaço.
- Ao liberar, junta com os vizinhos livres (coalescência).
- Se não houver espaço, mapeia mais páginas no fim do heap.
