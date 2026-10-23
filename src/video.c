#include "video.h"
#include "lcd.h"
#include "event.h"
#include "readdir.h"
#include "media_actions.h"
#include "ui.h"
#include "player.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* =====================================================================
 * 视频模块 video.c（白色 iOS 风格，纯图标 + 必要文字）
 *
 *  视频列表页：4列x3行卡片（视频图标），左下胶囊（全部/收藏），右下箭头返回。
 *  播放页：
 *   - 上方 800x320 为 mplayer 画面
 *   - 信息区（320~400）：文件名 + 可拖拽进度条 + 当前/总时长 + 播放模式按钮
 *   - 控制栏（400~480）：返回 / 快退5秒 / 暂停继续 / 快进5秒 / 收藏
 *   - 视频区左右滑动切换上一个/下一个，单击视频区暂停/继续
 *   - 播完按当前模式：顺序播完停止、列表循环下一个、单曲循环重播、随机
 * ===================================================================== */

#define VIDEO_DIR "/mnt/udisk/video"

/* ---------- 视频网格 ---------- */
#define VIDEO_COLUMNS 4
#define VIDEO_ROWS 3
#define VIDEO_PAGE_SIZE (VIDEO_COLUMNS * VIDEO_ROWS)
#define VIDEO_MARGIN_X 6
#define VIDEO_GAP_X 4
#define VIDEO_GAP_Y 4
#define VIDEO_GRID_TOP 8
#define VIDEO_GRID_BOTTOM 400
#define VIDEO_CELL_WIDTH \
	((UI_WIDTH - VIDEO_MARGIN_X * 2 - VIDEO_GAP_X * (VIDEO_COLUMNS - 1)) / VIDEO_COLUMNS)
#define VIDEO_CELL_HEIGHT \
	((VIDEO_GRID_BOTTOM - VIDEO_GRID_TOP - VIDEO_GAP_Y * (VIDEO_ROWS - 1)) / VIDEO_ROWS)
#define VIDEO_CARD_BG 0x00F2F2F2

/* ---------- 底部胶囊 ---------- */
#define SWITCH_X 12
#define SWITCH_Y 406
#define SWITCH_W 280
#define SWITCH_H 64
#define SWITCH_TAB_W (SWITCH_W / 2)
#define ALL_TAB_CX (SWITCH_X + SWITCH_TAB_W / 2)
#define FAV_TAB_CX (SWITCH_X + SWITCH_TAB_W + SWITCH_TAB_W / 2)
#define SWITCH_CY (SWITCH_Y + SWITCH_H / 2)
#define EXIT_CX 700
#define EXIT_CY 438
#define EXIT_SIZE 82

/* ---------- 播放页：画面/信息区/控制栏 ---------- */
#define VIDEO_H 320                 /* mplayer 画面高度 */
#define INFO_Y 320
#define BAR_Y 400

#define NAME_CY 330
#define NAME_MAX_W 520              /* 右侧留给模式按钮 */

#define PB_X 100
#define PB_Y 352
#define PB_W 520                    /* 缩短，右端约 620，避开模式按钮 */
#define PB_H 8
#define TIME_Y 366
#define PB_REFRESH_X 86
#define PB_REFRESH_Y 344
#define PB_REFRESH_W 554
#define PB_REFRESH_H 42

/* 播放模式按钮（信息区右侧） */
#define MODE_CX 690
#define MODE_CY 360
#define MODE_BTN_R 22
#define MODE_ICON_SIZE 30

#define BAR_CY 440
#define BAR_BTN_R 28
#define BAR_PLAY_R 32
#define BACK_CX 60
#define SEEK_BACK_CX 290
#define PLAY_CX 400
#define SEEK_FWD_CX 510
#define FAV_CX 740

#define SEEK_STEP 5.0

/* 取当前视频完整路径 */
static const char *video_full_path(const char *name_or_path, int is_full_path,
	char *buf, size_t buf_size)
{
	if (is_full_path)
		return name_or_path;
	snprintf(buf, buf_size, "%s/%s", VIDEO_DIR, name_or_path);
	return buf;
}

/* 去掉目录与扩展名，得到显示文件名 */
static void display_name(const char *name_or_path, char *buf, size_t buf_size)
{
	const char *base = strrchr(name_or_path, '/');
	const char *dot;

	base = base != NULL ? base + 1 : name_or_path;
	snprintf(buf, buf_size, "%s", base);

	dot = strrchr(buf, '.');
	if (dot != NULL)
		*((char *)dot) = '\0';
}

/* 播放指定下标的视频 */
static void start_video_at(Player *player, char **files, size_t index,
	int is_full_path)
{
	char path_buf[PATH_MAX];
	const char *path = video_full_path(files[index], is_full_path,
		path_buf, sizeof(path_buf));

	player_close(player);
	player_open(player, path, 1, VIDEO_H);
}

/* 按模式求“下一个”下标；随机模式尽量避免与当前相同 */
static size_t pick_next_index(size_t cur, size_t count, int mode)
{
	if (mode == UI_MODE_SHUFFLE && count > 1)
	{
		size_t r = (size_t)rand() % count;
		if (r == cur)
			r = (r + 1) % count;
		return r;
	}
	return (cur + 1) % count;
}

/* ===================== 列表页 ===================== */

static void draw_list_switch(int showing_favorites)
{
	ui_fill_rounded_rect(SWITCH_X, SWITCH_Y, SWITCH_W, SWITCH_H, SWITCH_H / 2, UI_FRAME);
	ui_fill_rounded_rect(SWITCH_X + 2, SWITCH_Y + 2, SWITCH_W - 4, SWITCH_H - 4,
		(SWITCH_H - 4) / 2, UI_WHITE);

	if (!showing_favorites)
		ui_fill_rounded_rect(SWITCH_X + 4, SWITCH_Y + 4, SWITCH_TAB_W - 8, SWITCH_H - 8,
			(SWITCH_H - 8) / 2, UI_TAB_BG);
	else
		ui_fill_rounded_rect(SWITCH_X + SWITCH_TAB_W + 4, SWITCH_Y + 4,
			SWITCH_TAB_W - 8, SWITCH_H - 8, (SWITCH_H - 8) / 2, UI_TAB_BG);

	ui_draw_icon_video(ALL_TAB_CX, SWITCH_CY, 42,
		showing_favorites ? UI_ICON : UI_BLUE, UI_WHITE);
	ui_draw_heart(FAV_TAB_CX, SWITCH_CY, 40,
		showing_favorites ? UI_BLUE : UI_ICON);
}

static void draw_list_page(char **video_files, size_t video_count,
	size_t page, int showing_favorites)
{
	size_t first = page * VIDEO_PAGE_SIZE;
	int row, column;

	(void)video_files;
	ui_fill_screen(UI_WHITE);

	for (row = 0; row < VIDEO_ROWS; row++)
	{
		for (column = 0; column < VIDEO_COLUMNS; column++)
		{
			size_t idx = first + (size_t)row * VIDEO_COLUMNS + (size_t)column;
			int x = VIDEO_MARGIN_X + column * (VIDEO_CELL_WIDTH + VIDEO_GAP_X);
			int y = VIDEO_GRID_TOP + row * (VIDEO_CELL_HEIGHT + VIDEO_GAP_Y);

			if (idx >= video_count)
				continue;

			ui_fill_rounded_rect(x, y, VIDEO_CELL_WIDTH, VIDEO_CELL_HEIGHT, 10,
				VIDEO_CARD_BG);
			ui_draw_icon_video(x + VIDEO_CELL_WIDTH / 2, y + VIDEO_CELL_HEIGHT / 2,
				62, UI_ICON, UI_WHITE);
		}
	}

	if (video_count == 0)
		ui_draw_icon_video(UI_WIDTH / 2, 190, 96, UI_EMPTY, UI_WHITE);

	draw_list_switch(showing_favorites);
	ui_draw_arrow(EXIT_CX, EXIT_CY, EXIT_SIZE, 1, UI_ICON);
}

/* ===================== 播放页信息区/控制栏 ===================== */

/* 局部刷新进度条 + 时长 */
static void update_progress_ui(Player *player)
{
	char current[24], total[24];
	double ratio;

	ui_fill_rect(PB_REFRESH_X, PB_REFRESH_Y, PB_REFRESH_W, PB_REFRESH_H, UI_WHITE);

	ratio = player->duration > 0.0 ? player->position / player->duration : 0.0;
	if (ratio < 0.0)
		ratio = 0.0;
	if (ratio > 1.0)
		ratio = 1.0;

	ui_draw_progress(PB_X, PB_Y, PB_W, PB_H, ratio, UI_BLUE);
	ui_format_time(player->position, current, sizeof(current));
	ui_format_time(player->duration, total, sizeof(total));
	ui_draw_text(PB_X, TIME_Y, current, UI_ICON);
	ui_draw_text_right(PB_X + PB_W, TIME_Y, total, UI_ICON);
}

/* 中间 播放/暂停 按钮（暂停切换时局部重绘） */
static void draw_play_button(Player *player)
{
	ui_circle_button(PLAY_CX, BAR_CY, BAR_PLAY_R);
	if (player->paused)
		ui_draw_play(PLAY_CX, BAR_CY, 42, UI_ICON);
	else
		ui_draw_pause(PLAY_CX, BAR_CY, 42, UI_ICON);
}

/* 播放模式按钮（信息区右侧，切换时局部重绘） */
static void draw_mode_button(int mode)
{
	ui_circle_button(MODE_CX, MODE_CY, MODE_BTN_R);
	ui_draw_mode_icon(MODE_CX, MODE_CY, MODE_ICON_SIZE, mode);
}

/* 信息区（320~400）+ 控制栏（400~480），画面区由 mplayer 负责 */
static void draw_player_chrome(char **files, size_t index, int is_favorite,
	Player *player, int mode)
{
	char name[128];

	/* 信息区 + 控制栏统一铺白 */
	ui_fill_rect(0, INFO_Y, UI_WIDTH, UI_HEIGHT - INFO_Y, UI_WHITE);

	if (files == NULL || files[index] == NULL)
		name[0] = '\0';
	else
		display_name(files[index], name, sizeof(name));
	/* 文件名可能含中文，按 GBK 混合显示 */
	ui_draw_text_center_cn(UI_WIDTH / 2, NAME_CY, name, NAME_MAX_W, UI_ICON);

	/* 控制栏顶部分隔线 */
	ui_fill_rect(0, BAR_Y, UI_WIDTH, 1, UI_FRAME);

	/* 返回 */
	ui_circle_button(BACK_CX, BAR_CY, BAR_BTN_R);
	ui_draw_arrow(BACK_CX, BAR_CY, 34, -1, UI_ICON);

	/* 快退5秒 */
	ui_circle_button(SEEK_BACK_CX, BAR_CY, BAR_BTN_R);
	ui_draw_skip(SEEK_BACK_CX, BAR_CY, 38, -1, UI_ICON);

	/* 播放/暂停 */
	draw_play_button(player);

	/* 快进5秒 */
	ui_circle_button(SEEK_FWD_CX, BAR_CY, BAR_BTN_R);
	ui_draw_skip(SEEK_FWD_CX, BAR_CY, 38, 1, UI_ICON);

	/* 收藏 */
	ui_circle_button(FAV_CX, BAR_CY, BAR_BTN_R);
	ui_draw_heart(FAV_CX, BAR_CY, 34, is_favorite ? UI_RED : UI_ICON);

	/* 信息区右侧：播放模式按钮 */
	draw_mode_button(mode);

	update_progress_ui(player);
}

static size_t find_video_in_grid(size_t page, int touch_x, int touch_y,
	size_t total_count)
{
	size_t first = page * VIDEO_PAGE_SIZE;
	int column = (touch_x - VIDEO_MARGIN_X) / (VIDEO_CELL_WIDTH + VIDEO_GAP_X);
	int row = (touch_y - VIDEO_GRID_TOP) / (VIDEO_CELL_HEIGHT + VIDEO_GAP_Y);
	size_t index, x, y;

	if (column < 0 || column >= VIDEO_COLUMNS || row < 0 || row >= VIDEO_ROWS)
		return SIZE_MAX;

	index = first + (size_t)row * VIDEO_COLUMNS + (size_t)column;
	if (index >= total_count)
		return SIZE_MAX;

	x = VIDEO_MARGIN_X + column * (VIDEO_CELL_WIDTH + VIDEO_GAP_X);
	y = VIDEO_GRID_TOP + row * (VIDEO_CELL_HEIGHT + VIDEO_GAP_Y);
	if (touch_x < (int)x || touch_x >= (int)(x + VIDEO_CELL_WIDTH) ||
		touch_y < (int)y || touch_y >= (int)(y + VIDEO_CELL_HEIGHT))
		return SIZE_MAX;

	return index;
}

/* ===================== 视频主流程 ===================== */
void play_video(void)
{
	char **all_files = NULL;
	char **fav_files = NULL;
	size_t all_count = 0;
	size_t fav_count = 0;
	size_t selected = 0;
	size_t page = 0;
	Player player;
	int x = 0, y = 0, x1 = 0, y1 = 0;
	int showing_favorites = 0;
	int showing_player = 0;
	int play_mode = UI_MODE_SEQUENCE;   /* 初始顺序播放 */
	int redraw = 1;
	int running = 1;

	player.pid = -1;
	player.in_fd = -1;
	player.out_fd = -1;

	srand((unsigned)time(NULL));

	all_files = read_file_names(VIDEO_DIR, ".avi", &all_count);
	if (all_files == NULL)
	{
		printf("打开视频失败：%s\n", VIDEO_DIR);
		return;
	}

	while (running)
	{
		char **current_files = showing_favorites ? fav_files : all_files;
		size_t current_count = showing_favorites ? fav_count : all_count;
		size_t total_pages = (current_count + VIDEO_PAGE_SIZE - 1) / VIDEO_PAGE_SIZE;

		/* 播放页：轮询进度，播完按模式决定下一个 */
		if (showing_player && player.pid > 0)
		{
			player_poll(&player);
			if (player_has_exited(&player))
			{
				if (current_count > 0)
				{
					if (play_mode == UI_MODE_REPEAT_ONE)
					{
						/* 单曲循环：重播当前 */
						start_video_at(&player, current_files, selected,
							showing_favorites);
					}
					else if (play_mode == UI_MODE_SEQUENCE &&
						selected + 1 >= current_count)
					{
						/* 顺序模式最后一个播完：停止（关闭管道），显示播放键，
						 * 再按播放可从头重新播放 */
						player_close(&player);
						player.paused = 1;
						player.position = 0.0;
					}
					else
					{
						/* 列表循环 / 随机（顺序模式非末尾） */
						selected = pick_next_index(selected, current_count, play_mode);
						start_video_at(&player, current_files, selected,
							showing_favorites);
					}
					redraw = 1;
				}
			}
			else if (!redraw)
			{
				update_progress_ui(&player);
			}
		}

		if (redraw)
		{
			if (showing_player)
			{
				int is_favorite = 0;
				if (selected < current_count)
				{
					char path_buf[PATH_MAX];
					const char *path = video_full_path(current_files[selected],
						showing_favorites, path_buf, sizeof(path_buf));
					is_favorite = (media_is_favorite(path) == 1);
				}
				draw_player_chrome(current_files, selected, is_favorite,
					&player, play_mode);
			}
			else
			{
				draw_list_page(current_files, current_count, page, showing_favorites);
			}
			redraw = 0;
		}

		{
			int got = get_pos_timeout(&x, &y, &x1, &y1,
				showing_player ? 200 : -1);
			if (got != 1)
				continue;
		}

		{
			int dx = x1 - x;
			int dy = y1 - y;
			int abs_dx = abs(dx);
			int abs_dy = abs(dy);

			if (showing_player)
			{
				/* ---- 控制栏按钮（按下点，y>=400） ---- */
				if (y >= BAR_Y &&
					ui_hit_circle(x, y, BACK_CX, BAR_CY, BAR_BTN_R))
				{
					player_close(&player);
					showing_player = 0;
					page = selected / VIDEO_PAGE_SIZE;
					redraw = 1;
				}
				else if (y >= BAR_Y &&
					ui_hit_circle(x, y, SEEK_BACK_CX, BAR_CY, BAR_BTN_R))
				{
					player_seek_relative(&player, -SEEK_STEP);
					update_progress_ui(&player);
				}
				else if (y >= BAR_Y &&
					ui_hit_circle(x, y, PLAY_CX, BAR_CY, BAR_PLAY_R))
				{
					if (player.pid < 0)
					{
						/* 顺序播完停止后重播当前 */
						player.position = 0.0;
						player.paused = 0;
						start_video_at(&player, current_files, selected,
							showing_favorites);
						redraw = 1;
					}
					else
					{
						player_toggle_pause(&player);
						draw_play_button(&player);
					}
				}
				else if (y >= BAR_Y &&
					ui_hit_circle(x, y, SEEK_FWD_CX, BAR_CY, BAR_BTN_R))
				{
					player_seek_relative(&player, SEEK_STEP);
					update_progress_ui(&player);
				}
				else if (y >= BAR_Y &&
					ui_hit_circle(x, y, FAV_CX, BAR_CY, BAR_BTN_R))
				{
					if (selected < current_count)
					{
						char path_buf[PATH_MAX];
						const char *path = video_full_path(current_files[selected],
							showing_favorites, path_buf, sizeof(path_buf));
						media_toggle_favorite(path);
					}
					if (showing_favorites)
					{
						media_free_favorites(fav_files, fav_count);
						fav_files = NULL;
						fav_count = 0;
						media_list_favorites_in_dir(VIDEO_DIR, &fav_files, &fav_count);
						if (selected >= fav_count)
						{
							if (fav_count > 0)
							{
								selected = fav_count - 1;
								start_video_at(&player, fav_files, selected, 1);
							}
							else
							{
								player_close(&player);
								showing_player = 0;
								selected = 0;
								page = 0;
							}
						}
					}
					redraw = 1;
				}
				/* ---- 信息区：模式按钮切换 / 进度条拖拽 ---- */
				else if (y >= INFO_Y && y < BAR_Y)
				{
					if (ui_hit_circle(x, y, MODE_CX, MODE_CY, MODE_BTN_R))
					{
						play_mode = (play_mode + 1) % 4;
						draw_mode_button(play_mode);
					}
					else
					{
						double v = ui_progress_hit(PB_X, PB_Y, PB_W, PB_H, x1, y1);
						if (v >= 0.0 && player.duration > 0.0)
						{
							player_seek_absolute(&player, v * player.duration);
							player.paused = 0;
							update_progress_ui(&player);
						}
					}
				}
				/* ---- 视频区手势：左右滑切换，单击暂停/重播 ---- */
				else if (y < INFO_Y)
				{
					if (abs_dx > 80 && abs_dx > abs_dy)
					{
						if (current_count > 0)
						{
							/* 左滑下一个（随机模式随机），右滑上一个 */
							if (dx < 0)
								selected = pick_next_index(selected, current_count,
									play_mode);
							else
								selected = selected == 0 ? current_count - 1 : selected - 1;
							start_video_at(&player, current_files, selected,
								showing_favorites);
							redraw = 1;
						}
					}
					else if (abs_dx <= 80 && abs_dy <= 80)
					{
						if (player.pid < 0)
						{
							player.position = 0.0;
							player.paused = 0;
							start_video_at(&player, current_files, selected,
								showing_favorites);
							redraw = 1;
						}
						else
						{
							player_toggle_pause(&player);
							draw_play_button(&player);
						}
					}
				}
			}
			/* ---------- 列表页 ---------- */
			else
			{
				if (x >= EXIT_CX - 55 && x < EXIT_CX + 60 &&
					y >= EXIT_CY - 43 && y < EXIT_CY + 42)
				{
					running = 0;
				}
				else if (y >= SWITCH_Y && y < SWITCH_Y + SWITCH_H &&
					x >= SWITCH_X && x < SWITCH_X + SWITCH_W)
				{
					if (x < SWITCH_X + SWITCH_TAB_W)
					{
						if (showing_favorites)
						{
							showing_favorites = 0;
							page = 0;
							selected = 0;
							redraw = 1;
						}
					}
					else
					{
						if (!showing_favorites)
						{
							media_free_favorites(fav_files, fav_count);
							fav_files = NULL;
							fav_count = 0;
							if (media_list_favorites_in_dir(VIDEO_DIR,
								&fav_files, &fav_count) == 0)
							{
								showing_favorites = 1;
								page = 0;
								selected = 0;
								redraw = 1;
							}
						}
					}
				}
				else if (dx < -80 && abs_dx > abs_dy && total_pages > 1)
				{
					page = (page + 1) % total_pages;
					redraw = 1;
				}
				else if (dx > 80 && abs_dx > abs_dy && total_pages > 1)
				{
					page = page == 0 ? total_pages - 1 : page - 1;
					redraw = 1;
				}
				else
				{
					size_t tapped = find_video_in_grid(page, x, y, current_count);
					if (tapped < current_count)
					{
						selected = tapped;
						start_video_at(&player, current_files, selected,
							showing_favorites);
						showing_player = 1;
						redraw = 1;
					}
				}
			}
		}
	}

	player_close(&player);
	free_file_names(all_files, all_count);
	media_free_favorites(fav_files, fav_count);
	show_bmp(0, 0, "/mnt/udisk/icons/main.bmp");
}
