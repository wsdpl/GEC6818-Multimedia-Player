#include "player.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* =====================================================================
 * 统一播放器封装 player.c（mplayer slave 模式）
 * 用两条管道分别连接 mplayer 的 stdin（发命令）与 stdout（读响应）。
 * 命令格式（末尾必须带换行）：
 *   pause
 *   seek <秒> 0   相对跳转（0=相对秒）
 *   seek <秒> 2   绝对跳转（2=绝对秒）
 *   volume <0~100> 1   绝对音量
 *   get_time_pos          回答 ANS_TIME_POSITION=<当前秒>
 *   get_property length   回答 ANS_length=<总时长秒>
 * ===================================================================== */

static void player_send(Player *p, const char *cmd)
{
	ssize_t written;

	if (p->in_fd < 0 || cmd == NULL)
		return;

	written = write(p->in_fd, cmd, strlen(cmd));
	/* mplayer 已退出时管道读端关闭，write 返回 EPIPE（SIGPIPE 已在
	 * player_open 中忽略）。此时关掉写端，避免后续反复无效写入；
	 * 进程的回收交给 player_has_exited() 的 waitpid。 */
	if (written < 0 && errno == EPIPE)
	{
		close(p->in_fd);
		p->in_fd = -1;
	}
}

/* 解析一行 mplayer 输出，提取当前位置 / 总时长 */
static void player_parse_line(Player *p, const char *line)
{
	const char *eq;
	double value;
	char key[64];
	int i;

	eq = strchr(line, '=');
	if (eq == NULL)
		return;

	i = 0;
	while (line < eq && i < (int)sizeof(key) - 1)
		key[i++] = (char)toupper((unsigned char)*line++);
	key[i] = '\0';

	value = atof(eq + 1);

	if (strcmp(key, "ANS_TIME_POSITION") == 0)
		p->position = value;
	else if (strcmp(key, "ANS_LENGTH") == 0 ||
		strcmp(key, "ANS_TIME_LENGTH") == 0)
		p->duration = value;
}

/* 非阻塞读出 mplayer 当前所有输出，按行解析 */
static void player_read_output(Player *p)
{
	char chunk[512];
	ssize_t n;

	if (p->out_fd < 0)
		return;

	while ((n = read(p->out_fd, chunk, sizeof(chunk))) > 0)
	{
		ssize_t i;
		for (i = 0; i < n; i++)
		{
			char ch = chunk[i];
			if (ch == '\n' || ch == '\r')
			{
				if (p->line_len > 0)
				{
					p->line_buf[p->line_len] = '\0';
					player_parse_line(p, p->line_buf);
					p->line_len = 0;
				}
			}
			else if (p->line_len < (int)sizeof(p->line_buf) - 1)
			{
				p->line_buf[p->line_len++] = ch;
			}
		}
	}
}

int player_open(Player *p, const char *full_path, int video, int video_h)
{
	int in_pipe[2], out_pipe[2];
	char height_buf[16];
	pid_t pid;

	if (p == NULL || full_path == NULL)
		return -1;

	/* 全局忽略 SIGPIPE：向已退出的 mplayer 管道写命令时，默认会收到
	 * SIGPIPE 直接终止整个程序（例如歌曲末尾连续点快进/暂停）。
	 * 忽略后 write 只会返回 -1(EPIPE)，不再崩溃。 */
	{
		static int sigpipe_ignored = 0;
		if (!sigpipe_ignored)
		{
			signal(SIGPIPE, SIG_IGN);
			sigpipe_ignored = 1;
		}
	}

	memset(p, 0, sizeof(*p));
	p->pid = -1;
	p->in_fd = -1;
	p->out_fd = -1;
	p->is_video = video;
	p->paused = 0;
	p->position = 0.0;
	p->duration = 0.0;

	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
	{
		perror("创建播放器管道失败");
		return -1;
	}

	pid = fork();
	if (pid < 0)
	{
		perror("创建mplayer进程失败");
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		return -1;
	}

	if (pid == 0)
	{
		/* 子进程：管道接到标准输入/输出 */
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		dup2(out_pipe[1], STDERR_FILENO);
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);

		if (video)
		{
			snprintf(height_buf, sizeof(height_buf), "%d", video_h);
			/* 画面固定在屏幕上方 video_h 高，底部留出信息区/控制栏 */
			execlp("mplayer", "mplayer", "-slave", "-quiet",
				"-geometry", "0:0", "-zoom", "-x", "800", "-y", height_buf,
				"-nojoystick", "-nolirc", "-noconsolecontrols",
				full_path, (char *)NULL);
		}
		else
		{
			/* 纯音频：-vo null 不输出画面，不影响自绘界面 */
			execlp("mplayer", "mplayer", "-slave", "-quiet",
				"-vo", "null",
				"-nojoystick", "-nolirc", "-noconsolecontrols",
				full_path, (char *)NULL);
		}

		perror("启动mplayer失败");
		_exit(127);
	}

	/* 父进程：保留写命令、读响应两端 */
	close(in_pipe[0]);
	close(out_pipe[1]);
	p->in_fd = in_pipe[1];
	p->out_fd = out_pipe[0];
	p->pid = pid;

	/* 响应读取设为非阻塞，没有数据时立即返回 */
	fcntl(p->out_fd, F_SETFL, O_NONBLOCK);

	/* 排空启动时的版权/格式信息 */
	usleep(150000);
	player_read_output(p);
	return 0;
}

void player_close(Player *p)
{
	if (p == NULL)
		return;

	if (p->pid > 0)
	{
		if (kill(p->pid, SIGKILL) == -1 && errno != ESRCH)
			perror("停止mplayer失败");
		while (waitpid(p->pid, NULL, 0) == -1 && errno == EINTR)
		{
		}
	}

	if (p->in_fd >= 0)
		close(p->in_fd);
	if (p->out_fd >= 0)
		close(p->out_fd);

	p->pid = -1;
	p->in_fd = -1;
	p->out_fd = -1;
}

void player_toggle_pause(Player *p)
{
	if (p == NULL || p->pid < 0)
		return;
	player_send(p, "pause\n");
	p->paused = !p->paused;
}

void player_seek_relative(Player *p, double seconds)
{
	char cmd[64];
	if (p == NULL || p->pid < 0)
		return;
	snprintf(cmd, sizeof(cmd), "seek %+.2f 0\n", seconds);
	player_send(p, cmd);
}

void player_seek_absolute(Player *p, double seconds)
{
	char cmd[64];
	if (p == NULL || p->pid < 0)
		return;
	if (seconds < 0.0)
		seconds = 0.0;
	snprintf(cmd, sizeof(cmd), "seek %.2f 2\n", seconds);
	player_send(p, cmd);
	/* 立即更新本地位置，避免等待 mplayer 回包造成进度条停顿 */
	p->position = seconds;
}

void player_set_volume(Player *p, int percent)
{
	char cmd[64];
	if (p == NULL || p->pid < 0)
		return;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	snprintf(cmd, sizeof(cmd), "volume %d 1\n", percent);
	player_send(p, cmd);
}

void player_poll(Player *p)
{
	if (p == NULL || p->pid < 0)
		return;

	/* 暂停时仍查询位置（seek 后需要更新），但时间不再增长也无妨 */
	player_send(p, "get_time_pos\n");
	player_send(p, "get_property length\n");
	usleep(20000);
	player_read_output(p);
}

int player_has_exited(Player *p)
{
	if (p == NULL || p->pid < 0)
		return 1;

	if (waitpid(p->pid, NULL, WNOHANG) == p->pid)
	{
		p->pid = -1;
		return 1;
	}
	return 0;
}
