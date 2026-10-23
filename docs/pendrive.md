# Pendrive bootável

Sim, dá para dar boot no Capivara OS a partir de um pendrive. O
`build/capivara.iso` é uma ISO **híbrida**: ela traz uma tabela de partições de
disco além da estrutura de CD. Por isso o mesmo arquivo, gravado "cru" num
pendrive, dá boot por **BIOS** (PCs antigos) e por **UEFI** (PCs modernos).
Nenhuma conversão é necessária.

> ⚠️ Gravar a ISO **apaga tudo** que estiver no pendrive.

## Gravando

**Linux ou macOS:**

```sh
sh build.sh usb             # lista os discos (procure o pendrive pelo tamanho/modelo)
sh build.sh usb /dev/sdX    # Linux
sh build.sh usb /dev/disk4  # macOS
```

O script se recusa a gravar em discos que não sejam USB ou removíveis, ou que
tenham partições do sistema em uso. Antes de gravar, ele pede que você digite o
nome do disco de novo.

O comando manual equivalente, no Linux, é:
`sudo dd if=build/capivara.iso of=/dev/sdX bs=4M conv=fsync status=progress`.

**Windows:** use o [Rufus](https://rufus.ie) ou o
[balenaEtcher](https://etcher.balena.io):

- **Rufus:** escolha o pendrive e a ISO e clique em Iniciar. Quando ele
  perguntar o modo, escolha **"Gravar em modo imagem DD"**. O modo ISO copia os
  arquivos e pode não dar boot.
- **balenaEtcher:** *Flash from file* → ISO → pendrive → *Flash*. Ele já grava
  em modo cru.

## Dando boot

1. Desligue o **Secure Boot** no setup da BIOS/UEFI. O GRUB do Capivara OS não
   é assinado pela Microsoft, então o firmware se recusaria a carregá-lo.
2. Ligue o PC com o pendrive conectado e abra o menu de boot. A tecla muda
   conforme o fabricante: geralmente F12, F11, F8, F9 ou Esc.
3. Escolha o pendrive. Pode aparecer duas vezes, uma com "UEFI" no nome; as
   duas funcionam.

O que foi testado: boot do pendrive (disco USB) por BIOS e por UEFI no QEMU,
com `sh build.sh test tests/boot.txt --usb --uefi`. Em hardware real, o
resultado depende do PC (veja abaixo).

## O que esperar em hardware real

O Capivara OS é um sistema de aprendizado e conhece pouco hardware. Ele roda
inteiro na memória RAM e não grava nada no disco do computador. Seus arquivos
somem ao desligar, e o Windows/Linux instalado no PC continua intacto.

| Item | Situação |
|---|---|
| Vídeo | funciona em praticamente qualquer PC: usa o framebuffer que o BIOS/UEFI entrega, na resolução que o firmware oferecer (1024×768 quando houver) |
| Teclado e touchpad de notebook | costumam funcionar: muitos notebooks ligam o teclado interno pelo controlador PS/2 |
| Teclado e mouse **USB** | só funcionam se o firmware emular PS/2 ("USB Legacy Support" ou CSM ligado no setup). O sistema ainda não tem driver USB (está no roteiro) |
| Desligar / reiniciar | por ACPI e pelo controlador do teclado; em alguns PCs pode ser preciso segurar o botão de energia |
| Disco, rede, som, Wi-Fi | ainda não existem |

Se travar logo depois do GRUB, tente estas opções no setup da BIOS/UEFI:

- **Ligar "USB Legacy Support"/CSM**, para o teclado e o mouse USB.
- **Desligar "8254 Clock Gating"** (existe em alguns PCs Intel recentes). Com
  essa opção ligada, o relógio PIT, que o escalonador usa, fica parado.

Se o PC não tiver nenhum controlador PS/2, o sistema registra isso no log e
segue sem teclado e mouse, em vez de travar.
