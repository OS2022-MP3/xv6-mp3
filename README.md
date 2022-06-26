# xv6-riscv音频播放

> 2022年软件学院操作系统大作业 基于xv6的音频播放
>
> 小组成员：和嘉晅、何承昱、徐浩博、马越洲

支持MP3/WAV/FLAC格式音频的播放/暂停/恢复/停止/调节音量。

### 运行环境

* Ubuntu 22.04
  * Windows平台请使用支持音频的虚拟机（如VMware），WSL不支持原生的音频播放
* QEMU 6.2.0

### 运行指南

* 配置环境

```shell
sudo apt-get update && sudo apt-get upgrade
sudo apt-get install git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu 
```

* 运行xv6：`make qemu`

* （在xv6中）启动音频播放器：`player`

```shell
$ player
Welcome to the music player!
Local music list:
class.mp3      long5.wav      1.mp3          summer.mp3     
1minute.mp3    novia.mp3      15.mp3         haoyunlai.mp3  
bgm.flac       
Enter Command:
```

* `player`指令
  - `play filename`：播放音乐
  - `pause`：暂停播放（可回复）
  - `resume`：恢复播放
  - `stop`：停止播放（不可恢复）
  - `volume {int 0~100}`：调节音量（默认值50）
  - `list`：显示可以播放的音频列表
  - `exit`：退出音频播放器
* 退出QEMU：`ctrl+a`然后`x`

* 添加音频文件
  * 将新的音频文件放在：`./audio/`下
  * 修改`Makefile`中的`AUDIOS`项，将文件添加到虚拟硬盘中
  * 若`fs.img`存在：`rm fs.img`
  * `make qemu`

### 技术细节

请参考`doc/`文件夹下的实验报告。

