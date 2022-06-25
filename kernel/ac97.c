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

uint16 namba;  // native audio mixer base address
uint16 nabmba; // native audio bus mastering base address

#define NAMBA_PCMV namba + 0x2
#define NAMBA_PCVID1 namba + 0x7C
#define NAMBA_PCVID2 namba + 0x7E
#define FRONT_DAC_RATE namba + 0x2C
#define SURROUND_DAC_RATE namba + 0x2E
#define LFE_DAC_RATE namba + 0x30

#define MASTER_VOLUME namba + 0x02
#define PCM_OUT_VOLUME namba + 0x18

#define NABMBA_GLOB_CNT nabmba + 0x2C
#define NABMBA_GLOB_STA nabmba + 0x30
#define PO_BDBAR nabmba + 0x10 // PCM Out Buffer Descriptor list Base Address Register
#define PO_LVI nabmba + 0x15   // PCM Out Last Valid Index
#define PO_SR nabmba + 0x16    // PCM Out Status Register
#define PO_CR nabmba + 0x1B    // PCM Out Control Register

#define FOR(i, a, b) for (uint32 i = (a), i##_END_ = (b); i <= i##_END_; ++i)

// all registers address can be found in https://wiki.osdev.org/AC97

static struct spinlock sound_lock;
static struct soundNode *soundQueue;

struct descriptor
{
    uint32 buf;
    uint cmd_len;
};

static struct descriptor descriTable[DMA_BUF_NUM];

volatile uint8 *RegByte(uint64 reg) { return (volatile uchar *)(reg); }
volatile uint16 *RegShort(uint64 reg) { return (volatile ushort *)(reg); }
volatile uint32 *RegInt(uint64 reg) { return (volatile uint32 *)(reg); }

uint8 ReadRegByte(uint64 reg) { return *RegByte(reg); }
void WriteRegByte(uint64 reg, uchar v) { *RegByte(reg) = v; }
uint16 ReadRegShort(uint64 reg) { return *RegShort(reg); }
void WriteRegShort(uint64 reg, ushort v) { *RegShort(reg) = v; }
uint32 ReadRegInt(uint64 reg) { return *RegInt(reg); }
void WriteRegInt(uint64 reg, uint32 v) { *RegInt(reg) = v; }

// Ecam: PCI Configuration Space
uint32 read_pci_config_int(uint32 bus, uint32 slot, uint32 func, uint32 offset)
{
    return ReadRegInt((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM);
}

uint32 read_pci_config_byte(uint32 bus, uint32 slot, uint32 func, uint32 offset)
{
    return ReadRegByte((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM);
}

void write_pci_config_byte(uint32 bus, uint32 slot, uint32 func, uint32 offset, uchar val)
{
    WriteRegByte((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM, val);
}

void write_pci_config_int(uint32 bus, uint32 slot, uint32 func, uint32 offset, uint32 val)
{
    WriteRegInt((bus << 20) | (slot << 15) | (func << 12) | (offset) | PCIE_ECAM, val);
}

void soundcard_init(uint32 bus, uint32 slot, uint32 func)
{
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
    // write_pci_config_byte(bus, slot, func, 0x3c, PCI_IRQ); no effect !!!

    // Removing AC_RESET#
    WriteRegByte(PCIE_PIO | (NABMBA_GLOB_CNT), 0x2);

    // Check until codec ready
    uint32 wait_time = 1000;
    while (!(ReadRegShort(PCIE_PIO | (NABMBA_GLOB_STA)) & (0x100)) && wait_time)
    {
        --wait_time;
    }
    if (!wait_time)
    {
        panic("Audio Init failed 0.");
        return;
    }
    printf("Codec is ready!\n");

    // Determining the Audio Codec
    WriteRegShort(PCIE_PIO | (NAMBA_PCMV), 0x8000);
    if ((ReadRegShort(PCIE_PIO | (NAMBA_PCMV))) != 0x8000)
    {
        panic("Audio Init failed 1.");
        return;
    }
    // Reading the Audio Codec Vendor ID
    uint32 vendorID1 = ReadRegShort(PCIE_PIO | (NAMBA_PCVID1));
    uint32 vendorID2 = ReadRegShort(PCIE_PIO | (NAMBA_PCVID2));
    // Programming the PCI Audio Subsystem ID
    uint32 vendorID = (vendorID2 << 16) + vendorID1;
    write_pci_config_int(bus, slot, func, PCI_CONFIG_SPACE_SID_SVID, vendorID);

    // printf("vendorID:%x\n", vendorID);

    // init base register
    uint64 base = (uint64)descriTable;
    WriteRegInt(PCIE_PIO | (PO_BDBAR), (uint32)((base)&0xffffffff));
    // printf("%x\n", ReadRegInt(PCIE_PIO | (PO_BDBAR)) - 0x80000000L);

    // WriteRegShort(PCIE_PIO | (PCM_OUT_VOLUME), 0);
}

void soundinit(void)
{
    // scan the pci configuration space
    FOR(bus, 0, 1)
    FOR(slot, 0, 31)
    FOR(func, 0, 7)
    {
        uint32 res = read_pci_config_int(bus, slot, func, 0);
        uint32 vendor = res & 0xffff;
        uint32 device = (res >> 16) & 0xffff;
        if (vendor == 0x8086 && device == 0x2415)
        {
            // printf("AC97 found!\n");
            soundcard_init(bus, slot, func);
            return;
        }
    }
    printf("AC97 NOT FOUND!\n");
}

void setSoundSampleRate(uint samplerate)
{
    // Control Register --> 0x00
    // pause audio
    // disable interrupt
    WriteRegByte(PCIE_PIO | (PO_CR), 0x00);

    // PCM Front DAC Rate
    WriteRegShort(PCIE_PIO | (FRONT_DAC_RATE), samplerate & 0xFFFF);
    // PCM Surround DAC Rate
    WriteRegShort(PCIE_PIO | (SURROUND_DAC_RATE), samplerate & 0xFFFF);
    // PCM LFE DAC Rate
    WriteRegShort(PCIE_PIO | (LFE_DAC_RATE), samplerate & 0xFFFF);
}

void soundInterrupt(void)
{
    int i;

    acquire(&sound_lock);

    struct soundNode *node = soundQueue;
    soundQueue = node->next;

    // flag
    int flag = node->flag;
    node->flag |= PROCESSED;

    // 0 sound file left
    if (soundQueue == 0)
    {
        ushort sr = ReadRegShort(PCIE_PIO | (PO_SR));
        WriteRegShort(PCIE_PIO | (PO_SR), sr);
        release(&sound_lock);
        return;
    }

    // descriptor table buffer
    for (i = 0; i < DMA_BUF_NUM; i++)
    {
        // printf("%d\n", soundQueue->data[i]);
        descriTable[i].buf = (uint64)(soundQueue->data) + i * DMA_BUF_SIZE;
        descriTable[i].cmd_len = 0x80000000 + DMA_SMP_NUM;
    }

    // play music
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

    // Traverse the descriptor list of DMA,
    // and initialize each buffer descriptor to point to the first audio data block in the buffer list
    // size of each data block: DMA_BUF_SIZE
    for (i = 0; i < DMA_BUF_NUM; i++)
    {
        descriTable[i].buf = (uint64)(soundQueue->data) + i * DMA_BUF_SIZE;
        descriTable[i].cmd_len = 0x80000000 + DMA_SMP_NUM;
    }

    // begin play: PCM_OUT
    if ((soundQueue->flag & PCM_OUT) == PCM_OUT)
    {
        // init last valid index
        WriteRegByte(PCIE_PIO | (PO_LVI), 0x1F);
        // init control register
        // run audio
        // enable interrupt
        WriteRegByte(PCIE_PIO | (PO_CR), 0x05);
    }
}

// add sound-piece to the end of queue
void addSound(struct soundNode *node)
{
    struct soundNode **ptr;

    acquire(&sound_lock);

    node->next = 0;
    for (ptr = &soundQueue; *ptr; ptr = &(*ptr)->next)
        ;
    *ptr = node;

    // node is already the first
    // play sound
    if (soundQueue == node)
        playSound();

    release(&sound_lock);
}

void ac97_pause(int isPaused)
{
    if (isPaused == 1)
        WriteRegByte(PCIE_PIO | (PO_CR), 0);
    else
        WriteRegByte(PCIE_PIO | (PO_CR), 0x05);
}

void ac97_stop()
{
    soundQueue = 0;

    WriteRegByte(PCIE_PIO | (PO_CR), 0);
    while (ReadRegByte(PCIE_PIO | (PO_CR)) != 0)
        ;
    WriteRegByte(PCIE_PIO | (PO_CR), 0x02);
    while (ReadRegByte(PCIE_PIO | (PO_CR)) != 0)
        ;

    memset(descriTable, 0, sizeof(descriTable));
    uint64 base = (uint64)descriTable;
    WriteRegInt(PCIE_PIO | (PO_BDBAR), (uint32)((base)&0xffffffff));
}