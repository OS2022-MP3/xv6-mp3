#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/audio_def.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int i;
  int fd;
  struct wav info;

  fd = open(argv[1], O_RDWR);
  if (fd < 0)
  {
    printf("open wav file fail\n");
    exit(0);
  }

  read(fd, &info, sizeof(struct wav));
  if ((info.riff_id != 0x46464952)||(info.wave_id != 0x45564157)) {
    printf("invalid file format\n");
    close(fd);
    exit(0);
  }
  // printf("encode conditions: %d %d %d %d\n", (info.info.id != 0x20746d66),
  //     (info.info.channel != 0x0002),
  //     (info.info.bytes_per_sample != 0x0004),
  //     (info.info.bits_per_sample != 0x0010));
  printf("%d\n", info.info.bytes_per_sample);

  // if ((info.info.id != 0x20746d66)||
  //     (info.info.channel != 0x0002)||
  //     (info.info.bytes_per_sample != 0x0004)||
  //     (info.info.bits_per_sample != 0x0010)) {
  //   printf("data encoded in an unaccepted way\n");
  //   close(fd);
  //   exit(0);
  // }

  int pid = fork();
  if (pid == 0) {
	exec("sh",argv);
  }
  printf("Sample Rate: %d\n", info.info.sample_rate);
  setSampleRate(info.info.sample_rate);
  uint rd = 0;
  char buf[512];
  //char *tmp[2] = {"w","w"};
  //int mp3pid = fork();
  //if (mp3pid == 0) {
  //    exec("decode", tmp);
  //}

  // 51-60为调试时新加入
  int decodepid = fork();
  {
    if(decodepid == 0)
    {
      while(1)
        wavdecode();
      exit(0);
    }
  }


  while (rd < info.dlen)
  {
    int len = (info.dlen - rd < 512 ? info.dlen -rd : 512);
    read(fd, buf, len);
    kwrite(buf, len);
    rd += len;
  }

  memset(buf, 0, 512);
  for (i = 0; i < DMA_BUF_NUM*DMA_BUF_SIZE / 512 + 1; i++)
  {
    kwrite(buf, 512);
  }

  close(fd);
  kill(pid);
  //kill(mp3pid);
  wait(0);
  wait(0);
  exit(0);
}