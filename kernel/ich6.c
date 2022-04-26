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

// remember where the ich6's configuration registers live.
static volatile uint32 *config_regs;
#define CONFIG_REGS_SIZE 0x2000 // space reserved for configuration registers
static volatile uint32 *CORB_base;
#define CORB_SIZE 1024

struct spinlock ich6_lock;

#define GCTL 0x08 // dword
#define STATESTS 0x0E // word
#define WAKEEN 0x0C // word
#define INTCTL 0x20 // dword

#define CORBLBASE 0x40 // dword
#define CORBUBASE 0x44 // dword
#define CORBCTL 0x4C // byte
#define CORBSIZE 0x4E // byte

void ich6_init(volatile uint32 *xregs)
{
  initlock(&ich6_lock, "ich6");
  config_regs = xregs; //

  // Reset device by writing 1 to CRST (GCTL, bit 0)
  write_dw(config_regs, INTCTL, 0);
  write_dw(config_regs, GCTL, read_dw(config_regs, GCTL) | 1);
  write_dw(config_regs, INTCTL, 0);
  while((read_dw(config_regs, GCTL) & 1) != 1); // Waiting until CRST = 1
  printf("ICH6 Reset Completed.\n");

  // Waiting for Status Change event.
  while(read_w(config_regs, STATESTS) == 0); // Waiting until STATESTS != 0, may take 521 us for codec linking.
  printf("Codec Found.\n");
  for(int i=0;i<3;i++)
    printf("  Codec Channel %d: %x\n", i, read_w(config_regs, STATESTS) & (1<<i));

  // TODO: CORB/RIRB init
  CORB_base = find_regs(config_regs, CONFIG_REGS_SIZE);
  uint32 CORB_base_dw = (uint64)CORB_base;
  write_dw(config_regs, CORBLBASE, CORB_base_dw);

  printf("CORBCTL: %x\n", read_dw(config_regs, CORBCTL));
  write_dw(CORB_base, 0, 0);

  // TODO: DMA and SD
}