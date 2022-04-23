#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

/*
 * Reference to Intel doc AC97
 */
#define PCI_CONFIG_SPACE_STS_CMD (0x4 / 4)
#define PCI_CONFIG_SPACE_NAMBAR (0x10 / 4)
#define PCI_CONFIG_SPACE_NABMBAR (0x14 / 4)
#define PCI_CONFIG_SPACE_SID_SVID (0x2C / 4)
#define PCI_CONFIG_SPACE_INT_PN_LN (0x3C / 4)

uint64 SOUND_NAMBAR_DATA;
#define NAMBAR_PCMV SOUND_NAMBAR_DATA + 0x2
#define NAMBAR_PCVID1 SOUND_NAMBAR_DATA + 0x7C
#define NAMBAR_PCVID2 SOUND_NAMBAR_DATA + 0x7E

uint64 SOUND_NABMBAR_DATA;
#define NABMBAR_GLOB_CNT SOUND_NABMBAR_DATA + 0x2C
#define NABMBAR_GLOB_STA SOUND_NABMBAR_DATA + 0x30
#define FRONT_DAC_RATE SOUND_NAMBAR_DATA + 0x2C
#define SURROUND_DAC_RATE SOUND_NAMBAR_DATA + 0x2E
#define LFE_DAC_RATE SOUND_NAMBAR_DATA + 0x30
#define PO_BDBAR SOUND_NABMBAR_DATA + 0x10//PCM Out Buffer Descriptor list Base Address Register 
#define PO_LVI SOUND_NABMBAR_DATA + 0x15//PCM Out Last Valid Index 
#define PO_SR SOUND_NABMBAR_DATA + 0x16//PCM Out Status Register
#define PO_CR SOUND_NABMBAR_DATA + 0x1B //PCM Out Control Register
#define MC_BDBAR SOUND_NABMBAR_DATA + 0x20//Mic. In Buffer Descriptor list Base Address Register
#define MC_LVI SOUND_NABMBAR_DATA + 0x25//Mic. In Last Valid Index
#define MC_SR SOUND_NABMBAR_DATA + 0x26//Mic. In Status Register
#define MC_CR SOUND_NABMBAR_DATA + 0x2B//Mic. In Control Register

static struct spinlock AC97_Lock;

void AC97_init(volatile uint32 *base)
{
    // TODO: after pci_init
    volatile uint32 *uint32_addr, tmp;
    uint8 *uint8_addr;
    uint16 *uint16_addr;
    uint32 vendorID;
	uint16 vendorID1, vendorID2;
	
	//Initailize Interruption
	initlock(&AC97_Lock, "AC97");

    //Initializing the Audio I/O Space
    uint32_addr = base + PCI_CONFIG_SPACE_STS_CMD;
    tmp = uint32_addr[0];
    uint32_addr[0] = tmp | 0x7;
    printf("%x\n", uint32_addr[0]);

    uint32_addr = (base + PCI_CONFIG_SPACE_NAMBAR);
    uint32_addr[0] = 0x35000000;
    uint32_addr = (base + PCI_CONFIG_SPACE_NABMBAR);
    uint32_addr[0] = 0x36000000;

    SOUND_NAMBAR_DATA = (base + PCI_CONFIG_SPACE_NAMBAR)[0];
	SOUND_NABMBAR_DATA = (base + PCI_CONFIG_SPACE_NABMBAR)[0];
    printf("AC97 I/O Space initialized successfully!\n");

	//Removing AC_RESET
    uint8_addr = (uint8 *)NABMBAR_GLOB_CNT;
    printf("%x\n", uint8_addr[0]);
    uint8_addr[0] = (uint8)0X2;
    printf("%x\n", uint8_addr[0]);
	printf("AC_RESET removed successfully!\n");

    //Reading Codec Ready Status
	printf("Waiting for Codec Ready Status...\n");
    uint16_addr = (uint16 *)NABMBAR_GLOB_STA;
    printf("%x\n", uint16_addr[0]);
	while (!(uint16_addr[0] & 0X100))
		;
	printf("Codec is ready!\n");

    //Determine Audio Codec
	tmp = ((uint16 *)NAMBAR_PCMV)[0];
	printf("%x\n", tmp);
    ((uint16 *)NAMBAR_PCMV)[0] = (uint16)0x8000;
    printf("%x\n", ((uint16 *)NAMBAR_PCMV)[0]);
	if (((uint16 *)NAMBAR_PCMV)[0] != 0x8000)
	{
		printf("Audio Codec Function not found!\n");
		return;
	}
    ((uint16 *)NAMBAR_PCMV)[0] = (uint16)tmp;
	printf("Audio Codec Function is found, current volume is %x.\n", tmp);

    //Reading the Audio Codec Vendor ID
	vendorID1 = ((uint16 *)NAMBAR_PCVID1)[0];
	vendorID2 = ((uint16 *)NAMBAR_PCVID2)[0];
	printf("Audio Codec Vendor ID read successfully!\n");
	
	//Programming the PCI Audio Subsystem ID
	vendorID = (vendorID2 << 16) + vendorID1;
    uint32_addr = (base + PCI_CONFIG_SPACE_SID_SVID);
    uint32_addr[0] = vendorID;
}