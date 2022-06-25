/* SPDX-License-Identifier: 0BSD */
#define MINIFLAC_IMPLEMENTATION
#include "flac.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
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
typedef struct {
    uint8_t* buffer;
    uint32_t pos;
    int len;
} membuffer_t;

int main(int argc, const char* argv[]) {
    MINIFLAC_RESULT res;
    unsigned int i = 0;
    uint32_t used = 0;
    uint32_t sampSize = 0;
    uint8_t shift = 0;
    uint32_t len = 0;
    miniflac_t* decoder = NULL;
    int32_t** samples = NULL;
    uint8_t* outSamples = NULL;

    membuffer_t mem;

    if(argc < 2) {
        printf("incorrect input\n");
        exit(0);
    }    
    mem.buffer = (uint8_t*)getFileBuffer(argv[1],&mem.len);
    mem.pos = 0;
    decoder = malloc(miniflac_size());

    samples = (int32_t **)malloc(sizeof(int32_t *) * 8);
    for(i=0;i<8;i++) {
        samples[i] = (int32_t *)malloc(sizeof(int32_t) * 65535);
    }
    outSamples = (uint8_t*)malloc(sizeof(int32_t) * 8 * 65535);

    miniflac_init(decoder, MINIFLAC_CONTAINER_UNKNOWN);
    if(miniflac_sync(decoder,&mem.buffer[mem.pos],mem.len,&used) != MINIFLAC_OK) printf("err");
    mem.len -= used;
    mem.pos += used;
    
    setSampleRate(44100);
    while(decoder->state == MINIFLAC_METADATA) {
    /*     printf("metadata block: type: %u, is_last: %u, length: %u\n",
          decoder->metadata.header.type_raw,
          decoder->metadata.header.is_last,
          decoder->metadata.header.length);
         */
        if(miniflac_sync(decoder,&mem.buffer[mem.pos],mem.len,&used) != MINIFLAC_OK) printf("err");
        mem.len -= used;
        mem.pos += used;
    }

    while( (res = miniflac_decode(decoder,&mem.buffer[mem.pos],mem.len,&used,samples)) == MINIFLAC_OK) {
        mem.len -= used;
        mem.pos += used;
        len = 0;
        sampSize = 0;
        packer pack = NULL;
        shift = 0;

        if(decoder->frame.header.bps <= 8) {
            sampSize = 1; pack = uint8_packer; shift = 8 - decoder->frame.header.bps;
        } else if(decoder->frame.header.bps <= 16) {
            sampSize = 2; pack = int16_packer; shift = 16 - decoder->frame.header.bps;
        } else if(decoder->frame.header.bps <= 24) {
            sampSize = 3; pack = int24_packer; shift = 24 - decoder->frame.header.bps;
        } else if(decoder->frame.header.bps <= 32) {
            sampSize = 4; pack = int32_packer; shift = 32 - decoder->frame.header.bps;
        } else  {
            //abort();
        }

        len = sampSize * decoder->frame.header.channels * decoder->frame.header.block_size;

        /* samples is planar, convert into an interleaved format, and pack into little-endian */
        pack(outSamples,samples,decoder->frame.header.channels,decoder->frame.header.block_size,shift);
        kwrite(outSamples,len);

        /* sync up to the next frame boundary */
        res = miniflac_sync(decoder,&mem.buffer[mem.pos],mem.len,&used);
        mem.len -= used;
        mem.pos += used;
        if(res != MINIFLAC_OK) break;
    }

    // printf("finished\n");

    for(i=0;i<8;i++) {
        free(samples[i]);
    }
    free(samples);
    exit(0);
}