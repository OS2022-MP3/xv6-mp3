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

void
pci_init()
{
  // we'll place the AC97 registers at this address.
  // vm.c maps this range.
  // uint64 AC97_regs = 0x40000000L;

  // qemu -machine virt puts PCIe config space here.
  // vm.c maps this range.
  uint32  *ecam = (uint32 *) 0x30000000L;

  // look at each possible PCI device on bus 0.
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
    volatile uint32 *base = ecam + off;
    uint32 id = base[0];

    // 0x24158086 is AC97
    if(id == 0x24158086){
      printf("Sound Card Found\n");
      AC97_init(base);
      return ;
    }
  }
  printf("SOUND CARD NOT FOUND\n");
}