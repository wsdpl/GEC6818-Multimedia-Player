#include "ui.h"
#include "lcd.h"
#include "font.h"
#include "font_cn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * 公共自绘 UI 模块 ui.c
 * 直接向 framebuffer 显存（mmap_p）写像素，相册/音乐/视频共用。
 * 纯整数运算，不依赖浮点，方便交叉编译到 ARM 开发板。
 * ===================================================================== */

/* ===================== 基础绘图原语 ===================== */

/* 画一个像素 */
void ui_put_pixel(int x, int y, uint32_t color)
{
	if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT)
		return;
	unsigned char *p = (unsigned char *)mmap_p + (y * UI_WIDTH + x) * 4;
	p[0] = (unsigned char)(color & 0xFF);
	p[1] = (unsigned char)((color >> 8) & 0xFF);
	p[2] = (unsigned char)((color >> 16) & 0xFF);
}

/* 实心矩形 */
void ui_fill_rect(int x, int y, int w, int h, uint32_t color)
{
	int row, col;
	for (row = y; row < y + h; row++)
	{
		for (col = x; col < x + w; col++)
			ui_put_pixel(col, row, color);
	}
}

/* 整屏填充 */
void ui_fill_screen(uint32_t color)
{
	ui_fill_rect(0, 0, UI_WIDTH, UI_HEIGHT, color);
}

/* 实心多边形（扫描线算法），顶点按顺序放在 points 中：x0,y0,x1,y1... */
void ui_fill_polygon(const int *points, int n, uint32_t color)
{
	int min_y = points[1], max_y = points[1];
	int i, y;
	for (i = 0; i < n; i++)
	{
		int yy = points[i * 2 + 1];
		if (yy < min_y)
			min_y = yy;
		if (yy > max_y)
			max_y = yy;
	}
	for (y = min_y; y <= max_y; y++)
	{
		int xs[20];
		int xn = 0;
		for (i = 0; i < n; i++)
		{
			int x0 = points[i * 2], y0 = points[i * 2 + 1];
			int x1 = points[((i + 1) % n) * 2], y1 = points[((i + 1) % n) * 2 + 1];
			if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y))
				xs[xn++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
		}
		/* 交点排序 */
		for (i = 0; i < xn - 1; i++)
		{
			int j;
			for (j = 0; j < xn - 1 - i; j++)
			{
				if (xs[j] > xs[j + 1])
				{
					int t = xs[j];
					xs[j] = xs[j + 1];
					xs[j + 1] = t;
				}
			}
		}
		for (i = 0; i + 1 < xn; i += 2)
		{
			if (xs[i + 1] >= xs[i])
				ui_fill_rect(xs[i], y, xs[i + 1] - xs[i] + 1, 1, color);
		}
	}
}

/* 实心圆（中点圆算法，逐行画水平扫描线，纯整数运算） */
void ui_fill_circle(int cx, int cy, int r, uint32_t color)
{
	int x = r;
	int y = 0;
	int e = 1 - r;
	if (r <= 0)
		return;
	while (x >= y)
	{
		ui_fill_rect(cx - x, cy + y, 2 * x + 1, 1, color);
		ui_fill_rect(cx - x, cy - y, 2 * x + 1, 1, color);
		ui_fill_rect(cx - y, cy + x, 2 * y + 1, 1, color);
		ui_fill_rect(cx - y, cy - x, 2 * y + 1, 1, color);
		y++;
		if (e < 0)
			e += 2 * y + 1;
		else
		{
			x--;
			e += 2 * (y - x) + 1;
		}
	}
}

/* 圆形描边（中点圆算法） */
void ui_draw_circle(int cx, int cy, int r, uint32_t color)
{
	int x = r;
	int y = 0;
	int e = 1 - r;
	if (r <= 0)
		return;
	while (x >= y)
	{
		ui_put_pixel(cx + x, cy + y, color);
		ui_put_pixel(cx + y, cy + x, color);
		ui_put_pixel(cx - y, cy + x, color);
		ui_put_pixel(cx - x, cy + y, color);
		ui_put_pixel(cx - x, cy - y, color);
		ui_put_pixel(cx - y, cy - x, color);
		ui_put_pixel(cx + y, cy - x, color);
		ui_put_pixel(cx + x, cy - y, color);
		y++;
		if (e < 0)
			e += 2 * y + 1;
		else
		{
			x--;
			e += 2 * (y - x) + 1;
		}
	}
}

/* 实心圆角矩形 */
void ui_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color)
{
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;
	if (r <= 0)
	{
		ui_fill_rect(x, y, w, h, color);
		return;
	}
	ui_fill_rect(x, y + r, w, h - 2 * r, color);
	ui_fill_rect(x + r, y, w - 2 * r, r, color);
	ui_fill_rect(x + r, y + h - r, w - 2 * r, r, color);
	ui_fill_circle(x + r, y + r, r, color);
	ui_fill_circle(x + w - 1 - r, y + r, r, color);
	ui_fill_circle(x + r, y + h - 1 - r, r, color);
	ui_fill_circle(x + w - 1 - r, y + h - 1 - r, r, color);
}

/* ===================== 通用图标 ===================== */

/* 方向箭头：dir=+1 向右，dir=-1 向左，size 为箭头总宽度 */
void ui_draw_arrow(int cx, int cy, int size, int dir, uint32_t color)
{
	int a = size / 2;
	int stem = size * 16 / 100;   /* 杆半宽 */
	int neck = size * 10 / 100;   /* 杆右端（箭头根部）偏移 */
	int head = size * 50 / 100;   /* 箭头上下半高 */
	int pts[14] = {
		cx - dir * a,      cy - stem,
		cx + dir * neck,   cy - stem,
		cx + dir * neck,   cy - head,
		cx + dir * a,      cy,
		cx + dir * neck,   cy + head,
		cx + dir * neck,   cy + stem,
		cx - dir * a,      cy + stem
	};
	ui_fill_polygon(pts, 7, color);
}

/* 实心心形：两个圆 + 一个倒三角 */
void ui_draw_heart(int cx, int cy, int size, uint32_t color)
{
	int s = size;
	int tri[6] = {
		cx - s * 50 / 100, cy,
		cx + s * 50 / 100, cy,
		cx,                cy + s * 50 / 100
	};
	ui_fill_circle(cx - s * 28 / 100, cy - s * 12 / 100, s * 27 / 100, color);
	ui_fill_circle(cx + s * 28 / 100, cy - s * 12 / 100, s * 27 / 100, color);
	ui_fill_polygon(tri, 3, color);
}

/* 垃圾桶：提手 + 盖子 + 桶身，桶内有白色竖纹 */
void ui_draw_trash(int cx, int cy, int size, uint32_t color)
{
	int s = size / 2;
	int body[8];
	/* 提手 */
	ui_fill_rect(cx - s * 2 / 5, cy - s * 9 / 10, s * 4 / 5, s * 1 / 5, color);
	/* 盖子 */
	ui_fill_rect(cx - s * 9 / 10, cy - s * 3 / 4, s * 9 / 5, s * 1 / 5, color);
	/* 桶身梯形（上宽下窄） */
	body[0] = cx - s * 7 / 10; body[1] = cy - s * 1 / 2;
	body[2] = cx + s * 7 / 10; body[3] = cy - s * 1 / 2;
	body[4] = cx + s * 1 / 2;  body[5] = cy + s * 9 / 10;
	body[6] = cx - s * 1 / 2;  body[7] = cy + s * 9 / 10;
	ui_fill_polygon(body, 4, color);
	/* 桶内白色竖纹 */
	ui_fill_rect(cx - s * 2 / 5, cy - s * 1 / 3, 3, s, UI_WHITE);
	ui_fill_rect(cx - 1,         cy - s * 1 / 3, 3, s, UI_WHITE);
	ui_fill_rect(cx + s * 2 / 5 - 2, cy - s * 1 / 3, 3, s, UI_WHITE);
}

/* 图库图标：圆角矩形图片底 + 太阳 + 山峰 */
void ui_draw_icon_photo(int cx, int cy, int size, uint32_t color, uint32_t bg)
{
	int s = size / 2;
	int m1[6] = {
		cx - s * 3 / 5, cy + s * 3 / 5,
		cx - s / 8,     cy - s / 8,
		cx + s * 3 / 4, cy + s * 3 / 5
	};
	int m2[6] = {
		cx + s / 10,    cy + s * 3 / 5,
		cx + s * 3 / 5, cy - s * 2 / 5,
		cx + s * 4 / 5, cy + s * 3 / 5
	};
	ui_fill_rounded_rect(cx - s, cy - s, size, size, 5, color);
	ui_fill_circle(cx + s * 2 / 5, cy - s * 2 / 5, s / 5, bg);
	ui_fill_polygon(m1, 3, bg);
	ui_fill_polygon(m2, 3, bg);
}

/* 收藏夹层叠图标：三层错位的圆角卡片 */
void ui_draw_icon_layers(int cx, int cy, int size, uint32_t color)
{
	int w = size * 9 / 10;
	int h = size * 3 / 10;
	ui_fill_rounded_rect(cx - w / 2 - size * 8 / 100, cy - size * 32 / 100, w, h, 4, color);
	ui_fill_rounded_rect(cx - w / 2 + size * 4 / 100, cy - size * 5 / 100, w, h, 4, color);
	ui_fill_rounded_rect(cx - w / 2 + size * 16 / 100, cy + size * 22 / 100, w, h, 4, color);
}

/* 粗对勾 */
void ui_draw_check(int cx, int cy, int size, uint32_t color)
{
	int pts[12] = {
		cx - size * 50 / 100, cy - size * 2 / 100,
		cx - size * 12 / 100, cy + size * 35 / 100,
		cx + size * 55 / 100, cy - size * 40 / 100,
		cx + size * 40 / 100, cy - size * 55 / 100,
		cx - size * 12 / 100, cy + size * 5 / 100,
		cx - size * 62 / 100, cy - size * 15 / 100
	};
	ui_fill_polygon(pts, 6, color);
}

/* 粗叉 */
void ui_draw_cross(int cx, int cy, int size, uint32_t color)
{
	int a = size * 60 / 100;
	int b = size * 11 / 100;
	int p1[8] = {
		cx - a - b, cy - a + b,
		cx - a + b, cy - a - b,
		cx + a + b, cy + a - b,
		cx + a - b, cy + a + b
	};
	int p2[8] = {
		cx + a + b, cy - a + b,
		cx + a - b, cy - a - b,
		cx - a + b, cy + a - b,
		cx - a - b, cy + a + b
	};
	ui_fill_polygon(p1, 4, color);
	ui_fill_polygon(p2, 4, color);
}

/* 白色圆底 + 浅灰描边的圆形按钮（图标随后画在上面） */
void ui_circle_button(int cx, int cy, int r)
{
	ui_fill_circle(cx, cy, r, UI_WHITE);
	ui_draw_circle(cx, cy, r, UI_FRAME);
}

/* 判断触摸点是否落在圆形按钮的外接正方形内 */
int ui_hit_circle(int touch_x, int touch_y, int cx, int cy, int r)
{
	return touch_x >= cx - r && touch_x <= cx + r &&
		touch_y >= cy - r && touch_y <= cy + r;
}

/* ===================== 音乐 / 视频图标 ===================== */

/* 音符：符头（圆）+ 符杆（竖矩形）+ 符尾（三角旗） */
void ui_draw_note(int cx, int cy, int size, uint32_t color)
{
	int s = size / 2;
	int flag[6] = {
		cx + s * 26 / 100, cy - s * 75 / 100,
		cx + s * 78 / 100, cy - s * 32 / 100,
		cx + s * 26 / 100, cy - s * 8 / 100
	};
	/* 符杆 */
	ui_fill_rect(cx + s * 10 / 100, cy - s * 75 / 100,
		s * 16 / 100, s * 125 / 100, color);
	/* 符头 */
	ui_fill_circle(cx - s * 22 / 100, cy + s * 50 / 100, s * 38 / 100, color);
	/* 符尾 */
	ui_fill_polygon(flag, 3, color);
}

/* 视频图标：圆角屏幕底 + 白色播放三角 */
void ui_draw_icon_video(int cx, int cy, int size, uint32_t color, uint32_t bg)
{
	int s = size / 2;
	int tri[6] = {
		cx - s * 28 / 100, cy - s * 42 / 100,
		cx - s * 28 / 100, cy + s * 42 / 100,
		cx + s * 48 / 100, cy
	};
	ui_fill_rounded_rect(cx - s, cy - s, size, size, 5, color);
	ui_fill_polygon(tri, 3, bg);
}

/* 播放三角 */
void ui_draw_play(int cx, int cy, int size, uint32_t color)
{
	int pts[6] = {
		cx - size * 36 / 100, cy - size * 50 / 100,
		cx - size * 36 / 100, cy + size * 50 / 100,
		cx + size * 46 / 100, cy
	};
	ui_fill_polygon(pts, 3, color);
}

/* 暂停双竖条 */
void ui_draw_pause(int cx, int cy, int size, uint32_t color)
{
	ui_fill_rect(cx - size * 32 / 100, cy - size * 45 / 100,
		size * 24 / 100, size * 90 / 100, color);
	ui_fill_rect(cx + size * 8 / 100, cy - size * 45 / 100,
		size * 24 / 100, size * 90 / 100, color);
}

/* 上一首/下一首：dir=+1 下一首(►|)，dir=-1 上一首(|◄) */
void ui_draw_skip(int cx, int cy, int size, int dir, uint32_t color)
{
	int tri[6];
	if (dir >= 0)
	{
		/* 朝右三角 + 右侧竖线 */
		tri[0] = cx - size * 42 / 100; tri[1] = cy - size * 42 / 100;
		tri[2] = cx - size * 42 / 100; tri[3] = cy + size * 42 / 100;
		tri[4] = cx + size * 14 / 100;  tri[5] = cy;
		ui_fill_polygon(tri, 3, color);
		ui_fill_rect(cx + size * 24 / 100, cy - size * 45 / 100,
			size * 14 / 100, size * 90 / 100, color);
	}
	else
	{
		/* 朝左三角 + 左侧竖线 */
		tri[0] = cx + size * 42 / 100; tri[1] = cy - size * 42 / 100;
		tri[2] = cx + size * 42 / 100; tri[3] = cy + size * 42 / 100;
		tri[4] = cx - size * 14 / 100; tri[5] = cy;
		ui_fill_polygon(tri, 3, color);
		ui_fill_rect(cx - size * 38 / 100, cy - size * 45 / 100,
			size * 14 / 100, size * 90 / 100, color);
	}
}

/* 加号 */
void ui_draw_plus(int cx, int cy, int size, uint32_t color)
{
	ui_fill_rect(cx - size * 14 / 100, cy - size * 50 / 100,
		size * 28 / 100, size, color);
	ui_fill_rect(cx - size * 50 / 100, cy - size * 14 / 100,
		size, size * 28 / 100, color);
}

/* 减号 */
void ui_draw_minus(int cx, int cy, int size, uint32_t color)
{
	ui_fill_rect(cx - size * 50 / 100, cy - size * 14 / 100,
		size, size * 28 / 100, color);
}

/* ---------- 播放模式图标（循环/单曲/随机） ---------- */

/* 粗直线（沿线段每像素画一个 thick 方块，支持水平/垂直/斜线） */
static void ui_draw_line(int x1, int y1, int x2, int y2, int thick,
	uint32_t color)
{
	int dx = abs(x2 - x1);
	int dy = abs(y2 - y1);
	int steps = dx > dy ? dx : dy;
	int half = thick / 2;
	int i;

	if (steps == 0)
	{
		ui_fill_rect(x1 - half, y1 - half, thick, thick, color);
		return;
	}

	for (i = 0; i <= steps; i++)
	{
		int x = x1 + (x2 - x1) * i / steps;
		int y = y1 + (y2 - y1) * i / steps;
		ui_fill_rect(x - half, y - half, thick, thick, color);
	}
}

/* 方向箭头（尖端在 (ex,ey)，朝向单位向量 (ux,uy)，head 长、aw 翼宽） */
static void ui_arrow_head(double ex, double ey, double ux, double uy,
	int head, int aw, uint32_t color)
{
	double px = -uy;
	double py = ux;
	double bx = ex - ux * head;
	double by = ey - uy * head;
	int pts[6] = {
		(int)(ex + 0.5), (int)(ey + 0.5),
		(int)(bx + px * aw / 2 + 0.5), (int)(by + py * aw / 2 + 0.5),
		(int)(bx - px * aw / 2 + 0.5), (int)(by - py * aw / 2 + 0.5)
	};
	ui_fill_polygon(pts, 3, color);
}

/* 列表循环：跑道形回旋带，上弧末端右箭头、下弧末端左箭头 */
void ui_draw_repeat_list(int cx, int cy, int size, uint32_t color)
{
	int half = size / 2;
	int t = size * 11 / 100;
	int hh = size * 27 / 100;
	int head = size * 30 / 100;
	int aw = size * 26 / 100;
	int lx = cx - half + size * 8 / 100;
	int rx = cx + half - size * 8 / 100;
	int top = cy - hh;
	int bot = cy + hh;

	if (t < 2)
		t = 2;

	/* 上回旋：上边从左到右（右端右箭头），左端竖线只向下到中线 */
	ui_draw_line(lx, top, rx, top, t, color);
	ui_draw_line(lx, top, lx, cy, t, color);
	/* 下回旋：下边从右到左（左端左箭头），右端竖线只向上到中线 */
	ui_draw_line(rx, bot, lx, bot, t, color);
	ui_draw_line(rx, bot, rx, cy, t, color);
	/* 右端右箭头、左端左箭头 */
	ui_arrow_head(rx + head * 0.45, top, 1.0, 0.0, head, aw, color);
	ui_arrow_head(lx - head * 0.45, bot, -1.0, 0.0, head, aw, color);
}

/* 单曲循环：列表循环 + 右上角数字 1 */
void ui_draw_repeat_one(int cx, int cy, int size, uint32_t color)
{
	int scale = 2;
	int nx = cx + size * 11 / 100;
	int ny = cy - size * 24 / 100;
	const unsigned char *glyph;
	int row, col;

	ui_draw_repeat_list(cx, cy, size, color);

	/* 用 8x16 的 '1' 放大 2 倍，盖在右上弧处 */
	glyph = font8x16['1' - FONT_FIRST];
	for (row = 0; row < FONT_H; row++)
	{
		for (col = 0; col < FONT_W; col++)
		{
			if (glyph[row] & (0x80 >> col))
				ui_fill_rect(nx + col * scale, ny + row * scale,
					scale, scale, color);
		}
	}
}

/* 随机播放：两条交叉斜线，右上、右下各一个箭头 */
void ui_draw_shuffle(int cx, int cy, int size, uint32_t color)
{
	int half = size / 2;
	int t = size * 11 / 100;
	int hh = size * 29 / 100;
	int head = size * 28 / 100;
	int aw = size * 28 / 100;
	int lx = cx - half + size * 6 / 100;
	int rx = cx + half - size * 6 / 100;
	int top = cy - hh;
	int bot = cy + hh;

	if (t < 2)
		t = 2;

	/* 左下 -> 右上，左上 -> 右下 */
	ui_draw_line(lx, bot, rx, top, t, color);
	ui_draw_line(lx, top, rx, bot, t, color);
	/* 右上箭头（朝右上）、右下箭头（朝右下） */
	ui_arrow_head(rx, top, 0.707, -0.707, head, aw, color);
	ui_arrow_head(rx, bot, 0.707, 0.707, head, aw, color);
}

/* 按当前模式绘制模式图标：顺序用灰色列表循环，其余蓝色 */
void ui_draw_mode_icon(int cx, int cy, int size, int mode)
{
	switch (mode)
	{
	case UI_MODE_LIST:
		ui_draw_repeat_list(cx, cy, size, UI_BLUE);
		break;
	case UI_MODE_REPEAT_ONE:
		ui_draw_repeat_one(cx, cy, size, UI_BLUE);
		break;
	case UI_MODE_SHUFFLE:
		ui_draw_shuffle(cx, cy, size, UI_BLUE);
		break;
	case UI_MODE_SEQUENCE:
	default:
		/* 顺序播放：列表循环图标置灰，表示未启用循环 */
		ui_draw_repeat_list(cx, cy, size, UI_FRAME);
		break;
	}
}

/* ===================== 文字（8x16 ASCII 点阵） ===================== */

/* 在 (x,y) 画一行 8x16 文字；非 ASCII 字符显示为 '?' */
void ui_draw_text(int x, int y, const char *text, uint32_t color)
{
	const unsigned char *p;

	if (text == NULL)
		return;

	for (p = (const unsigned char *)text; *p != '\0'; p++)
	{
		unsigned char ch = *p;
		const unsigned char *glyph;
		int row;

		if (ch >= FONT_FIRST && ch < FONT_FIRST + FONT_GLYPH_COUNT)
			glyph = font8x16[ch - FONT_FIRST];
		else
			glyph = font8x16['?' - FONT_FIRST];

		for (row = 0; row < FONT_H; row++)
		{
			unsigned char bits = glyph[row];
			int col;
			for (col = 0; col < FONT_W; col++)
			{
				if (bits & (0x80 >> col))
					ui_put_pixel(x + col, y + row, color);
			}
		}
		x += FONT_W;
	}
}

int ui_text_width(const char *text)
{
	if (text == NULL)
		return 0;
	return (int)strlen(text) * FONT_W;
}

/* 右对齐：文字右端落在 right_x */
void ui_draw_text_right(int right_x, int y, const char *text, uint32_t color)
{
	ui_draw_text(right_x - ui_text_width(text), y, text, color);
}

/* 水平居中并限定最大宽度；超长截断，末尾显示 '>' */
void ui_draw_text_center(int cx, int y, const char *text, int max_w,
	uint32_t color)
{
	char shown[128];
	int len, max_chars, width;

	if (text == NULL)
		return;

	len = (int)strlen(text);
	max_chars = max_w / FONT_W;
	if (max_chars < 1)
		max_chars = 1;

	if (len <= max_chars)
	{
		memcpy(shown, text, len + 1);
	}
	else
	{
		int keep = max_chars - 1;
		if (keep < 0)
			keep = 0;
		memcpy(shown, text, keep);
		shown[keep] = '>';
		shown[keep + 1] = '\0';
	}

	width = ui_text_width(shown);
	ui_draw_text(cx - width / 2, y, shown, color);
}

/* ===================== 文字（ASCII 8x16 + 中文 GB2312 16x16） ===================== */

/* 画一个 8x16 ASCII 字符 */
static void ui_draw_ascii_char(int x, int y, unsigned char ch, uint32_t color)
{
	const unsigned char *glyph;
	int row;

	if (ch >= FONT_FIRST && ch < FONT_FIRST + FONT_GLYPH_COUNT)
		glyph = font8x16[ch - FONT_FIRST];
	else
		glyph = font8x16['?' - FONT_FIRST];

	for (row = 0; row < FONT_H; row++)
	{
		unsigned char bits = glyph[row];
		int col;
		for (col = 0; col < FONT_W; col++)
		{
			if (bits & (0x80 >> col))
				ui_put_pixel(x + col, y + row, color);
		}
	}
}

/* 按 GB2312 双字节取 16x16 字模；超出字库或空字返回 NULL */
static const unsigned char *ui_cn_glyph_ptr(unsigned char b1, unsigned char b2)
{
	const unsigned char *glyph;
	int i;

	if (b1 < 0xA1 || b1 > 0xA0 + FONT_CN_ZONES || b2 < 0xA1 || b2 > 0xFE)
		return NULL;

	glyph = font_cn16[b1 - 0xA1][b2 - 0xA1];
	for (i = 0; i < 32; i++)
	{
		if (glyph[i] != 0)
			return glyph;
	}
	return NULL;
}

/* 画一个 16x16 中文字符 */
static void ui_draw_cn_char(int x, int y, const unsigned char *glyph,
	uint32_t color)
{
	int row;

	for (row = 0; row < FONT_CN_H; row++)
	{
		unsigned char lo = glyph[row * 2];
		unsigned char hi = glyph[row * 2 + 1];
		int col;
		for (col = 0; col < 8; col++)
		{
			if (lo & (0x80 >> col))
				ui_put_pixel(x + col, y + row, color);
			if (hi & (0x80 >> col))
				ui_put_pixel(x + 8 + col, y + row, color);
		}
	}
}

/* 混合绘制：ASCII 用 8x16，GBK 双字节用 16x16，字库中没有的字显示 '?' */
void ui_draw_text_cn(int x, int y, const char *text, uint32_t color)
{
	const unsigned char *p;

	if (text == NULL)
		return;

	for (p = (const unsigned char *)text; *p != '\0'; )
	{
		unsigned char ch = *p;

		if (ch < 0x80)
		{
			ui_draw_ascii_char(x, y, ch, color);
			x += FONT_W;
			p++;
		}
		else
		{
			unsigned char ch2 = p[1];

			if (ch2 == '\0')
			{
				/* 末尾孤立的高字节，按 '?' 处理 */
				ui_draw_ascii_char(x, y, '?', color);
				x += FONT_W;
				p++;
			}
			else
			{
				const unsigned char *glyph = ui_cn_glyph_ptr(ch, ch2);
				if (glyph != NULL)
					ui_draw_cn_char(x, y, glyph, color);
				else
					ui_draw_ascii_char(x, y, '?', color);
				x += FONT_CN_W;
				p += 2;
			}
		}
	}
}

/* 混合字符串的像素宽度：ASCII 8、中文 16 */
int ui_text_width_cn(const char *text)
{
	const unsigned char *p;
	int width = 0;

	if (text == NULL)
		return 0;

	for (p = (const unsigned char *)text; *p != '\0'; )
	{
		unsigned char ch = *p;
		if (ch < 0x80)
		{
			width += FONT_W;
			p++;
		}
		else
		{
			if (p[1] == '\0')
				width += FONT_W;
			else
				width += FONT_CN_W;
			p += (p[1] == '\0') ? 1 : 2;
		}
	}
	return width;
}

/* 右对齐混合文字 */
void ui_draw_text_right_cn(int right_x, int y, const char *text, uint32_t color)
{
	ui_draw_text_cn(right_x - ui_text_width_cn(text), y, text, color);
}

/* 混合文字水平居中并限定最大宽度；超长按整字截断，末尾显示 '>' */
void ui_draw_text_center_cn(int cx, int y, const char *text, int max_w,
	uint32_t color)
{
	char shown[160];
	int total;

	if (text == NULL)
		return;

	total = ui_text_width_cn(text);
	if (total <= max_w)
	{
		ui_draw_text_cn(cx - total / 2, y, text, color);
		return;
	}

	{
		/* 预留一个 '>' 的宽度 */
		int limit = max_w - FONT_W;
		int width = 0;
		const unsigned char *p = (const unsigned char *)text;
		char *q = shown;
		const char *end = shown + sizeof(shown) - 2;

		while (*p != '\0' && q < end)
		{
			unsigned char ch = *p;
			int char_w, byte_n;

			if (ch < 0x80)
			{
				char_w = FONT_W;
				byte_n = 1;
			}
			else if (p[1] == '\0')
			{
				char_w = FONT_W;
				byte_n = 1;
			}
			else
			{
				char_w = FONT_CN_W;
				byte_n = 2;
			}

			if (width + char_w > limit)
				break;

			while (byte_n-- > 0)
				*q++ = *p++;
			width += char_w;
		}

		*q++ = '>';
		*q = '\0';
		ui_draw_text_cn(cx - (width + FONT_W) / 2, y, shown, color);
	}
}

/* ===================== 可拖拽进度条 ===================== */

/* 画进度条：浅灰轨道 + 蓝色已播放填充 + 末端蓝色圆形拖动点 */
void ui_draw_progress(int x, int y, int w, int h, double ratio, uint32_t color)
{
	int cy = y + h / 2;
	int fill_w;
	int dot_r = h + 2;

	if (ratio < 0.0)
		ratio = 0.0;
	if (ratio > 1.0)
		ratio = 1.0;
	fill_w = (int)(ratio * (w - 1) + 0.5);

	/* 轨道 */
	ui_fill_rounded_rect(x, y, w, h, h / 2, UI_FRAME);
	/* 已播放部分 */
	if (fill_w > 0)
		ui_fill_rounded_rect(x, y, fill_w + 1, h, h / 2, color);
	/* 拖动点：白边 + 蓝色内芯 */
	{
		int kx = x + fill_w;
		ui_fill_circle(kx, cy, dot_r, UI_WHITE);
		ui_draw_circle(kx, cy, dot_r, color);
		ui_fill_circle(kx, cy, dot_r - 3, color);
	}
}

/* 触摸点在进度条热区内返回 0~1，否则返回 -1（热区上下左右放大便于手指点击） */
double ui_progress_hit(int x, int y, int w, int h, int touch_x, int touch_y)
{
	double v;
	if (touch_x < x - 14 || touch_x > x + w + 14 ||
		touch_y < y - 16 || touch_y > y + h + 16)
		return -1.0;
	v = (double)(touch_x - x) / (double)(w - 1);
	if (v < 0.0)
		v = 0.0;
	if (v > 1.0)
		v = 1.0;
	return v;
}

/* 秒数格式化为 m:ss（超过1小时为 h:mm:ss） */
void ui_format_time(double seconds, char *buf, size_t buf_size)
{
	int total;
	int hours, minutes, secs;

	if (seconds < 0.0)
		seconds = 0.0;
	total = (int)(seconds + 0.5);
	hours = total / 3600;
	minutes = (total % 3600) / 60;
	secs = total % 60;

	if (hours > 0)
		snprintf(buf, buf_size, "%d:%02d:%02d", hours, minutes, secs);
	else
		snprintf(buf, buf_size, "%d:%02d", minutes, secs);
}
