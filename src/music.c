#include "music.h"
#include "lcd.h"
#include "event.h"
#include "readdir.h"
#include "media_actions.h"
#include "ui.h"
#include "player.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =====================================================================
 * 音乐模块 music.c（白色 iOS 风格，纯图标 + 必要文字）
 *
 *  歌曲列表页：4列x3行卡片（音符），左下胶囊（全部/收藏），右下箭头返回，
 *             左右滑动翻页，点击进入播放。
 *  播放页：
 *   - 顶部：左返回，右收藏；中间显示文件名
 *   - 中间大音符；下方可拖拽进度条 + 当前时长/总时长
 *   - 底部：快退5秒 / 音量- / 播放暂停 / 音量+ / 快进5秒
 *   - 音量条右侧：播放模式按钮（顺序/列表循环/单曲循环/随机）
 *   - 手势：左右滑动切歌，上下滑动调音量
 *   - 播完按当前模式：顺序播完停止、列表循环下一首、单曲循环重播、随机
 *
 * 播放统一走 mplayer（-vo null 纯音频，不占屏幕），用 slave 管道
 * 获取进度/时长、执行进度拖拽与音量控制。
 * ===================================================================== */

#define MUSIC_DIR "/mnt/udisk/music"

/* ---------- 歌曲网格（与相册同布局） ---------- */
#define MUSIC_COLUMNS 4
#define MUSIC_ROWS 3
#define MUSIC_PAGE_SIZE (MUSIC_COLUMNS * MUSIC_ROWS)
#define MUSIC_MARGIN_X 6
#define MUSIC_GAP_X 4
#define MUSIC_GAP_Y 4
#define MUSIC_GRID_TOP 8
#define MUSIC_GRID_BOTTOM 400
#define MUSIC_CELL_WIDTH \
	((UI_WIDTH - MUSIC_MARGIN_X * 2 - MUSIC_GAP_X * (MUSIC_COLUMNS - 1)) / MUSIC_COLUMNS)
#define MUSIC_CELL_HEIGHT \
	((MUSIC_GRID_BOTTOM - MUSIC_GRID_TOP - MUSIC_GAP_Y * (MUSIC_ROWS - 1)) / MUSIC_ROWS)
#define MUSIC_CARD_BG 0x00F2F2F2

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

/* ---------- 播放页布局 ---------- */
#define TOP_BTN_R 36
#define BACK_CX 55
#define BACK_CY 55
#define FAV_CX 745
#define FAV_CY 55
#define NAME_CX 400
#define NAME_Y 48
#define NAME_MAX_W 600
#define NOTE_CX 400
#define NOTE_CY 160
#define NOTE_SIZE 120

#define PB_X 70
#define PB_Y 250
#define PB_W 660
#define PB_H 10
#define TIME_Y 270
#define PB_REFRESH_X 54
#define PB_REFRESH_Y 236
#define PB_REFRESH_W 692
#define PB_REFRESH_H 54

/* 音量条：左起 x=70，右端约 604，右侧留给模式按钮 */
#define VOL_BARS 10
#define VOL_BAR_X 70
#define VOL_BAR_W 48
#define VOL_BAR_H 12
#define VOL_BAR_GAP 6
#define VOL_BAR_Y 318

/* 播放模式按钮（音量条右侧） */
#define MODE_CX 725
#define MODE_CY 324
#define MODE_BTN_R 28
#define MODE_ICON_SIZE 38

#define BOTTOM_CY 425
#define SMALL_BTN_R 32
#define PLAY_BTN_R 38
#define SEEK_BACK_CX 130
#define VOLM_CX 265
#define PLAY_CX 400
#define VOLP_CX 535
#define SEEK_FWD_CX 670

#define SEEK_STEP 5.0
#define VOLUME_STEP 10
#define VOLUME_INIT 60

/* 取当前歌曲完整路径 */
static const char *music_full_path(const char *name_or_path, int is_full_path,
	char *buf, size_t buf_size)
{
	if (is_full_path)
		return name_or_path;
	snprintf(buf, buf_size, "%s/%s", MUSIC_DIR, name_or_path);
	return buf;
}

/* 去掉目录与扩展名，得到用于界面显示的文件名 */
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

/* 播放指定下标的歌曲（先停旧进程） */
static void start_song_at(Player *player, char **files, size_t index,
	int is_full_path, int volume)
{
	char path_buf[640];
	const char *path = music_full_path(files[index], is_full_path,
		path_buf, sizeof(path_buf));

	player_close(player);
	if (player_open(player, path, 0, 0) == 0)
		player_set_volume(player, volume);
}

/* 按模式求“下一首”下标；随机模式尽量避免与当前相同 */
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

	ui_draw_note(ALL_TAB_CX, SWITCH_CY, 40,
		showing_favorites ? UI_ICON : UI_BLUE);
	ui_draw_heart(FAV_TAB_CX, SWITCH_CY, 40,
		showing_favorites ? UI_BLUE : UI_ICON);
}

static void draw_list_page(char **music_files, size_t music_count,
	size_t page, int showing_favorites)
{
	size_t first = page * MUSIC_PAGE_SIZE;
	int row, column;

	(void)music_files;
	ui_fill_screen(UI_WHITE);

	for (row = 0; row < MUSIC_ROWS; row++)
	{
		for (column = 0; column < MUSIC_COLUMNS; column++)
		{
			size_t idx = first + (size_t)row * MUSIC_COLUMNS + (size_t)column;
			int x = MUSIC_MARGIN_X + column * (MUSIC_CELL_WIDTH + MUSIC_GAP_X);
			int y = MUSIC_GRID_TOP + row * (MUSIC_CELL_HEIGHT + MUSIC_GAP_Y);

			if (idx >= music_count)
				continue;

			ui_fill_rounded_rect(x, y, MUSIC_CELL_WIDTH, MUSIC_CELL_HEIGHT, 10,
				MUSIC_CARD_BG);
			ui_draw_note(x + MUSIC_CELL_WIDTH / 2, y + MUSIC_CELL_HEIGHT / 2,
				60, UI_ICON);
		}
	}

	if (music_count == 0)
		ui_draw_note(UI_WIDTH / 2, 190, 90, UI_EMPTY);

	draw_list_switch(showing_favorites);
	ui_draw_arrow(EXIT_CX, EXIT_CY, EXIT_SIZE, 1, UI_ICON);
}

/* ===================== 播放页 ===================== */

static void draw_volume_bar(int volume)
{
	int i;

	for (i = 0; i < VOL_BARS; i++)
	{
		int x = VOL_BAR_X + i * (VOL_BAR_W + VOL_BAR_GAP);
		int active = (i + 1) * 10 <= volume;
		ui_fill_rounded_rect(x, VOL_BAR_Y, VOL_BAR_W, VOL_BAR_H, 4,
			active ? UI_BLUE : UI_FRAME);
	}
}

/* 播放模式按钮（圆底 + 模式图标） */
static void draw_mode_button(int mode)
{
	ui_circle_button(MODE_CX, MODE_CY, MODE_BTN_R);
	ui_draw_mode_icon(MODE_CX, MODE_CY, MODE_ICON_SIZE, mode);
}

/* 局部刷新进度条 + 时长（不整屏重绘，避免闪烁） */
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

/* 播放页静态部分：文件名、音符、按钮、音量条、模式按钮；进度条随后单独刷新 */
static void draw_player_page(char **files, size_t index, int is_favorite,
	Player *player, int volume, int mode)
{
	char name[128];

	ui_fill_screen(UI_WHITE);

	if (files == NULL || files[index] == NULL)
		name[0] = '\0';
	else
		display_name(files[index], name, sizeof(name));
	/* 文件名可能含中文，按 GBK 混合显示 */
	ui_draw_text_center_cn(NAME_CX, NAME_Y, name, NAME_MAX_W, UI_ICON);

	/* 中间大音符：播放中蓝色，暂停深色 */
	ui_draw_note(NOTE_CX, NOTE_CY, NOTE_SIZE,
		player->paused ? UI_ICON : UI_BLUE);

	/* 顶部：返回 / 收藏 */
	ui_circle_button(BACK_CX, BACK_CY, TOP_BTN_R);
	ui_draw_arrow(BACK_CX, BACK_CY, 40, -1, UI_ICON);
	ui_circle_button(FAV_CX, FAV_CY, TOP_BTN_R);
	ui_draw_heart(FAV_CX, FAV_CY, 42, is_favorite ? UI_RED : UI_ICON);

	/* 底部：快退5秒 / 音量- / 播放暂停 / 音量+ / 快进5秒 */
	ui_circle_button(SEEK_BACK_CX, BOTTOM_CY, SMALL_BTN_R);
	ui_draw_skip(SEEK_BACK_CX, BOTTOM_CY, 42, -1, UI_ICON);

	ui_circle_button(VOLM_CX, BOTTOM_CY, SMALL_BTN_R);
	ui_draw_minus(VOLM_CX, BOTTOM_CY, 40, UI_ICON);

	ui_circle_button(PLAY_CX, BOTTOM_CY, PLAY_BTN_R);
	if (player->paused)
		ui_draw_play(PLAY_CX, BOTTOM_CY, 50, UI_ICON);
	else
		ui_draw_pause(PLAY_CX, BOTTOM_CY, 50, UI_ICON);

	ui_circle_button(VOLP_CX, BOTTOM_CY, SMALL_BTN_R);
	ui_draw_plus(VOLP_CX, BOTTOM_CY, 40, UI_ICON);

	ui_circle_button(SEEK_FWD_CX, BOTTOM_CY, SMALL_BTN_R);
	ui_draw_skip(SEEK_FWD_CX, BOTTOM_CY, 42, 1, UI_ICON);

	draw_volume_bar(volume);
	draw_mode_button(mode);
	update_progress_ui(player);
}

static size_t find_music_in_grid(size_t page, int touch_x, int touch_y,
	size_t total_count)
{
	size_t first = page * MUSIC_PAGE_SIZE;
	int column = (touch_x - MUSIC_MARGIN_X) / (MUSIC_CELL_WIDTH + MUSIC_GAP_X);
	int row = (touch_y - MUSIC_GRID_TOP) / (MUSIC_CELL_HEIGHT + MUSIC_GAP_Y);
	size_t index, x, y;

	if (column < 0 || column >= MUSIC_COLUMNS || row < 0 || row >= MUSIC_ROWS)
		return SIZE_MAX;

	index = first + (size_t)row * MUSIC_COLUMNS + (size_t)column;
	if (index >= total_count)
		return SIZE_MAX;

	x = MUSIC_MARGIN_X + column * (MUSIC_CELL_WIDTH + MUSIC_GAP_X);
	y = MUSIC_GRID_TOP + row * (MUSIC_CELL_HEIGHT + MUSIC_GAP_Y);
	if (touch_x < (int)x || touch_x >= (int)(x + MUSIC_CELL_WIDTH) ||
		touch_y < (int)y || touch_y >= (int)(y + MUSIC_CELL_HEIGHT))
		return SIZE_MAX;

	return index;
}

/* ===================== 音乐主流程 ===================== */
void play_music(void)
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
	int volume = VOLUME_INIT;
	int play_mode = UI_MODE_SEQUENCE;   /* 初始顺序播放 */
	int redraw = 1;
	int running = 1;

	player.pid = -1;
	player.in_fd = -1;
	player.out_fd = -1;

	srand((unsigned)time(NULL));

	all_files = read_file_names(MUSIC_DIR, ".mp3", &all_count);
	if (all_files == NULL)
	{
		printf("打开音乐失败：%s\n", MUSIC_DIR);
		return;
	}

	while (running)
	{
		char **current_files = showing_favorites ? fav_files : all_files;
		size_t current_count = showing_favorites ? fav_count : all_count;
		size_t total_pages = (current_count + MUSIC_PAGE_SIZE - 1) / MUSIC_PAGE_SIZE;

		/* 播放页：轮询播放器，刷新进度；播完按模式决定下一首 */
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
						start_song_at(&player, current_files, selected,
							showing_favorites, volume);
					}
					else if (play_mode == UI_MODE_SEQUENCE &&
						selected + 1 >= current_count)
					{
						/* 顺序模式最后一首播完：停止（关闭管道），显示播放键，
						 * 再按播放可从头重新播放 */
						player_close(&player);
						player.paused = 1;
						player.position = 0.0;
					}
					else
					{
						/* 列表循环 / 随机（顺序模式非末尾） */
						selected = pick_next_index(selected, current_count, play_mode);
						start_song_at(&player, current_files, selected,
							showing_favorites, volume);
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
					char path_buf[640];
					const char *path = music_full_path(current_files[selected],
						showing_favorites, path_buf, sizeof(path_buf));
					is_favorite = (media_is_favorite(path) == 1);
				}
				draw_player_page(current_files, selected, is_favorite,
					&player, volume, play_mode);
			}
			else
			{
				draw_list_page(current_files, current_count, page, showing_favorites);
			}
			redraw = 0;
		}

		{
			/* 播放页200ms超时（用于刷新进度），列表页永久等待触摸 */
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
				/* ---- 顶部按钮 ---- */
				if (ui_hit_circle(x, y, BACK_CX, BACK_CY, TOP_BTN_R))
				{
					showing_player = 0;
					page = selected / MUSIC_PAGE_SIZE;
					redraw = 1;
				}
				else if (ui_hit_circle(x, y, FAV_CX, FAV_CY, TOP_BTN_R))
				{
					if (selected < current_count)
					{
						char path_buf[640];
						const char *path = music_full_path(current_files[selected],
							showing_favorites, path_buf, sizeof(path_buf));
						media_toggle_favorite(path);
					}
					if (showing_favorites)
					{
						media_free_favorites(fav_files, fav_count);
						fav_files = NULL;
						fav_count = 0;
						media_list_favorites_in_dir(MUSIC_DIR, &fav_files, &fav_count);
						if (selected >= fav_count)
						{
							if (fav_count > 0)
							{
								selected = fav_count - 1;
								start_song_at(&player, fav_files, selected, 1, volume);
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
				/* ---- 快退5秒 ---- */
				else if (ui_hit_circle(x, y, SEEK_BACK_CX, BOTTOM_CY, SMALL_BTN_R))
				{
					player_seek_relative(&player, -SEEK_STEP);
					update_progress_ui(&player);
				}
				/* ---- 音量- ---- */
				else if (ui_hit_circle(x, y, VOLM_CX, BOTTOM_CY, SMALL_BTN_R))
				{
					if (volume > 0)
					{
						volume -= VOLUME_STEP;
						if (volume < 0)
							volume = 0;
						player_set_volume(&player, volume);
						draw_volume_bar(volume);
					}
				}
				/* ---- 播放 / 暂停 / 顺序播完停止后重播 ---- */
				else if (ui_hit_circle(x, y, PLAY_CX, BOTTOM_CY, PLAY_BTN_R))
				{
					if (player.pid < 0)
					{
						player.position = 0.0;
						player.paused = 0;
						start_song_at(&player, current_files, selected,
							showing_favorites, volume);
					}
					else
					{
						player_toggle_pause(&player);
					}
					redraw = 1;
				}
				/* ---- 音量+ ---- */
				else if (ui_hit_circle(x, y, VOLP_CX, BOTTOM_CY, SMALL_BTN_R))
				{
					if (volume < 100)
					{
						volume += VOLUME_STEP;
						if (volume > 100)
							volume = 100;
						player_set_volume(&player, volume);
						draw_volume_bar(volume);
					}
				}
				/* ---- 快进5秒 ---- */
				else if (ui_hit_circle(x, y, SEEK_FWD_CX, BOTTOM_CY, SMALL_BTN_R))
				{
					player_seek_relative(&player, SEEK_STEP);
					update_progress_ui(&player);
				}
				/* ---- 播放模式切换（顺序/列表循环/单曲循环/随机） ---- */
				else if (ui_hit_circle(x, y, MODE_CX, MODE_CY, MODE_BTN_R))
				{
					play_mode = (play_mode + 1) % 4;
					draw_mode_button(play_mode);
				}
				/* ---- 进度条拖拽（用松开点，支持按住拖动） ---- */
				else
				{
					double v = ui_progress_hit(PB_X, PB_Y, PB_W, PB_H, x1, y1);
					if (v < 0.0)
						v = ui_progress_hit(PB_X, PB_Y, PB_W, PB_H, x, y);
					if (v >= 0.0 && player.duration > 0.0)
					{
						player_seek_absolute(&player, v * player.duration);
						player.paused = 0;
						update_progress_ui(&player);
					}
					/* ---- 手势：左右切歌，上下音量 ---- */
					else if (abs_dx > 60 && abs_dx > abs_dy)
					{
						if (current_count > 0)
						{
							/* 左滑下一首（随机模式随机），右滑上一首 */
							if (dx < 0)
								selected = pick_next_index(selected, current_count,
									play_mode);
							else
								selected = selected == 0 ? current_count - 1 : selected - 1;
							start_song_at(&player, current_files, selected,
								showing_favorites, volume);
							redraw = 1;
						}
					}
					else if (abs_dy > 60 && abs_dy > abs_dx)
					{
						if (dy < 0 && volume < 100)
						{
							volume += VOLUME_STEP;
							if (volume > 100)
								volume = 100;
							player_set_volume(&player, volume);
							draw_volume_bar(volume);
						}
						else if (dy > 0 && volume > 0)
						{
							volume -= VOLUME_STEP;
							if (volume < 0)
								volume = 0;
							player_set_volume(&player, volume);
							draw_volume_bar(volume);
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
							if (media_list_favorites_in_dir(MUSIC_DIR,
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
					size_t tapped = find_music_in_grid(page, x, y, current_count);
					if (tapped < current_count)
					{
						selected = tapped;
						start_song_at(&player, current_files, selected,
							showing_favorites, volume);
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
