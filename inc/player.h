#ifndef __PLAYER_H_
#define __PLAYER_H_

#include <sys/types.h>

/* =====================================================================
 * 统一播放器封装 player.h（基于 mplayer 的 slave 模式）
 * 音乐、视频共用：
 *  - 视频：画面输出到 framebuffer 屏幕上方 video_h 像素高区域
 *  - 音乐：-vo null 纯音频输出，不占用屏幕，界面照常自绘
 * 通过管道向 mplayer 发送 slave 命令，实现暂停、快进/快退、进度拖拽、
 * 音量调节，并能查询当前播放位置与总时长。
 * ===================================================================== */

typedef struct
{
	pid_t pid;         /* mplayer 进程号，-1 表示未运行 */
	int in_fd;         /* 父进程写命令（连到 mplayer 的 stdin） */
	int out_fd;        /* 父进程读响应（连到 mplayer 的 stdout） */
	int is_video;      /* 1=视频画面，0=纯音频 */
	int paused;        /* 本端维护的暂停状态 */
	double position;   /* 当前播放位置（秒） */
	double duration;   /* 总时长（秒） */
	char line_buf[512];/* 拼接 mplayer 输出的半行 */
	int line_len;
} Player;

/* 打开播放器。video=1 时画面固定在屏幕上方 video_h 像素高；video=0 纯音频。成功返回0 */
int player_open(Player *p, const char *full_path, int video, int video_h);
/* 停止并回收播放器 */
void player_close(Player *p);
/* 暂停 / 继续切换 */
void player_toggle_pause(Player *p);
/* 相对快进/快退（秒，正为前进，负为后退） */
void player_seek_relative(Player *p, double seconds);
/* 跳转到绝对位置（秒），用于进度条拖拽 */
void player_seek_absolute(Player *p, double seconds);
/* 设置绝对音量（0~100） */
void player_set_volume(Player *p, int percent);
/* 主循环定时调用：查询并刷新 position/duration */
void player_poll(Player *p);
/* 播放器是否已退出（如单曲自然播完）：1=已退出，0=运行中 */
int player_has_exited(Player *p);

#endif
