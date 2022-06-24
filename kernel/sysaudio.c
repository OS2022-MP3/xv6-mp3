#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "sleeplock.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include "audio_def.h"


// global variables
static struct soundNode audiobuf[3];
static char buf[32768];

static int datacount;
static int bufcount;
static int size;
static int ispaused = 0;

static double volume_factor = 0.5;

// lock for soundNode/Volume
struct snd {
    struct spinlock lock;
    uint tag;
};
static struct snd sndlock;


int sys_setSampleRate(void)
{
    int rate, i;
    // get the 0th parameter of the system
    if (argint(0, &rate) < 0)
        return -1;
    datacount = 0;
    bufcount = 0;
    // empty the soundNode and put it in the processed state
    for (i = 0; i < 3; i++)
    {
        memset(&audiobuf[i], 0, sizeof(struct soundNode));
        audiobuf[i].flag = PROCESSED;
    }
    // set sample rate for ac97
    setSoundSampleRate(rate);
    return 0;
}

int
transfer_data(void)
{
    // data size of soundNode
    int bufsize = DMA_BUF_NUM*DMA_BUF_SIZE;
    if (datacount == 0)
        memset(&audiobuf[bufcount], 0, sizeof(struct soundNode));
    // if the remaining size of the soundNode is bigger than the data size, 
    // write the data to the soundNode
    if (bufsize - datacount > size)
    {
        memmove(&audiobuf[bufcount].data[datacount], buf, size);
        audiobuf[bufcount].flag = PCM_OUT | PROCESSED;
        datacount += size;
    }
    else
    {
        //play after the soundNode is full
        int temp = bufsize - datacount,i;
        memmove(&audiobuf[bufcount].data[datacount], buf, temp);
        audiobuf[bufcount].flag = PCM_OUT;
        addSound(&audiobuf[bufcount]);
        int flag = 1;
        //find a soundNode that has been processed and write the remaining data to
        while(flag == 1)
        {
            for (i = 0; i < 3; ++i)
            {
                if ((audiobuf[i].flag & PROCESSED) == PROCESSED)
                {
                    memset(&audiobuf[i], 0, sizeof(struct soundNode));
                    if (bufsize > size - temp)
                    {
                        memmove(&audiobuf[i].data[0], (buf + temp), (size - temp));
                        audiobuf[i].flag = PCM_OUT | PROCESSED;
                        datacount = size - temp;
                        bufcount = i;
                        flag = -1;
                        break;
                    }
                    else
                    {
                        memmove(&audiobuf[i].data[0], (buf + temp), bufsize);
                        temp = temp + bufsize;
                        audiobuf[i].flag = PCM_OUT;
                        addSound(&audiobuf[i]);
                    }
                }
            }
        }
    }
    return 0;
}



int
sys_kwrite(void)
{
    // paused? sleep
    acquire(&sndlock.lock);
    if (ispaused == 1)
        sleep(&sndlock.tag, &sndlock.lock);
    release(&sndlock.lock);


    // read PCM data from user space
    char *buffer;
    if (argint(1, &size) < 0 || argptr(0, &buffer, size) < 0)
        return -1;
    either_copyin((void*)buf, 1, (uint64)buffer, size); // to: buf, isUserSpace: 1, from: buffer, bytes: size

    short* buf_16 = (short*)buf;
    for (int i=0;i<size/2;i++)
        buf_16[i] = (short)(buf_16[i] * volume_factor);
    transfer_data();

    return 0;
}

int
sys_pause(void)
{
    sndlock.tag = 0;
    if (ispaused == 0) {
        acquire(&sndlock.lock);
	    ispaused = 1;
        ac97_pause(ispaused);
        release(&sndlock.lock);
    }
    else {
    	acquire(&sndlock.lock);
    	ispaused = 0;
        ac97_pause(ispaused);
    	wakeup(&sndlock.tag);
    	release(&sndlock.lock);
    }
    return 0;
}


int
sys_stop_wav(void)
{
    for (int i=0;i<3;i++)
    {
        audiobuf[i].flag = 0;
        audiobuf[i].next = 0;
    }
    datacount = bufcount = ispaused = 0;

    ac97_stop();

    return 0;
}

int 
sys_set_volume(void)
{
    int volume;
    if (argint(0, &volume) < 0)
        return -1;
    if (volume < 0 || volume > 100)
        return -1;
    
    volume_factor = (double)volume / 100;

    short* buf_16 = (short*)buf;
    for (int i=0;i<size/2;i++)
        buf_16[i] = (short)(buf_16[i] * volume_factor);
    for (int j=0;j<3;j++)
    {
        short* buf_16 = (short*)audiobuf[j].data;
        for (int i=0;i<sizeof(audiobuf[j].data)/2;i++)
            buf_16[i] = (short)(buf_16[i] * volume_factor);
    }
    return 0;
}