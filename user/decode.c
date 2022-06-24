// REF: https://github.com/lieff/minimp3/blob/master/minimp3.h
//      http://tntmonks.cnblogs.com/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int_least8_t;
typedef short              int_least16_t;
typedef int                int_least32_t;
typedef long long          int_least64_t;
typedef unsigned char      uint_least8_t;
typedef unsigned short     uint_least16_t;
typedef unsigned int       uint_least32_t;
typedef unsigned long long uint_least64_t;

typedef signed char        int_fast8_t;
typedef int                int_fast16_t;
typedef int                int_fast32_t;
typedef long long          int_fast64_t;
typedef unsigned char      uint_fast8_t;
typedef unsigned int       uint_fast16_t;
typedef unsigned int       uint_fast32_t;
typedef unsigned long long uint_fast64_t;

typedef long long          intmax_t;
typedef unsigned long long uintmax_t;

#define MINIMP3_IMPLEMENTATION
#include "decode.h"


// write a wav file
void wavWrite_int16(char* filename, int16_t* buffer, int sampleRate, uint32_t totalSampleCount, int channels) {
    if(channels <= 0)
        channels = 1;
    int fp = open(filename, O_CREATE|O_WRONLY);
    if (fp < 0) {
        printf("Failed to open the file.\n");
        return;
    }
    // resize the buffer length
    totalSampleCount *= sizeof(int16_t) * channels;
    int nbit = 16;
    int FORMAT_PCM = 1;
    int nbyte = nbit / 8;

    write(fp, "RIFF", 4);
    uint32_t long_number = 36 + totalSampleCount;
    write(fp, &long_number, 4);

    write(fp, "WAVE", 4);
    write(fp, "fmt ", 4);

    long_number = 16;
    write(fp, &long_number, 4);
    int16_t short_number = FORMAT_PCM; // default format
    write(fp, &short_number, 2);
    short_number = channels; // num of channels
    write(fp, &short_number, 2);
    long_number = sampleRate; // sample rate
    write(fp, &long_number, 4);
    long_number = sampleRate * nbyte; // bits rate
    write(fp, &long_number, 4);
    short_number = nbyte; // blocks aligned
    write(fp, &short_number, 2);
    short_number = nbit; // sampling accuracy
    write(fp, &short_number, 2);
    write(fp, "data", 4);
    long_number = totalSampleCount;
    write(fp, &long_number, 4);
    write(fp, buffer, totalSampleCount);
    close(fp);
}

char* getFileBuffer(const char* fname, int* size)
{
    int fd = open(fname, O_RDONLY);
    if (fd == 0)
        return 0;

    struct stat st;
    char* file_buf = 0;
    if (fstat(fd, &st) < 0)
        goto doexit;
    file_buf = (char*)malloc(st.size + 1);
    if (file_buf != 0)
    {
        // printf("%x\n", (uint64)file_buf);
        if(read(fd, file_buf, st.size)<0)
        {
            close(fd);
            return 0;
        }
        file_buf[st.size] = 0;
    }

    if (size)
        *size = st.size;
doexit:
    close(fd);
    return file_buf;
}

// decode mp3 to PCM data
int16_t* DecodeMp3ToBuffer(char* filename, uint32_t* sampleRate, uint32_t* totalSampleCount, unsigned int* channels)
{
    setSampleRate(44100);
    int music_size = 0;
    int alloc_samples = 1024, num_samples = 0;
    int16_t* music_buf = (int16_t*)malloc(alloc_samples * 2 * 2);
    unsigned char* file_buf = (unsigned char*)getFileBuffer(filename, &music_size);
    int p = 0;
    if (file_buf == 0)
    {
        if (music_buf)
        free(music_buf);
        return 0;
    }
    unsigned char* buf = file_buf;

    // declared in "minimp3.h"
    mp3dec_frame_info_t *info=malloc(sizeof(mp3dec_frame_info_t));
    mp3dec_t *dec=malloc(sizeof(mp3dec_t ));

    mp3dec_init(dec);
    while(1)
    {
        int16_t *frame_buf=malloc(2 * 1152* sizeof(int16_t));
            // decode the PCM data of one frame (1152 for mono, 2 * 1152 for stereo)
        int samples = mp3dec_decode_frame(dec, buf, music_size, frame_buf, info);
        // num of samples
        if (alloc_samples < (num_samples + samples)) // need to expand the array which functions as a vector
        {
            alloc_samples *= 2;

            int16_t *new_buf = (int16_t *)malloc(alloc_samples * 2 * info->channels);
            if(new_buf)
            {
                memcpy(new_buf, music_buf, alloc_samples * info->channels);
                free(music_buf);
                music_buf = new_buf;
            }
        }
        if (music_buf) // add the current frame data to the total data
            memcpy(music_buf + num_samples * info->channels, frame_buf, samples * info->channels * 2);
        num_samples += samples;
        if (info->frame_bytes <= 0 || music_size <= (info->frame_bytes + 4))
            break;
        buf += info->frame_bytes;
        music_size -= info->frame_bytes;
          int i;
      
        for(i = p * info->channels; i < num_samples * info->channels; i += 256)
        {
            int len = ((num_samples - p) * info->channels * 2 < 512 )? (num_samples - p) * 2 * info->channels : 512;
            
            //printf("%d\n", len);
            kwrite((char*)(music_buf + p * info->channels), len);
            p += (len/2/info->channels);

        }
    }
    int len = (num_samples - p) * 2 * info->channels;
    kwrite((uchar*)(music_buf + p * info->channels), len);
    p += (len/2/info->channels);

    if (alloc_samples > num_samples) //shrink the data array
    {
        int16_t *new_buf = (int16_t *)malloc(num_samples * 2 * info->channels);
        if(new_buf)
        {
            memcpy(new_buf, music_buf, num_samples * 2 * info->channels);
            free(music_buf);
            music_buf = new_buf;
        }
    }

    if (sampleRate)
        *sampleRate = info->hz;
    if (channels)
        *channels = info->channels;
    if (num_samples)
        *totalSampleCount = num_samples;

    free(file_buf);
    return music_buf;
    
    
}

// get the name of input file
void splitpath(const char* path, char* drv, char* dir, char* name, char* ext)
{
    const char* end;
    const char* p;
    const char* s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    }
    else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}

int main(int argc, char* argv[])
{
    sbrk(4096 * 100);
    if (argc < 2) 
    {
        printf("Incorrect input!");
        return -1;
    }
    uint32_t totalSampleCount = 0;
    uint32_t sampleRate = 0;
    unsigned int channels = 0;
    int16_t* wavBuffer = 0;
    char drive[3];
    char *dir=malloc(256);
    char *fname=malloc(256);
    char *ext=malloc(256);
    char *out_file=malloc(1024);


    char *in_file = argv[1];
    wavBuffer = DecodeMp3ToBuffer(in_file, &sampleRate, &totalSampleCount, &channels);
    splitpath(in_file, drive, dir, fname, ext);
    //sprintf(out_file, "%s%s%s.wav", drive, dir, fname);
    
    int p = 0;
    for (int i = 0; i < strlen(drive); i++)
        out_file[p++] = drive[i];
    for (int i = 0; i < strlen(dir); i++)
        out_file[p++] = dir[i];
    for (int i = 0; i < strlen(fname); i++)
        out_file[p++] = fname[i];
    out_file[p++] = '.', out_file[p++] = 'w', out_file[p++] = 'a',
    out_file[p++] = 'v', out_file[p++] = '\0';

    // wavWrite_int16(out_file, wavBuffer, sampleRate, totalSampleCount, channels);

    if(out_file)
        free(out_file);
    if(ext)
        free(ext);
    if(dir)
        free(dir);
    if(fname)
        free(fname);
    if (wavBuffer)
        free(wavBuffer);

    // printf("Finished!\n");
    exit(0);
}