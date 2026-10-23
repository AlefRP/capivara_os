/*
 * PCI "mecanismo de configuração #1": escreve-se o endereço (barramento,
 * dispositivo, função, registrador) na porta 0xCF8 e lê-se o valor na 0xCFC.
 */
#include "dev/pci.h"
#include "arch/x86_64/cpu.h"
#include "lib/printf.h"

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

static void fill(pci_device_t *d, uint8_t bus, uint8_t dev, uint8_t fn, uint32_t id) {
    d->bus = bus;
    d->dev = dev;
    d->fn = fn;
    d->vendor = id & 0xFFFF;
    d->device = id >> 16;
    uint32_t cls = pci_read32(bus, dev, fn, 0x08);
    d->class_code = cls >> 24;
    d->subclass = (cls >> 16) & 0xFF;
    d->irq = pci_read32(bus, dev, fn, 0x3C) & 0xFF;
}

int pci_scan(pci_device_t *out, int max) {
    int n = 0;
    for (int bus = 0; bus < 256; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0) break;
                    continue;
                }
                if (n < max) fill(&out[n], bus, dev, fn, id);
                n++;
                /* função 0 sem o bit "multifunção" no header type: não há outras */
                if (fn == 0 && !((pci_read32(bus, dev, 0, 0x0C) >> 16) & 0x80)) break;
            }
    return n;
}

bool pci_find(uint16_t vendor, uint16_t device, pci_device_t *out) {
    pci_device_t list[64];
    int n = pci_scan(list, 64);
    for (int i = 0; i < n && i < 64; i++)
        if (list[i].vendor == vendor && list[i].device == device) {
            *out = list[i];
            return true;
        }
    return false;
}

uint32_t pci_bar(const pci_device_t *d, int index) {
    return pci_read32(d->bus, d->dev, d->fn, 0x10 + index * 4);
}

void pci_enable(const pci_device_t *d) {
    uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, 0x04);
    pci_write32(d->bus, d->dev, d->fn, 0x04, cmd | 0x7);
}
