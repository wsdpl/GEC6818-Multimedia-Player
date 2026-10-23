#ifndef __UI_H_
#define __UI_H_

#include <stdint.h>
#include <stddef.h>

/* =====================================================================
 * 公共自绘 UI 模块 ui.h
 * 白色 iOS 风格：相册 / 音乐 / 视频三个模块共用同一套绘图原语与图标。
 * 所有界面只用图标，不显示文字；颜色统一用 0x00RRGGBB。
 * ===================================================================== */

#define UI_WIDTH 800
#define UI_HEIGHT 480

/* 白色主题配色 */
#define UI_WHITE  0x00FFFFFF
#define UI_BLACK  0x00000000
#define UI_ICON   0x00222222   /* 主图标深色（近黑） */
#define UI_BLUE   0x00007AFF   /* 选中态 iOS 蓝 */
#define UI_RED    0x00FF3B30   /* 收藏/确认红色 */
#define UI_FRAME  0x00D8D8D8   /* 圆按钮、卡片描边浅灰 */
#define UI_TAB_BG 0x00F0F0F0   /* 胶囊选中标签的浅灰底 */
#define UI_EMPTY  0x00D8D8D8   /* 空列表占位图标灰 */

/* ---------- 基础绘图原语 ---------- */
void ui_put_pixel(int x, int y, uint32_t color);
void ui_fill_rect(int x, int y, int w, int h, uint32_t color);
void ui_fill_screen(uint32_t color);
void ui_fill_polygon(const int *points, int n, uint32_t color);
void ui_fill_circle(int cx, int cy, int r, uint32_t color);
void ui_draw_circle(int cx, int cy, int r, uint32_t color);
void ui_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* ---------- 通用图标 ---------- */
/* 方向箭头：dir=+1 向右，dir=-1 向左，size 为箭头总宽度 */
void ui_draw_arrow(int cx, int cy, int size, int dir, uint32_t color);
/* 实心心形 */
void ui_draw_heart(int cx, int cy, int size, uint32_t color);
/* 垃圾桶 */
void ui_draw_trash(int cx, int cy, int size, uint32_t color);
/* 图库图标（圆角图片底 + 太阳 + 山峰） */
void ui_draw_icon_photo(int cx, int cy, int size, uint32_t color, uint32_t bg);
/* 收藏夹层叠图标 */
void ui_draw_icon_layers(int cx, int cy, int size, uint32_t color);
/* 粗对勾 */
void ui_draw_check(int cx, int cy, int size, uint32_t color);
/* 粗叉 */
void ui_draw_cross(int cx, int cy, int size, uint32_t color);
/* 白色圆底 + 浅灰描边的圆形按钮 */
void ui_circle_button(int cx, int cy, int r);
/* 判断触摸点是否落在圆形按钮内，命中返回 1 */
int ui_hit_circle(int touch_x, int touch_y, int cx, int cy, int r);

/* ---------- 音乐 / 视频图标 ---------- */
/* 音符 */
void ui_draw_note(int cx, int cy, int size, uint32_t color);
/* 视频图标（屏幕 + 播放三角） */
void ui_draw_icon_video(int cx, int cy, int size, uint32_t color, uint32_t bg);
/* 播放三角 */
void ui_draw_play(int cx, int cy, int size, uint32_t color);
/* 暂停双竖条 */
void ui_draw_pause(int cx, int cy, int size, uint32_t color);
/* 上一首/下一首：dir=+1 下一首(►|)，dir=-1 上一首(|◄) */
void ui_draw_skip(int cx, int cy, int size, int dir, uint32_t color);
/* 加号 / 减号（音量增减） */
void ui_draw_plus(int cx, int cy, int size, uint32_t color);
void ui_draw_minus(int cx, int cy, int size, uint32_t color);

/* ---------- 播放模式图标 ---------- */
/* 列表循环（回旋带） */
void ui_draw_repeat_list(int cx, int cy, int size, uint32_t color);
/* 单曲循环（回旋带 + 数字1） */
void ui_draw_repeat_one(int cx, int cy, int size, uint32_t color);
/* 随机播放（交叉箭头） */
void ui_draw_shuffle(int cx, int cy, int size, uint32_t color);

/* ---------- 播放模式 ---------- */
#define UI_MODE_SEQUENCE   0   /* 顺序播放（播完列表停止） */
#define UI_MODE_LIST       1   /* 列表循环 */
#define UI_MODE_REPEAT_ONE 2   /* 单曲循环 */
#define UI_MODE_SHUFFLE    3   /* 随机播放 */
/* 按模式绘制模式图标：顺序用灰色列表循环，其余蓝色 */
void ui_draw_mode_icon(int cx, int cy, int size, int mode);

/* ---------- 文字（8x16 ASCII 点阵） ---------- */
void ui_draw_text(int x, int y, const char *text, uint32_t color);
int ui_text_width(const char *text);
/* 右对齐：文字右端落在 right_x */
void ui_draw_text_right(int right_x, int y, const char *text, uint32_t color);
/* 水平居中并限定最大宽度，超长则截断并在末尾显示 '>' */
void ui_draw_text_center(int cx, int y, const char *text, int max_w, uint32_t color);

/* ---------- 中文混合文字（ASCII 8x16 + GB2312 中文 16x16） ----------
 * 文件名按 GBK 编码解析：0x80 以下为 ASCII，0x81 起的双字节为中文。
 * 字库为 GB2312 简体，GBK 扩展字（部分繁体/生僻字）会显示为 '?'。 */
void ui_draw_text_cn(int x, int y, const char *text, uint32_t color);
int ui_text_width_cn(const char *text);
void ui_draw_text_right_cn(int right_x, int y, const char *text, uint32_t color);
void ui_draw_text_center_cn(int cx, int y, const char *text, int max_w, uint32_t color);

/* ---------- 可拖拽进度条 ---------- */
/* 画进度条：轨道 + 已播放填充 + 末端圆形拖动点；ratio 取 0~1 */
void ui_draw_progress(int x, int y, int w, int h, double ratio, uint32_t color);
/* 触摸点在进度条上时返回 0~1 比例，否则返回 -1（热区在条上下左右各放大若干像素） */
double ui_progress_hit(int x, int y, int w, int h, int touch_x, int touch_y);
/* 把秒数格式化为 m:ss（或 h:mm:ss），写入 buf */
void ui_format_time(double seconds, char *buf, size_t buf_size);

#endif
