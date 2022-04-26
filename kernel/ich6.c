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

struct spinlock ich6_lock;

#define GCTL 0x08 // dword
#define STATESTS 0x0E // word
#define WAKEEN 0x0C // word
#define INTCTL 0x20 // dword

#define CORBLBASE 0x40 // dword
#define CORBUBASE 0x44 // dword
#define CORBRP 0x4A // word
#define CORBCTL 0x4C // byte
#define CORBSIZE 0x4E // byte

#define RIRBLBASE 0x50 // dword
#define RIRBUBASE 0x54 // dword
#define RIRBWP 0x58 // word
#define RINTCNT 0x5A // word
#define RIRBCTL 0x5C // byte
#define RIRBSTS 0x5D // byte
#define RIRBSIZE 0x5E // byte


// CORB ring buffer
#define CORB_SIZE 256
static uint32 CORB_ring_buffer[CORB_SIZE];
void corb_init()
{
  // Ensure CORB is stopped.
  if ((read_b(config_regs, CORBCTL) >> 1) & 1) // DMA running
  {
    printf("Stopping CORB\n");
    write_b(config_regs, CORBCTL, 0); // CORB DMA Engine = DMA stop
    while ((read_b(config_regs, CORBCTL) >> 1) & 1); // Waiting for stop
  }
  printf("CORB Stopped\n");

  memset(CORB_ring_buffer, 0, sizeof(CORB_ring_buffer));
  // Tell CORB the address of the ring buffer.
  uint64 corb_addr = (uint64)CORB_ring_buffer;
  write_dw(config_regs, CORBLBASE, corb_addr & 0xffffffff);
  write_dw(config_regs, CORBUBASE, corb_addr >> 32);

  // Reset Read Pointer
  write_w(config_regs, CORBRP, 1 << 15);

  // Run CORB
  write_b(config_regs, CORBCTL, 2); // CORB DMA Engine = DMA run
  printf("CORB Running\n");
}

// RIRB ring buffer
#define RIRB_SIZE 256
struct RIRB_Entry
{
  uint32 Response;
  uint32 Resp_Ex;
};
static struct RIRB_Entry RIRB_ring_buffer[RIRB_SIZE];
void rirb_init()
{
  // Ensure RIRB is stopped.
  if ((read_b(config_regs, RIRBCTL) >> 1) & 1) // DMA running
  {
    printf("Stopping RIRB\n");
    write_b(config_regs, RIRBCTL, 0); // RIRB DMA Engine = DMA stop
    while ((read_b(config_regs, RIRBCTL) >> 1) & 1); // Waiting for stop
  }
  printf("RIRB Stopped\n");

  memset(RIRB_ring_buffer, 0, sizeof(RIRB_ring_buffer));
  // Tell RIRB the address of the ring buffer.
  uint64 rirb_addr = (uint64)RIRB_ring_buffer;
  write_dw(config_regs, RIRBLBASE, rirb_addr & 0xffffffff);
  write_dw(config_regs, RIRBUBASE, rirb_addr >> 32);

  // Reset Read Pointer
  write_w(config_regs, RIRBWP, 1 << 15);

  // Run RIRB
  write_b(config_regs, RIRBCTL, 2); // RIRB DMA Engine = DMA run
  printf("RIRB Running\n");
}

uint32 get_verb_codec(uint32 Cad, uint32 NID, uint32 verbCode, uint32 payload)
{
  return (Cad << 28) | (NID << 20) | (verbCode << 8) | (payload);
}

void ich6_init(volatile uint32 *xregs)
{
  initlock(&ich6_lock, "ich6");
  config_regs = xregs;

  // Reset device by writing 1 to CRST (GCTL, bit 0)
  write_dw(config_regs, GCTL, read_dw(config_regs, GCTL) | 1);
  while((read_dw(config_regs, GCTL) & 1) != 1); // Waiting until CRST = 1
  printf("ICH6 Reset Completed.\n");

  // Waiting for Status Change event.
  while(read_w(config_regs, STATESTS) == 0); // Waiting until STATESTS != 0, may take 521 us for codec linking.
  printf("Codec Found.\n");
  for(int i=0;i<3;i++)
    printf("  Codec Channel %d: %x\n", i, read_w(config_regs, STATESTS) & (1<<i));

  // CORB/RIRB init
  corb_init();
  rirb_init();

  // DEBUG: CORB/RIRB test
  uint32 testVerb = get_verb_codec(0, 0, 0, 0);
  CORB_ring_buffer[1] = testVerb;
  write_w(config_regs, CORBRP, 0x1);
  while (read_b(config_regs, RIRBWP) == 0);
  printf("%x\n", RIRB_ring_buffer[0].Response);

  // TODO: DMA and SD
}