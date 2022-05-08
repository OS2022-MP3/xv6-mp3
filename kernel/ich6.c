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

#define SSYNC 0x34 // dword

#define CORBLBASE 0x40 // dword
#define CORBUBASE 0x44 // dword
#define CORBRP 0x4A // word
#define CORBCTL 0x4C // byte
#define CORBST 0x4D // byte
#define CORBSIZE 0x4E // byte

#define RIRBLBASE 0x50 // dword
#define RIRBUBASE 0x54 // dword
#define RIRBWP 0x58 // word
#define RINTCNT 0x5A // word
#define RIRBCTL 0x5C // byte
#define RIRBSTS 0x5D // byte
#define RIRBSIZE 0x5E // byte

#define IC 0x60 // dword
#define IR 0x64 // dword
#define IRS 0x68 // word

#define SDCTL 0x100 // 24 bits
#define SDSTS 0x103 // byte
#define SDLPIB 0x104 // dword
#define SDCBL 0x108 // dword
#define SDLVI 0x10C // word
#define SDFIFOW 0x10E // word
#define SDFIFOS 0x110 // word
#define SDFMT 0x112 // word
#define SDBDPL 0x118 // dword
#define SDBDPU 0x11C // dword

// CORB ring buffer
#define CORB_SIZE 256
static uint32 CORB_ring_buffer[CORB_SIZE];
void* corb_init()
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
  uint64 corb_addr = 0x40002000L;
  write_dw(config_regs, CORBLBASE, corb_addr & 0xffffffff);
  write_dw(config_regs, CORBUBASE, corb_addr >> 32);

  corb_addr = read_dw(config_regs, CORBUBASE);
  corb_addr = (corb_addr<<32);
  corb_addr |= read_dw(config_regs, CORBLBASE);

  // Reset Read Pointer
  write_w(config_regs, CORBRP, 1 << 15);

  // Run CORB
  write_b(config_regs, CORBCTL, 2); // CORB DMA Engine = DMA run
  printf("CORB Running\n");

  return (void*)corb_addr;
}

// RIRB ring buffer
#define RIRB_SIZE 256
struct RIRB_Entry
{
  uint32 Response;
  uint32 Resp_Ex;
};
static struct RIRB_Entry RIRB_ring_buffer[RIRB_SIZE];
void* rirb_init()
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
  uint64 rirb_addr = 0x40003000L;
  write_dw(config_regs, RIRBLBASE, rirb_addr & 0xffffffff);
  write_dw(config_regs, RIRBUBASE, rirb_addr >> 32);

  rirb_addr = read_dw(config_regs, CORBUBASE);
  rirb_addr = (rirb_addr<<32);
  rirb_addr |= read_dw(config_regs, CORBLBASE);

  // Reset Read Pointer
  write_w(config_regs, RIRBWP, 1 << 15);

  // Run RIRB
  write_b(config_regs, RIRBCTL, 2); // RIRB DMA Engine = DMA run
  printf("RIRB Running\n");

  return (void*)rirb_addr;
}

// payload = 8 bits. (for most codec control command)
uint32 get_verb_codec(uint32 Cad, uint32 NID, uint32 verbCode, uint32 payload)
{
  return (Cad << 28) | (NID << 20) | (verbCode << 8) | (payload);
}

// payload = 16 bits. (Gain/Mute, Converter Format, etc.)
uint32 get_verb_codec16(uint32 Cad, uint32 NID, uint32 verbCode, uint32 payload)
{
  return (Cad << 28) | (NID << 20) | (verbCode << 16) | (payload);
}

uint32 immediateCommand(uint32 Cad, uint32 NID, uint32 verbCode, uint32 payload)
{
  write_w(config_regs, IRS, 0x2);
  uint32 testVerb = get_verb_codec(Cad, NID, verbCode, payload);
  write_dw(config_regs, IC, testVerb);
  write_w(config_regs, IRS, 0x1);
  while(read_w(config_regs, IRS) != 2);
  return read_dw(config_regs, IR);
}

// payload = 16 bits
uint32 immediateCommand16(uint32 Cad, uint32 NID, uint32 verbCode, uint32 payload)
{
  write_w(config_regs, IRS, 0x2);
  uint32 testVerb = get_verb_codec16(Cad, NID, verbCode, payload);
  write_dw(config_regs, IC, testVerb);
  write_w(config_regs, IRS, 0x1);
  while(read_w(config_regs, IRS) != 2);
  return read_dw(config_regs, IR);
}

#define STREAM_DATA_SIZE 4096
uint16 streamDataList[STREAM_DATA_SIZE];
#define BDL_SIZE 128
uint32 bufferDespList[BDL_SIZE * 4];

void ich6_init(volatile uint32 *xregs)
{
  initlock(&ich6_lock, "ich6");
  config_regs = xregs;

  // Reset device by writing 1 to CRST (GCTL, bit 0)
  write_dw(config_regs, GCTL, read_dw(config_regs, GCTL) | 1);
  while((read_dw(config_regs, GCTL) & 1) != 1); // Waiting until CRST = 1
  // for(int i=0;i<1e9;i++);
  printf("ICH6 Reset Completed.\n");

  // Waiting for Status Change event.
  while(read_w(config_regs, STATESTS) == 0); // Waiting until STATESTS != 0
  // for(int i=0;i<1e9;i++);
  printf("Codec Found.\n");
  for(int i=0;i<3;i++)
    printf("  Codec Channel %d: %x\n", i, read_w(config_regs, STATESTS) & (1<<i));

  /*
  // CORB/RIRB init
  uint32 *corb_addr = corb_init();
  uint64 *rirb_addr = rirb_init();
  */

  // Immediate Command
  /*
  Node Info:
  NID 0 : root node
  NID 1 : Audio Function Group
  NID 2 : Audio Output
          - 16-bit audio
          - Sample Rate: 16.0kHz - 96.0 kHz (R7 = 48.0 kHz, Supported by all codecs)
  NID 3 : Pin Complex
          - Output Capable = 1
          - Connection List Length = 1
          - ConnectionList[0] = 2
  */

  uint32 random_num = 2333;
  uint32 random_mod = 1e9 + 7;
  for (int i=0;i<STREAM_DATA_SIZE;i++) {
    random_num = (random_num * 9973 + 2333) % random_mod;
    streamDataList[i] = random_num & 0xffff;
  }

  // 2 entries BDL
  uint64 bdl_base = ((uint64)bufferDespList & 0xffffff00) + 0x100;
  uint32* bdl_addr = (void*)bdl_base;
  // bdl[0]
  bdl_addr[0] = (uint64)streamDataList;
  //printf("%x\n",bdl_addr[0]);
  bdl_addr[1] = 0;
  bdl_addr[2] = 0x80;
  bdl_addr[3] = 0;
  // bdl[1]
  bdl_addr[4] = (uint64)streamDataList + 2 * 0x80;
  bdl_addr[5] = 0;
  bdl_addr[6] = 0x80;
  bdl_addr[7] = 0;

  //printf("%x\n",immediateCommand(0,2,0xf00,0x12) &0x0f);

  // config Stream Descriptor
  write_dw(config_regs, SDBDPL, (uint64)bdl_addr); // BDL address
  write_w(config_regs, SDFMT, 1 << 4); // set format = 48kHz, 16bit
  write_w(config_regs, SDLVI, 1); // last valid = 1  <=  bdl size = 2
  write_w(config_regs, SDCBL, 0x80); // buffer length
  write_dw(config_regs, SDCTL, (1 << 1) | (1 << 20)); // Stream run (Stream ID = 1)

  // Pin Widget
  //immediateCommand(0, 3, 0x707, 1 << 6); // pin output enable

  // Audio Output (DAC)
  immediateCommand(0, 2, 0x706, 1 << 4); // Connect to Stream 1, Channel 0
  immediateCommand16(0, 2, 0x3, (1 << 15) | (immediateCommand16(0, 2, 0xB, 1 << 15) ^ (1 << 7)));
  // No Need to disable mute
  immediateCommand16(0, 2, 0x2, 1 << 4); // set foramt = Stream format
  //printf("%x\n", immediateCommand16(0, 2, 0xA, 0));

  // SYNC
  printf("%x\n",read_dw(config_regs, SSYNC));
  write_dw(config_regs, SSYNC, 0);
  printf("%x\n", read_dw(config_regs, SSYNC));

  //printf("%x\n", immediateCommand16(0, 2, 0xB, 1 << 15));
}