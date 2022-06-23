#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
show_audioList()
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    char path[] = ".";

    if((fd = open(path, 0)) < 0){
        fprintf(2, "player: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0){
        fprintf(2, "player: cannot stat %s\n", path);
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        char *extensionname, *name;
        char tmp[] = " ";
        name = buf;
        if(strlen(name)>4)
            extensionname = name + strlen(name) - 4;
        else
            extensionname = tmp;
        if(strcmp(extensionname,".mp3")==0  || strcmp(extensionname,".wav")==0 )
            printf("%s\n", fmtname(name));
    }
    close(fd);
}

int
main(void)
{
    //static char buf[100];
    //int fd;

    printf("Welcome to the music player!\n");
    printf("Local music list:\n");
    show_audioList();
    exit(0);
}