<#
.SYNOPSIS
  Compila e executa o Capivara OS a partir do Windows.

.DESCRIPTION
  As ferramentas GNU (GCC, binutils, GRUB) rodam dentro de um container Docker;
  nada precisa ser instalado no Windows além do Docker Desktop e de um
  virtualizador (VirtualBox ou QEMU).

.EXAMPLE
  .\build.ps1            # compila e gera build\capivara.iso
  .\build.ps1 vbox       # compila e abre no VirtualBox (cria a VM "Capivara OS" se preciso)
  .\build.ps1 qemu       # compila e abre no QEMU para Windows, se instalado
  .\build.ps1 test       # roda o roteiro de testes automáticos (tests\gui.txt)
  .\build.ps1 usb        # compila e explica como gravar a ISO num pendrive
  .\build.ps1 clean      # apaga a pasta build

  No Linux e no macOS, use o build.sh (veja docs\compilar.md).
#>
param(
    [ValidateSet("iso", "vbox", "qemu", "test", "usb", "clean", "toolchain")]
    [string]$Acao = "iso"
)

# "Continue": avisos que o docker escreve no stderr não devem abortar o script
# (os erros de verdade são detectados pelo código de saída, $LASTEXITCODE)
$ErrorActionPreference = "Continue"
$root = $PSScriptRoot
$image = "capivara-toolchain"
$iso = Join-Path $root "build\capivara.iso"

function Invoke-Container([string]$cmd) {
    docker run --rm --platform linux/amd64 -v "${root}:/src" -w /src $image sh -c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Falhou: $cmd" }
}

function Ensure-Toolchain([switch]$Force) {
    docker info *> $null
    if ($LASTEXITCODE -ne 0) { throw "O Docker não está rodando. Abra o Docker Desktop e tente de novo." }
    docker image inspect $image *> $null
    if ($Force -or $LASTEXITCODE -ne 0) {
        Write-Host "==> Construindo as ferramentas GNU (só na primeira vez, ~10 min)..."
        docker build --platform linux/amd64 -t $image (Join-Path $root "docker")
        if ($LASTEXITCODE -ne 0) { throw "Falha ao construir a imagem de ferramentas" }
    }
}

function Build-Iso {
    Ensure-Toolchain
    Invoke-Container "make -j`$(nproc)"
}

function Start-VirtualBox {
    $vbox = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe"
    if (-not (Test-Path $vbox)) { throw "VirtualBox não encontrado em $vbox" }
    $vm = "Capivara OS"
    & $vbox showvminfo $vm *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "==> Criando a máquina virtual '$vm'"
        & $vbox createvm --name $vm --ostype Other_64 --register | Out-Null
        & $vbox modifyvm $vm --memory 512 --vram 32 --graphicscontroller vmsvga --ioapic on `
            --firmware bios --mouse ps2 --keyboard ps2 --rtc-use-utc off --audio-enabled off | Out-Null
        & $vbox storagectl $vm --name "IDE" --add ide | Out-Null
    }
    & $vbox storageattach $vm --storagectl "IDE" --port 0 --device 0 --type dvddrive --medium $iso | Out-Null
    & $vbox startvm $vm | Out-Null
    Write-Host "==> VM iniciada. Clique dentro da janela para capturar o mouse (Ctrl direito solta)."
}

switch ($Acao) {
    "toolchain" { Ensure-Toolchain -Force }
    "iso"   { Build-Iso; Write-Host "ISO: $iso" }
    "vbox"  { Build-Iso; Start-VirtualBox }
    "qemu"  {
        Build-Iso
        $qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
        if (-not (Test-Path $qemu)) { throw "QEMU não encontrado. Instale com: winget install SoftwareFreedomConservancy.QEMU" }
        & $qemu -m 512M -cdrom $iso -rtc base=localtime -serial stdio
    }
    "test"  {
        Build-Iso
        Invoke-Container "python3 tools/qemu-test.py tests/gui.txt"
        Write-Host "Screenshots em build\test\"
    }
    "usb"   {
        Build-Iso
        Write-Host ""
        Write-Host "Grave $iso no pendrive com o Rufus (https://rufus.ie) ou o balenaEtcher."
        Write-Host "No Rufus, quando ele perguntar o modo, escolha 'Gravar em modo imagem DD'."
        Write-Host "ATENÇÃO: isso apaga tudo no pendrive. Detalhes em docs\pendrive.md"
    }
    "clean" { Ensure-Toolchain; Invoke-Container "make clean" }
}
