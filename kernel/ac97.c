#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "audio_def.h"

#define PCIE_PIO 0x3000000
#define PCIE_ECAM 0x30000000L

/*
 * Reference to Intel doc AC97
 */
#define PCI_CONFIG_SPACE_STS_CMD 0x4
#define PCI_CONFIG_SPACE_NAMBAAR 0x10
#define PCI_CONFIG_SPACE_NABMBAAR 0x14
#define PCI_CONFIG_SPACE_SID_SVID 0x2C
#define PCI_CONFIG_SPACE_INTPN_LN 0x3C

uint16 namba; // native audio mixer base address
uint16 nabmba; // native audio bus mastering base address

#define NAMBA_PCMV namba + 0x2
#define NAMBA_PCVID1 namba + 0x7C
#define NAMBA_PCVID2 namba + 0x7E
#define FRONT_DAC_RATE namba + 0x2C
#define SURROUND_DAC_RATE namba + 0x2E
#define LFE_DAC_RATE namba + 0x30

#define NABMBA_GLOB_CNT nabmba + 0x2C
#define NABMBA_GLOB_STA nabmba + 0x30
#define PO_BDBAR nabmba + 0x10//PCM Out Buffer Descriptor list Base Address Register 
#define PO_LVI nabmba + 0x15//PCM Out Last Valid Index 
#define PO_SR nabmba + 0x16//PCM Out Status Register
#define PO_CR nabmba + 0x1B //PCM Out Control Register
#define MC_BDBAR nabmba + 0x20//Mic. In Buffer Descriptor list Base Address Register
#define MC_LVI nabmba + 0x25//Mic. In Last Valid Index
#define MC_SR nabmba + 0x26//Mic. In Status Register
#define MC_CR nabmba + 0x2B//Mic. In Control Register

#define FOR(i, a, b) for (uint32 i = (a), i##_END_ = (b); i <= i##_END_; ++i)

// all registers address can be found in https://wiki.osdev.org/AC97


static struct spinlock sound_lock;
static struct soundNode *soundQueue;

struct descriptor{
  uint32 buf;
  uint cmd_len;
};

static struct descriptor descriTable[DMA_BUF_NUM];

volatile uint8* RegByte(uint64 reg) {return (volatile uchar *)(reg);}
volatile uint16* RegShort(uint64 reg) {return (volatile ushort *)(reg);}
volatile uint32* RegInt(uint64 reg) {return (volatile uint32 *)(reg);}

uint8 ReadRegByte(uint64 reg) {return *RegByte(reg);}
void WriteRegByte(uint64 reg, uchar v) {*RegByte(reg) = v;}
uint16 ReadRegShort(uint64 reg) {return *RegShort(reg);}
void WriteRegShort(uint64 reg, ushort v) {*RegShort(reg) = v;}
uint32 ReadRegInt(uint64 reg) {return *RegInt(reg);}
void WriteRegInt(uint64 reg, uint32 v) {*RegInt(reg) = v;}

// Ecam: PCI Configuration Space
uint32 read_pci_config_int(uint32 bus, uint32 slot, uint32 func, uint32 offset) {
  return ReadRegInt((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM);
}

uint32 read_pci_config_short(uint32 bus, uint32 slot, uint32 func, uint32 offset) {
  return ReadRegShort((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM);
}

uint32 read_pci_config_byte(uint32 bus, uint32 slot, uint32 func, uint32 offset) {
  return ReadRegByte((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM);
}

void write_pci_config_byte(uint32 bus, uint32 slot, uint32 func, uint32 offset, uchar val) {
  WriteRegByte((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM, val);
}

void write_pci_config_short(uint32 bus, uint32 slot, uint32 func, uint32 offset, ushort val) {
  WriteRegShort((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM, val);
}

void write_pci_config_int(uint32 bus, uint32 slot, uint32 func, uint32 offset, uint32 val) {
  WriteRegInt((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM, val);
}

void test();

void soundcard_init(uint32 bus, uint32 slot, uint32 func) {
  initlock(&sound_lock, "sound");

  // Initializing the Audio I/O Space
  write_pci_config_byte(bus, slot, func, PCI_CONFIG_SPACE_STS_CMD, 0x5);

  // Write Native Audio Mixer Base Address
  write_pci_config_int(bus, slot, func, 0x10, 0x1001);
  namba = read_pci_config_int(bus, slot, func, 0x10) & (~0x1);

  // Write Native Audio Bus Mastering Base Address
  write_pci_config_int(bus, slot, func, 0x14, 0x1401);
  nabmba = read_pci_config_int(bus, slot, func, 0x14) & (~0x1);

  printf("AUDIO I/O Space initialized successfully!\n");

  // Hardware Interrupt Routing
  //write_pci_config_byte(bus, slot, func, 0x3c, PCI_IRQ); no effect !!!

  // Removing AC_RESET#
  WriteRegByte(PCIE_PIO | (NABMBA_GLOB_CNT), 0x2);

  // Check until codec ready
  uint32 wait_time = 1000;
  while (!(ReadRegShort(PCIE_PIO | (NABMBA_GLOB_STA)) & (0x100)) && wait_time) {
    --wait_time;
  }
  if (!wait_time) {
    panic("Audio Init failed 0.");
    return;
  }
  printf("Codec is ready!\n");


  // Determining the Audio Codec
  WriteRegShort(PCIE_PIO | (NAMBA_PCMV), 0x8000);
  if ((ReadRegShort(PCIE_PIO | (NAMBA_PCMV))) != 0x8000) {
    panic("Audio Init failed 1.");
    return;
  }
  // Reading the Audio Codec Vendor ID
  uint32 vendorID1 = ReadRegShort(PCIE_PIO | (NAMBA_PCVID1));
  uint32 vendorID2 = ReadRegShort(PCIE_PIO | (NAMBA_PCVID2));
  // Programming the PCI Audio Subsystem ID
  uint32 vendorID = (vendorID2 << 16) + vendorID1;
  write_pci_config_int(bus, slot, func, PCI_CONFIG_SPACE_SID_SVID, vendorID);

  printf("vendorID:%x\n", vendorID);

  //init base register
  uint64 base = (uint64)descriTable ;
  WriteRegInt(PCIE_PIO | (PO_BDBAR), (uint32)((base) & 0xffffffff));
  printf("%x\n", ReadRegInt(PCIE_PIO | (PO_BDBAR)) - 0x80000000L);

  // test();
}

uchar temp[DMA_BUF_SIZE*DMA_BUF_NUM];
void test()
{
  struct spinlock t;
  initlock(&t, "sud");
  setSoundSampleRate(44100);
  //while(1)
  {
  acquire(&t);
  for (int j = 1; j < DMA_BUF_SIZE*DMA_BUF_NUM; j++)
  {
      temp[j] = (temp[j-1]+1)%256;
  }
  for (int i = 0; i < DMA_BUF_NUM; i++)
    {
        descriTable[i].buf = (uint64)(temp + i * DMA_BUF_SIZE);
        descriTable[i].cmd_len = 0x80000000 + DMA_SMP_NUM;
    }
    
        //init last valid index
        WriteRegByte(PCIE_PIO | (PO_LVI), 0x1F);
        //init control register
        //run audio
        //enable interrupt
        WriteRegByte(PCIE_PIO | (PO_CR), (0x05) | (1 << 3));
        // printf("play");
    
    release(&t);
  }
  // while (1) {
  //   for(int i=0;i<3000000;i++);
  //   printf("CVI: %x\n", ReadRegByte(PCIE_PIO | (nabmba + 0x14)));
  // }
}

void soundinit(void) {
  // scan the pci configuration space
  FOR(bus, 0, 1) FOR(slot, 0, 31) FOR(func, 0, 7) {
    uint32 res = read_pci_config_int(bus, slot, func, 0);
    uint32 vendor = res & 0xffff;
    uint32 device = (res >> 16) & 0xffff;
    if (vendor == 0x8086 && device == 0x2415) {
      printf("AC97 found\n");
      soundcard_init(bus, slot, func);
      return;
    }
  }
  printf("AC97 NOT FOUND\n");
}

void setSoundSampleRate(uint samplerate)
{
    //Control Register --> 0x00
    //pause audio
    //disable interrupt
    WriteRegByte(PCIE_PIO | (PO_CR), 0x00);

    //PCM Front DAC Rate
    WriteRegShort(PCIE_PIO | (FRONT_DAC_RATE), samplerate & 0xFFFF);
    //PCM Surround DAC Rate
    WriteRegShort(PCIE_PIO | (SURROUND_DAC_RATE), samplerate & 0xFFFF);
    //PCM LFE DAC Rate
    WriteRegShort(PCIE_PIO | (LFE_DAC_RATE), samplerate & 0xFFFF);
}

void soundInterrupt(void)
{
  printf("soundInterrupt\n");
    int i;
    // if(soundQueue == 0)
    //   return;//临时debug写的 6.20

    acquire(&sound_lock);

    struct soundNode *node = soundQueue;
    soundQueue = node->next;

    //flag
    int flag = node->flag;
    node->flag |= PROCESSED;

    //0 sound file left
    if (soundQueue == 0)
    {
      // printf("0 Sound\n");
        if ((flag & PCM_OUT) == PCM_OUT)
        {
            ushort sr = ReadRegShort(PCIE_PIO | (PO_SR));
            WriteRegShort(PCIE_PIO | (PO_SR), sr);
        }
        else if ((flag & PCM_IN) == PCM_IN)
        {
            ushort sr = ReadRegShort(PCIE_PIO | (MC_SR));
            WriteRegShort(PCIE_PIO | (MC_SR), sr);
        }
        release(&sound_lock);
        return;
    }

    //descriptor table buffer
    for (i = 0; i < DMA_BUF_NUM; i++)
    {
      // printf("%d\n", soundQueue->data[i]);
        descriTable[i].buf = (uint64)(soundQueue->data) + i * DMA_BUF_SIZE;
        descriTable[i].cmd_len = 0x80000000 + DMA_SMP_NUM;
    }

    //play music
    if ((flag & PCM_OUT) == PCM_OUT)
    {
        ushort sr = ReadRegShort(PCIE_PIO | (PO_SR));
        WriteRegShort(PCIE_PIO | (PO_SR), sr);
        WriteRegByte(PCIE_PIO | (PO_CR), 0x05);
    }

    release(&sound_lock);
}

void playSound(void)
{
  // printf("playSound\n");
    int i;

    //遍历声卡DMA的描述符列表，初始化每一个描述符buf指向缓冲队列中第一个音乐的数据块
    //每个数据块大小: DMA_BUF_SIZE
    for (i = 0; i < DMA_BUF_NUM; i++)
    {
        descriTable[i].buf = (uint64)(soundQueue->data) + i * DMA_BUF_SIZE;
        descriTable[i].cmd_len = 0x80000000 + DMA_SMP_NUM;
    }

    //uint64 base = (uint64)descriTable;

    //开始播放: PCM_OUT
    if ((soundQueue->flag & PCM_OUT) == PCM_OUT)
    {
        //init base register
        //WriteRegInt(PCIE_PIO | (PO_BDBAR), (uint32)((uint64)(&base) & 0xffffffff));
        //init last valid index
        WriteRegByte(PCIE_PIO | (PO_LVI), 0x1F);
        //init control register
        //run audio
        //enable interrupt
        WriteRegByte(PCIE_PIO | (PO_CR), 0x05);
    }
}


//add sound-piece to the end of queue
void addSound(struct soundNode *node)
{   
  printf("Add Sound\n");
    // for(int i=0;i<DMA_BUF_NUM*DMA_BUF_SIZE;i++)
    //   if (node->data[i] != 0)
    //     printf("Non-Zero: %d\n", i);
  

    struct soundNode **ptr;

    acquire(&sound_lock);

    node->next = 0;
    for(ptr = &soundQueue; *ptr; ptr = &(*ptr)->next)
        ;
    *ptr = node;

    //node is already the first
    //play sound
    printf("soundQueue == node: %d\n", (soundQueue == node));
    if (soundQueue == node)
    {
        playSound();
    }

    release(&sound_lock);
}
