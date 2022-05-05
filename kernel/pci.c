//
// simple PCI-Express initialization, only
// works for qemu and its AC97 card.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define PCICMD (0x04/4)
#define BUS_BASE (0x10/4)
#define AZCTL (0x40/4)

void
pci_init()
{
  // qemu -machine virt puts PCIe config space here.
  // vm.c maps this range.
  uint32  *ecam = (uint32 *) 0x30000000L;
  uint64 hda_regs = 0x40000000L;

  // look at each possible PCI device on bus 0.
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
    volatile uint32 *base = ecam + off;
    uint32 id = base[0];

    // 0x26688086 is ich6
    if(id == 0x26688086){
      printf("ICH6 Found\n");
      // command and status register.
      // bit 0 : I/O access enable
      // bit 1 : memory access enable
      // bit 2 : enable mastering
      base[PCICMD] = 7;
      __sync_synchronize();

      // tell the ich6 to reveal its registers at
      // physical address 0x40000000.
      base[BUS_BASE+0] = hda_regs;
      __sync_synchronize();

      ich6_init((uint32*)hda_regs);
      return ;
    }
  }
  printf("ERROR: ICH6 NOT FOUND\n");
}