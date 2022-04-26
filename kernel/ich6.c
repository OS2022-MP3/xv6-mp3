#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void* find_regs(volatile uint32* base_p, uint32 offset)
{
  uint32 base_i = (uint64)base_p;
  uint64 addr_i = base_i + offset;
  return (void*)addr_i;;
}

// read 1 byte
uint8 read_b(volatile uint32* base_p, uint32 offset)
{
  uint8* addr_p = find_regs(base_p, offset);
  return addr_p[0];
}
// write {value} to 1 byte
void write_b(volatile uint32* base_p, uint32 offset, uint8 value)
{
  uint8* addr_p = find_regs(base_p, offset);
  addr_p[0] = value;
  __sync_synchronize();
}
// read word (2 bytes)
uint16 read_w(volatile uint32* base_p, uint32 offset)
{
  uint16* addr_p = find_regs(base_p, offset);
  return addr_p[0];
}
// write {value} to word (2 bytes)
void write_w(volatile uint32* base_p, uint32 offset, uint16 value)
{
  uint16* addr_p = find_regs(base_p, offset);
  addr_p[0] = value;
  __sync_synchronize();
}
// read dword (4 bytes)
uint32 read_dw(volatile uint32* base_p, uint32 offset)
{
  uint32* addr_p = find_regs(base_p, offset);
  return addr_p[0];
}
// write {value} to dword (4 bytes)
void write_dw(volatile uint32* base_p, uint32 offset, uint32 value)
{
  uint32* addr_p = find_regs(base_p, offset);
  addr_p[0] = value;
  __sync_synchronize();
}

// remember where the ich6's registers live.
static volatile uint32 *regs;

struct spinlock ich6_lock;

#define GCTL 0x08
#define STATESTS 0x0E

void ich6_init(volatile uint32 *xregs)
{
  initlock(&ich6_lock, "ich6");
  regs = xregs;

  // Reset device by writing 1 to CRST (GCTL, bit 0)
  write_dw(regs, GCTL, read_dw(regs, GCTL) | 1);
  if (read_dw(regs, GCTL) & 1)
    printf("ICH6 RESET COMPLETE\n");
  else
    printf("ERROR: ICH6 RESET FAILED\n");

  // printf("%x\n", read_dw(regs, STATESTS));

}