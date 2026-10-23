#!/usr/bin/env python3
"""Empacota uma pasta (rootfs/) no initrd: um tar no formato USTAR.

Feito em Python para dar o mesmo resultado em qualquer sistema: o "tar" do
macOS/BSD e o GNU tar do Linux aceitam opções diferentes. Dono, grupo e
permissões são fixados, para o arquivo não depender de quem compilou.

Uso: mkinitrd.py <pasta> <saida.tar>
"""
import sys
import tarfile


def normalize(info):
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    info.mode = 0o755 if info.isdir() else 0o644
    return info


def main():
    src, out = sys.argv[1], sys.argv[2]
    with tarfile.open(out, "w", format=tarfile.USTAR_FORMAT, encoding="utf-8") as tar:
        tar.add(src, arcname=".", filter=normalize)


if __name__ == "__main__":
    main()
