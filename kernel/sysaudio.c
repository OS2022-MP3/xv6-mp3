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
static struct soundNode ac97_buffer[3]; // buffers contain data for AC97 DMA
static int filling_index; // now filling ac97_buffer[filling_index] with PCM data in user_buffer
static int filling_end; // [0, filling_end) of ac97_buffer[filling_index] has been filled

static char user_buffer[32768]; // buffer for user input PCM data from system call
static int user_buffer_length; // bytes of PCM data

static int is_paused = 0; // =1: paused, =0: playing
static double volume_factor = 0.5; // scale PCM wave amplitude

// mutex lock for pause
// kwrite stopped when pause audio play
struct signal_lock
{
    struct spinlock lock;
    uint tag;
};
static struct signal_lock pause_lock;

int sys_setSampleRate(void)
{
    int rate, i;
    // get the 0th parameter of the system
    if (argint(0, &rate) < 0)
        return -1;
    filling_end = 0;
    filling_index = 0;
    // empty the soundNode and put it in the processed state
    for (i = 0; i < 3; i++)
    {
        memset(&ac97_buffer[i], 0, sizeof(struct soundNode));
        ac97_buffer[i].flag = PROCESSED;
    }
    // set sample rate for ac97
    setSoundSampleRate(rate);
    return 0;
}

int transfer_data(void)
{
    // data size of soundNode
    int bufsize = DMA_BUF_NUM * DMA_BUF_SIZE;
    if (filling_end == 0)
        memset(&ac97_buffer[filling_index], 0, sizeof(struct soundNode));
    // if the remaining size of the soundNode is bigger than the data size,
    // write the data to the soundNode
    if (bufsize - filling_end > user_buffer_length)
    {
        memmove(&ac97_buffer[filling_index].data[filling_end], user_buffer, user_buffer_length);
        ac97_buffer[filling_index].flag = PCM_OUT | PROCESSED;
        filling_end += user_buffer_length;
    }
    else
    {
        // play after the soundNode is full
        int temp = bufsize - filling_end, i;
        memmove(&ac97_buffer[filling_index].data[filling_end], user_buffer, temp);
        ac97_buffer[filling_index].flag = PCM_OUT;
        addSound(&ac97_buffer[filling_index]);
        int flag = 1;
        // find a soundNode that has been processed and write the remaining data to
        // TODO: non-busy waiting
        while (flag == 1)
        {
            for (i = 0; i < 3; ++i)
            {
                if ((ac97_buffer[i].flag & PROCESSED) == PROCESSED)
                {
                    memset(&ac97_buffer[i], 0, sizeof(struct soundNode));
                    if (bufsize > user_buffer_length - temp)
                    {
                        memmove(&ac97_buffer[i].data[0], (user_buffer + temp), (user_buffer_length - temp));
                        ac97_buffer[i].flag = PCM_OUT | PROCESSED;
                        filling_end = user_buffer_length - temp;
                        filling_index = i;
                        flag = -1;
                        break;
                    }
                    else
                    {
                        memmove(&ac97_buffer[i].data[0], (user_buffer + temp), bufsize);
                        temp = temp + bufsize;
                        ac97_buffer[i].flag = PCM_OUT;
                        addSound(&ac97_buffer[i]);
                    }
                }
            }
        }
    }
    return 0;
}

int sys_kwrite(void)
{
    // paused? sleep
    acquire(&pause_lock.lock);
    if (is_paused == 1)
        sleep(&pause_lock.tag, &pause_lock.lock);
    release(&pause_lock.lock);

    // read PCM data from user space
    char *buffer;
    if (argint(1, &user_buffer_length) < 0 || argptr(0, &buffer, user_buffer_length) < 0)
        return -1;
    either_copyin((void *)user_buffer, 1, (uint64)buffer, user_buffer_length); // to: user_buffer, isUserSpace: 1, from: buffer, bytes: user_buffer_length

    // scale PCM data
    short *buf_16 = (short *)user_buffer;
    for (int i = 0; i < user_buffer_length / 2; i++)
        buf_16[i] = (short)(buf_16[i] * volume_factor);
    transfer_data();

    return 0;
}

int sys_pause(void)
{
    pause_lock.tag = 0;
    if (is_paused == 0)
    {
        acquire(&pause_lock.lock);
        is_paused = 1;
        ac97_pause(is_paused);
        release(&pause_lock.lock);
    }
    else
    {
        acquire(&pause_lock.lock);
        is_paused = 0;
        ac97_pause(is_paused);
        wakeup(&pause_lock.tag);
        release(&pause_lock.lock);
    }
    return 0;
}

int sys_stop_wav(void)
{
    for (int i = 0; i < 3; i++)
    {
        ac97_buffer[i].flag = 0;
        ac97_buffer[i].next = 0;
    }
    filling_end = filling_index = is_paused = 0;

    ac97_stop();

    return 0;
}

int sys_set_volume(void)
{
    // TODO: set volume when playing/paused
    int volume;
    if (argint(0, &volume) < 0)
        return -1;
    if (volume < 0 || volume > 100)
        return -1;

    volume_factor = (double)volume / 100;

    short *buf_16 = (short *)user_buffer;
    for (int i = 0; i < user_buffer_length / 2; i++)
        buf_16[i] = (short)(buf_16[i] * volume_factor);
    for (int j = 0; j < 3; j++)
    {
        short *buf_16 = (short *)ac97_buffer[j].data;
        for (int i = 0; i < sizeof(ac97_buffer[j].data) / 2; i++)
            buf_16[i] = (short)(buf_16[i] * volume_factor);
    }
    return 0;
}