# 基于GEC6818开发板的多媒体综合播放器设计

面向 GEC6818 开发板的 Linux 多媒体播放器，使用 C 语言实现，通过 framebuffer 绘图和触摸屏输入提供相册、音乐、视频及 AOD 时钟界面。

## 功能

- 相册：BMP 缩略图网格、分页、大图浏览、收藏和删除确认。
- 音乐：MP3 播放、暂停、切歌、音量、进度控制及播放模式切换。
- 视频：AVI 播放、暂停、快进快退、进度控制及收藏。
- 收藏：统一保存完整文件路径，各模块按媒体目录显示各自的收藏。
- AOD：低亮度背景与时钟显示。

## 运行环境

- GEC6818 开发板及 Linux 系统。
- 800 × 480 显示屏，framebuffer 设备 `/dev/fb0`。
- 触摸屏设备 `/dev/input/event0`。
- 支持目标开发板的 `arm-linux-gcc` 交叉编译工具链。
- 开发板安装 `mplayer`，音乐和视频播放通过其 slave 模式控制。

## 编译与运行

在 Linux 或已配置交叉编译工具链的 WSL 环境中执行：

```sh
mkdir -p bin
make
```

将生成的 `bin/main` 复制到开发板，并准备以下资源：

```text
/mnt/udisk/
├── icons/main.bmp       # 主界面图片
├── photo/               # .bmp 照片
├── music/               # .mp3 音乐
├── video/               # .avi 视频
├── 00.bmp ... 09.bmp    # AOD 数字图片
├── 0m.bmp               # AOD 冒号图片
└── favorites.txt        # 收藏操作时生成的记录文件
```

图片解码使用未压缩的 24 位 BMP。上述媒体与界面图片未包含在本仓库，需要自行准备，并匹配代码中的路径和显示尺寸。

在开发板上执行：

```sh
chmod +x main
./main
```

运行账号需要能够访问 framebuffer、触摸设备，并能写入 U 盘收藏记录。

## 项目结构

```text
main.c                  程序入口及主界面调度
Makefile                ARM 静态编译配置
inc/                    模块头文件
src/photo.c             相册交互
src/music.c             音乐界面
src/video.c             视频界面
src/player.c            mplayer 进程与控制管道
src/media_actions.c     收藏记录及文件删除
src/lcd.c               framebuffer 与 BMP 绘制
src/ui.c                公共绘图、图标和控件
src/event.c             触摸输入
src/aod.c               AOD 时钟
tests/photo_regression.c 相册主机回归测试
```

## 相册回归测试

在 Linux 主机中执行，无需连接开发板：

```sh
gcc -g -Wall -Wextra -fsanitize=address,undefined -I inc \
  tests/photo_regression.c src/photo.c src/media_actions.c \
  src/ui.c src/font.c src/font_cn.c \
  -Wl,--wrap=fopen -o /tmp/photo_regression
ASAN_OPTIONS=detect_leaks=1 /tmp/photo_regression
```

测试使用模拟触摸与临时收藏数据，覆盖连续进入/退出收藏夹、混合媒体筛选、照片从左到右排列、空白点击以及空收藏夹，不读取或修改实际 U 盘数据。实机显示和触摸效果仍需在开发板上验证。
