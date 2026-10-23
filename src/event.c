#include "event.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

//全局变量定义
int event_fd;
static int ts_max_x;   //触摸X原始最大值
static int ts_max_y;   //触摸Y原始最大值

//对触摸屏初始化
void init_event(void)
{
	event_fd = open("/dev/input/event0", O_RDONLY);
	if (event_fd == -1)
	{
		printf("触摸屏打开失败\n");
		return;
	}
	//运行时读取触摸屏真实ADC范围，不再硬写1024、600
	struct input_absinfo abs_x, abs_y;
	ioctl(event_fd, EVIOCGABS(ABS_X), &abs_x);
	ioctl(event_fd, EVIOCGABS(ABS_Y), &abs_y);
	ts_max_x = abs_x.maximum;
	ts_max_y = abs_y.maximum;
	printf("触摸ADC X_MAX=%d Y_MAX=%d\n", ts_max_x, ts_max_y);
}

/* 把原始ADC坐标映射到LCD像素，并钳位到 0~799 / 0~479 */
static int clamp_x(int raw)
{
	int px = raw * 800 / ts_max_x;
	if (px >= 800) px = 799;
	if (px < 0) px = 0;
	return px;
}

static int clamp_y(int raw)
{
	int py = raw * 480 / ts_max_y;
	if (py >= 480) py = 479;
	if (py < 0) py = 0;
	return py;
}

/* 带超时地等待一次完整的"按下-松开"。
 * 返回值：1=拿到一次完整触摸（x1,y1按下点，x2,y2松开点）；
 *         0=等待按下超时（没有触摸）；-1=出错。
 * timeout_ms<0 表示永久等待。
 * 注意：检测到按下后会一直等到松开，不会在中途超时。 */
int get_pos_timeout(int *x1, int *y1, int *x2, int *y2, int timeout_ms)
{
	struct input_event buf;
	int temp_x_raw = 0, temp_y_raw = 0;
	int pressed = 0;

	while (1)
	{
		fd_set rfds;
		struct timeval tv;
		int ret;

		FD_ZERO(&rfds);
		FD_SET(event_fd, &rfds);

		if (!pressed && timeout_ms >= 0)
		{
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
			ret = select(event_fd + 1, &rfds, NULL, NULL, &tv);
		}
		else
		{
			/* 永久等待（旧 get_pos 的行为，以及按下后等松开） */
			ret = select(event_fd + 1, &rfds, NULL, NULL, NULL);
		}

		if (ret == 0)
			return 0;                 /* 空闲超时：主循环可以去刷新进度 */
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}

		if (read(event_fd, &buf, sizeof(buf)) != (ssize_t)sizeof(buf))
			continue;

		if (buf.type == EV_ABS)
		{
			if (buf.code == ABS_X)
				temp_x_raw = buf.value;
			if (buf.code == ABS_Y)
				temp_y_raw = buf.value;
		}

		/* 手指按下：记录按下点 */
		if (buf.type == EV_KEY && buf.code == BTN_TOUCH &&
			buf.value == 1 && pressed == 0)
		{
			*x1 = clamp_x(temp_x_raw);
			*y1 = clamp_y(temp_y_raw);
			pressed = 1;
		}

		/* 手指松开：记录松开点并结束本次触摸 */
		if (buf.type == EV_KEY && buf.code == BTN_TOUCH &&
			buf.value == 0)
		{
			if (!pressed)
				continue;
			*x2 = clamp_x(temp_x_raw);
			*y2 = clamp_y(temp_y_raw);
			return 1;
		}
	}
}

//获取坐标（永久阻塞等待一次完整触摸）
void get_pos(int *x1, int *y1, int *x2, int *y2)
{
	get_pos_timeout(x1, y1, x2, y2, -1);
}

void close_event(void)
{
	close(event_fd);
}
