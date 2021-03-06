#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/audio_def.h"

char *get_input()
{
    static char input_buffer[100];
    gets(input_buffer, 100);
    char *buf = input_buffer;
    while ((*buf) != '\n')
        buf++;
    *buf = 0;
    return input_buffer;
}

char *
fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
    return buf;
}

void show_audioList()
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    int cnt = 0, pos = 0;
    char path[] = ".";

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "player: cannot open %s\n", path);
        return;
    }
    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "player: cannot stat %s\n", path);
        close(fd);
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de))
    {
        if (de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        char *extensionname, *name;
        char tmp[] = " ";
        name = buf;
        for (pos = 1; pos < strlen(name); pos++)
        {
            if (name[pos] == '.')
                break;
        }
        if (pos <= 2)
            extensionname = tmp;
        else
            extensionname = name + pos;
        if (strcmp(extensionname, ".mp3") == 0 || strcmp(extensionname, ".wav") == 0 || strcmp(extensionname, ".flac") == 0)
        {
            printf("%s ", fmtname(name));
            cnt++;
            if (cnt == 4)
            {
                cnt = 0;
                printf("\n");
            }
        }
    }
    printf("\n");
    close(fd);
}

int startswith(char *s, char *t)
{
    while (*t && *s == *t)
        s++, t++;
    return (*t == 0);
}

void play_wav(char *filename)
{
    int fd;
    struct wav info;

    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("open wav file fail\n");
        exit(0);
    }

    read(fd, &info, sizeof(struct wav));
    if ((info.riff_id != 0x46464952) || (info.wave_id != 0x45564157))
    {
        printf("invalid file format\n");
        close(fd);
        exit(0);
    }

    if ((info.info.id != 0x20746d66) ||
        (info.info.bits_per_sample != 0x0010))
    {
        printf("data encoded in an unaccepted way\n");
        close(fd);
        exit(0);
    }

    setSampleRate(info.info.sample_rate / 2 * info.info.channel); // TODO: support different channel number
    uint rd = 0;
    char buf[512];

    while (rd < info.dlen)
    {
        int len = (info.dlen - rd < 512 ? info.dlen - rd : 512);
        read(fd, buf, len);
        kwrite(buf, len);
        rd += len;
    }

    close(fd);
    exit(0);
}

void play_mp3(char *filename)
{
    char *args[] = {"mp3", filename};
    exec("mp3", args);
};

void play_flac(char *filename)
{
    char *args[] = {"flac", filename};
    exec("flac", args);
};

int main(void)
{
    printf("Welcome to the music player!\n");
    printf("Local music list:\n");
    char *input_str;
    int play_pid = -1, pos = 0;
    int isPaused = 0;
    show_audioList();
    while (1)
    {
        printf("Enter Command: ");
        input_str = get_input();
        if (startswith(input_str, "play "))
        {
            if (play_pid != -1)
            {
                pause();
                kill(play_pid);
                stop_wav();
            }
            char *extensionname, *name;
            name = input_str + 5;
            for (pos = 1; pos < strlen(name); pos++)
            {
                if (name[pos] == '.')
                    break;
            }
            extensionname = name + pos;
            play_pid = fork();
            if (play_pid == 0 && strcmp(extensionname, ".wav") == 0)
                play_wav(input_str + 5);
            else if (play_pid == 0 && strcmp(extensionname, ".mp3") == 0)
                play_mp3(input_str + 5);
            else if (play_pid == 0 && strcmp(extensionname, ".flac") == 0)
                play_flac(input_str + 5);
        }
        else if (strcmp(input_str, "stop") == 0)
        {
            pause();
            kill(play_pid);
            stop_wav();
        }
        else if (strcmp(input_str, "pause") == 0)
        {
            if (isPaused == 0)
            {
                isPaused = 1;
                pause();
            }
        }
        else if (strcmp(input_str, "resume") == 0)
        {
            if (isPaused == 1)
            {
                isPaused = 0;
                pause();
            }
        }
        else if (startswith(input_str, "volume "))
        {
            int volume = parseInt(input_str + 7);
            printf("%d\n", volume);
            set_volume(volume);
        }
        else if (startswith(input_str, "list"))
            show_audioList();
        else if (startswith(input_str, "exit"))
        {
            pause();
            kill(play_pid);
            stop_wav();
            break;
        }
    }
    exit(0);
}