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
#include "stream.h"


// global variables
static struct soundNode audiobuf[3];
static char buf[32768];

static int datacount;
static int bufcount;
static int size;
static int isdecoding = 0;
static int ispaused = 0;

// lock for soundNode/Decode
struct snd {
    struct spinlock lock;
    uint tag;
};
struct decode {
    struct spinlock lock;
    uint nread;
    uint nwrite;
};
static struct snd sndlock;
static struct decode decodelock;


int sys_setSampleRate(void)
{
    ispaused = 0;
    // corebuf.buf_bit_idx=8;
    // corebuf.totbit=0;
    // corebuf.buf_byte_idx=0;
    int rate, i;
    // 获取系统的第0个参数
    if (argint(0, &rate) < 0)
        return -1;
    datacount = 0;
    bufcount = 0;
    // 将soundNode清空并置为已处理状态
    for (i = 0; i < 3; i++)
    {
        memset(&audiobuf[i], 0, sizeof(struct soundNode));
        audiobuf[i].flag = PROCESSED;
    }
    // audio.c设置采样率
    setSoundSampleRate(rate);
    return 0;
}

int
sys_wavdecode(void)
{
    //soundNode的数据大小
    int bufsize = DMA_BUF_NUM*DMA_BUF_SIZE;
    acquire(&decodelock.lock);
    while (isdecoding == 0)
    {
	   sleep(&decodelock.nread, &decodelock.lock);
    }
    release(&decodelock.lock);
    if (datacount == 0)
        memset(&audiobuf[bufcount], 0, sizeof(struct soundNode));
    //若soundNode的剩余大小大于数据大小，将数据写入soundNode中
    if (bufsize - datacount > size)
    {
        memmove(&audiobuf[bufcount].data[datacount], buf, size);
        audiobuf[bufcount].flag = PCM_OUT | PROCESSED;
        datacount += size;
    }
    else
    {
        int temp = bufsize - datacount,i;
        //soundNode存满后调用audioplay进行播放
    	acquire(&sndlock.lock);
    	while (ispaused == 1)
    	{
    		sleep(&sndlock.tag, &sndlock.lock);
    	}
    	release(&sndlock.lock);
        memmove(&audiobuf[bufcount].data[datacount], buf, temp);
        audiobuf[bufcount].flag = PCM_OUT;
        addSound(&audiobuf[bufcount]);
        int flag = 1;
        //寻找一个已经被处理的soundNode，将剩余数据戏写入
        while(flag == 1)
        {
            for (i = 0; i < 3; ++i)
            {
                if ((audiobuf[i].flag & PROCESSED) == PROCESSED)
                {
                    memset(&audiobuf[i], 0, sizeof(struct soundNode));
                    if (bufsize > size - temp)
                    {
                        memmove(&audiobuf[i].data[0], (buf +temp), (size-temp));
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
    acquire(&decodelock.lock);
    isdecoding = 0;
    wakeup(&decodelock.nwrite);
    release(&decodelock.lock);
    return 0;
}

int
sys_kwrite(void)
{
    char *buffer;
    //获取待播放的数据和数据大小
    acquire(&decodelock.lock);
    while (isdecoding) {
 	  sleep(&decodelock.nwrite, &decodelock.lock);
    }
    if (argint(1, &size) < 0 || argptr(0, &buffer, size) < 0)
        return -1;
    // memmove(buf, buffer, size);
    either_copyin((void*)buf, 1, (uint64)buffer, size); // to: buf, isUserSpace: 1, from: buffer, bytes: size
    isdecoding = 1;
    // printf("%d\n", ret);
    wakeup(&decodelock.nread);
    release(&decodelock.lock);
    return 0;
}

int
sys_pause(void)
{
    sndlock.tag = 0;
    if (ispaused == 0) {
	   ispaused = 1;
       ac97_pause(ispaused);
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
    memset(&sndlock, 0, sizeof(sndlock));
    memset(&decodelock, 0, sizeof(decodelock));

    for (int i=0;i<3;i++)
    {
        audiobuf[i].flag = 0;
        audiobuf[i].next = 0;
    }


    memset(buf, 0, sizeof(buf));
    datacount = bufcount = size = isdecoding = ispaused = 0;

    ac97_stop();
    printf("AC97 Reset\n");

    return 0;
}