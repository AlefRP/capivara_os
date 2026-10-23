#!/usr/bin/env python3
"""Roda o Capivara OS no QEMU sem janela e executa um roteiro de testes.

Uso:  python3 tools/qemu-test.py ROTEIRO [--iso build/capivara.iso] [--out build/test]
                                         [--uefi] [--usb]

    --uefi   dá boot com firmware UEFI (OVMF) em vez da BIOS
    --usb    "grava" a ISO num pendrive virtual (disco USB) em vez de usar o
             drive de CD: é o mesmo caminho de boot de um pendrive de verdade

Comandos do roteiro (um por linha):
    sleep 2.5            espera (segundos)
    waitlog TEXTO [S]    espera TEXTO aparecer na serial (padrão: 120 s); falha se não vier
    (o roteiro falha, com código de saída 1, se o kernel entrar em pânico)
    shot nome            salva build/test/nome.png
    move X Y             move o mouse (coordenadas da tela 1024x768)
    click X Y [right]    clica
    dclick X Y           duplo clique
    drag X1 Y1 X2 Y2     arrasta com o botão esquerdo
    key ctrl-shift-esc   combinação de teclas (nomes "qcode" do QEMU)
    type texto           digita texto (ASCII simples)
    wheel X Y N          gira a roda (N>0 para cima)
"""
import json
import os
import socket
import subprocess
import sys
import time

W, H = 1024, 768


class QMP:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                if "error" in r:
                    print("QMP erro:", r["error"], file=sys.stderr)
                return r

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})

    def abs_move(self, x, y):
        ax = int(x * 0x7FFF / (W - 1))
        ay = int(y * 0x7FFF / (H - 1))
        self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}}])

    def button(self, btn, down):
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"down": down, "button": btn}}])


# caractere -> teclas (qcodes do QEMU) no layout ABNT2 usado pelo sistema
KEYMAP = {
    " ": ["spc"], ".": ["dot"], ",": ["comma"], "-": ["minus"], "=": ["equal"], "\n": ["ret"],
    "/": ["ro"], "?": ["shift", "ro"], ";": ["slash"], ":": ["shift", "slash"],
    ">": ["shift", "dot"], "<": ["shift", "comma"], "_": ["shift", "minus"], "+": ["shift", "equal"],
    "*": ["shift", "8"], "(": ["shift", "9"], ")": ["shift", "0"], "!": ["shift", "1"],
    "@": ["shift", "2"], "#": ["shift", "3"], "$": ["shift", "4"], "%": ["shift", "5"],
    "&": ["shift", "7"], "'": ["grave_accent"], '"': ["shift", "grave_accent"],
    "[": ["bracket_right"], "]": ["backslash"], "\\": ["less"], "|": ["shift", "less"],
}

# Onde cada sistema guarda o OVMF (firmware UEFI para o QEMU)
OVMF_PATHS = [
    "/usr/share/ovmf/OVMF.fd",                       # Debian/Ubuntu
    "/usr/share/edk2/x64/OVMF.4m.fd",                # Arch
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",             # Fedora
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",  # macOS (Homebrew)
    "/usr/local/share/qemu/edk2-x86_64-code.fd",
]


def uefi_firmware():
    path = os.environ.get("OVMF") or next((p for p in OVMF_PATHS if os.path.exists(p)), None)
    if not path:
        sys.exit("OVMF (firmware UEFI) não encontrado; instale o pacote ovmf/edk2-ovmf ou defina OVMF=/caminho")
    # arquivos *CODE* têm só o firmware: entram como memória flash somente leitura
    if "code" in os.path.basename(path).lower():
        return ["-drive", "if=pflash,format=raw,readonly=on,file=" + path]
    return ["-bios", path]


def read_log(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


# Mensagens que o kernel escreve na serial quando morre (panic.c)
PANIC_MARKS = ("*** PANIC", "*** EXCEÇÃO")


def check_panic(path):
    log = read_log(path)
    for mark in PANIC_MARKS:
        i = log.find(mark)
        if i >= 0:
            sys.exit("FALHOU: erro do kernel na serial:\n" + log[i:i + 600])


def wait_log(path, text, timeout):
    """Espera o texto aparecer na saída serial (ex.: 'kmain: pronto')."""
    end = time.time() + timeout
    while time.time() < end:
        check_panic(path)
        if text in read_log(path):
            return
        time.sleep(0.5)
    sys.exit("FALHOU: '%s' não apareceu na serial em %.0f s" % (text, timeout))


def main():
    script = sys.argv[1]
    iso = "build/capivara.iso"
    out = "build/test"
    args = sys.argv[2:]
    if "--iso" in args:
        iso = args[args.index("--iso") + 1]
    if "--out" in args:
        out = args[args.index("--out") + 1]
    os.makedirs(out, exist_ok=True)
    sock = "/tmp/capivara-qmp.sock"
    if os.path.exists(sock):
        os.unlink(sock)

    media = ["-cdrom", iso]
    if "--usb" in args:
        media = ["-device", "qemu-xhci", "-device", "usb-storage,drive=pendrive,bootindex=0",
                 "-drive", "if=none,id=pendrive,format=raw,readonly=on,file=" + iso]
    firmware = uefi_firmware() if "--uefi" in args else []

    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-m", "512M", *media, *firmware, "-display", "none",
        "-serial", "file:" + os.path.join(out, "serial.log"),
        "-qmp", "unix:%s,server,nowait" % sock, "-rtc", "base=localtime",
        "-no-reboot"])
    try:
        q = QMP(sock)
        serial = os.path.join(out, "serial.log")
        for raw in open(script, encoding="utf-8"):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            op, *a = line.split()
            print(">>", line, flush=True)
            if op == "sleep":
                time.sleep(float(a[0]))
            elif op == "waitlog":
                timeout = float(a.pop()) if len(a) > 1 and a[-1].replace(".", "").isdigit() else 120
                wait_log(serial, " ".join(a), timeout)
            elif op == "shot":
                ppm = "/tmp/shot.ppm"
                q.hmp("screendump " + ppm)
                time.sleep(0.3)
                subprocess.run(["convert", ppm, os.path.join(out, a[0] + ".png")], check=False)
            elif op == "move":
                q.abs_move(int(a[0]), int(a[1]))
            elif op in ("click", "dclick"):
                q.abs_move(int(a[0]), int(a[1]))
                time.sleep(0.15)
                btn = "right" if len(a) > 2 and a[2] == "right" else "left"
                for _ in range(2 if op == "dclick" else 1):
                    q.button(btn, True)
                    time.sleep(0.08)
                    q.button(btn, False)
                    time.sleep(0.08)
            elif op == "drag":
                x1, y1, x2, y2 = map(int, a)
                q.abs_move(x1, y1)
                time.sleep(0.15)
                q.button("left", True)
                time.sleep(0.1)
                steps = 10
                for i in range(1, steps + 1):
                    q.abs_move(x1 + (x2 - x1) * i // steps, y1 + (y2 - y1) * i // steps)
                    time.sleep(0.05)
                time.sleep(0.1)
                q.button("left", False)
            elif op == "key":
                keys = [{"type": "qcode", "data": k} for k in a[0].split("-")]
                q.cmd("send-key", keys=keys)
            elif op == "type":
                text = line[len("type "):]
                for ch in text:
                    if ch.isupper():
                        keys = ["shift", ch.lower()]
                    else:
                        keys = KEYMAP.get(ch, [ch])
                    q.cmd("send-key", keys=[{"type": "qcode", "data": k} for k in keys])
                    time.sleep(0.05)
            elif op == "wheel":
                q.abs_move(int(a[0]), int(a[1]))
                n = int(a[2])
                for _ in range(abs(n)):
                    b = "wheel-up" if n > 0 else "wheel-down"
                    q.button(b, True)
                    q.button(b, False)
                    time.sleep(0.05)
            time.sleep(0.2)
            check_panic(serial)
        q.cmd("quit")
    finally:
        try:
            qemu.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qemu.kill()
    print("OK: roteiro concluído sem erro do kernel")


if __name__ == "__main__":
    main()
