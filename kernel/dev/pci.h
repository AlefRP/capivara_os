/* Barramento PCI: acesso ao espaço de configuração pelas portas 0xCF8/0xCFC. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bus, dev, fn;
    uint16_t vendor, device;
    uint8_t class_code, subclass;
    uint8_t irq;
} pci_device_t;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
int      pci_scan(pci_device_t *out, int max);
bool     pci_find(uint16_t vendor, uint16_t device, pci_device_t *out);
uint32_t pci_bar(const pci_device_t *d, int index);
void     pci_enable(const pci_device_t *d);   /* liga E/S, memória e bus master */
