# Próximos passos

## Sistemas distribuídos (caps. 47 a 50)

**No livro.** Com rede, as falhas são normais: mensagens se perdem, chegam fora
de ordem ou duplicadas. O capítulo constrói comunicação confiável sobre UDP
(confirmações, tempo esgotado e retransmissão) e chamadas de procedimento
remoto (RPC). Depois vêm o **NFS**, que usa um protocolo **sem estado** para
um servidor que cai poder voltar sem perder nada, e o **AFS**, que guarda
arquivos inteiros no cliente e avisa quando eles mudam.

**No Capivara.** Não há rede (item 4 do roteiro: placa e1000 ou virtio-net,
ARP, IP, UDP, TCP e DHCP). A ideia de mensagens já existe dentro do sistema:
os aplicativos conversam com o compositor por eventos numa fila, sem memória
compartilhada.

## Segurança

**No livro.** Os capítulos de segurança tratam de autenticação, controle de
acesso, criptografia e proteção da memória.

**No Capivara.** A tela de bloqueio guarda a senha como **hash com sal**, e não
como texto (`kernel/apps/session.c`). Fora isso, tudo roda no anel 0: um
aplicativo pode ler e escrever qualquer memória. A proteção começa pelo modo
usuário.

## Projetos para levar o Capivara adiante

Cada um é um trabalho de várias semanas e liga vários capítulos do livro.

- **Modo usuário e processos** (caps. 4 a 6, 13, 18, 23): uma PML4 por
  processo, a metade de baixo do espaço para ele, chamadas de sistema com
  `syscall`, um carregador de ELF e uma libc mínima.
- **Prioridades no escalonador** (cap. 8): um campo de prioridade em `task_t`,
  uma fila por nível e o boost.
- **Variáveis de condição** (cap. 30): criar uma API geral de espera e
  sinalização e uma terceira versão do produtor/consumidor. Semáforos e
  `task_join` já estão no laboratório `prodcons`.
- **Consistência após falhas** (caps. 40 e 42): simular um dispositivo de
  blocos, manter seus blocos ao descartar o cache e interromper a execução
  entre escritas. Comparar o estado recuperado com e sem diário. Os dados
  permaneceriam em RAM no sistema real; o experimento estudaria uma queda
  simulada, separada do rollback em memória do Tatu DB.
- **Disco** (caps. 37, 39 e 40): driver ATA ou AHCI e um sistema de arquivos
  simples, FAT32 ou um *vsfs* próprio.
- **Journaling** (cap. 42): um diário de metadados no sistema de arquivos novo.
- **Várias CPUs** (caps. 10 e 28): o APIC, um *spin lock* com instrução
  atômica no lugar de desligar interrupções e uma fila de prontas por CPU.

Antes de começar um deles, releia o capítulo, escreva o plano em papel e
procure no código os lugares que vão mudar. É assim que o livro foi pensado:
entender o mecanismo primeiro, e só então construir.
