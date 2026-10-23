#include "photo.h"
#include "lcd.h"
#include "event.h"
#include "readdir.h"
#include "media_actions.h"
#include "ui.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 * 相册模块 photo.c
 * 功能：
 *  - 自动扫描 /mnt/udisk/photo 下所有 .bmp 照片
 *  - 纯白相册网格页：4列x3行缩略图，每页12张
 *  - 底部胶囊：左侧"图库"（图片图标）、"收藏夹"（心形图标），无文字
 *  - 右下角右箭头：点击返回主界面
 *  - 大图浏览页：左上圆形左箭头返回，左下收藏（心形），右下删除（垃圾桶）
 *  - 左右滑动翻页/切换照片，删除前有对勾/叉二次确认
 *
 * 绘图原语与图标统一放在 ui.c，本文件只负责相册的布局与交互。
 * 整个界面只使用图标，不显示任何文字。
 * ===================================================================== */

#define PHOTO_DIR "/mnt/udisk/photo"

/* ---------- 相册网格布局 ---------- */
#define PHOTO_COLUMNS 4
#define PHOTO_ROWS 3
#define PHOTO_PAGE_SIZE (PHOTO_COLUMNS * PHOTO_ROWS)  /* 每页12张 */
#define PHOTO_MARGIN_X 6
#define PHOTO_GAP_X 4
#define PHOTO_GAP_Y 4
#define PHOTO_GRID_TOP 8
#define PHOTO_GRID_BOTTOM 400                          /* 底部留出控制栏 */
#define PHOTO_CELL_WIDTH \
	((UI_WIDTH - PHOTO_MARGIN_X * 2 - PHOTO_GAP_X * (PHOTO_COLUMNS - 1)) / PHOTO_COLUMNS)
#define PHOTO_CELL_HEIGHT \
	((PHOTO_GRID_BOTTOM - PHOTO_GRID_TOP - PHOTO_GAP_Y * (PHOTO_ROWS - 1)) / PHOTO_ROWS)

/* ---------- 底部"图库/收藏夹"切换胶囊 ---------- */
#define SWITCH_X 12
#define SWITCH_Y 406
#define SWITCH_W 280
#define SWITCH_H 64
#define SWITCH_TAB_W (SWITCH_W / 2)                    /* 每个标签宽度140 */
#define GALLERY_CX (SWITCH_X + SWITCH_TAB_W / 2)       /* 图库图标中心 */
#define FAVORITE_TAB_CX (SWITCH_X + SWITCH_TAB_W + SWITCH_TAB_W / 2) /* 收藏夹图标中心 */
#define SWITCH_CY (SWITCH_Y + SWITCH_H / 2)

/* ---------- 右下角返回主界面箭头 ---------- */
#define EXIT_CX 700
#define EXIT_CY 438
#define EXIT_SIZE 82

/* ---------- 大图页圆形按钮 ---------- */
#define VIEW_BTN_R 36
#define BACK_CX 55
#define BACK_CY 55
#define FAV_CX 55
#define FAV_CY 425
#define DEL_CX 745
#define DEL_CY 425

/* ---------- 删除确认卡片（无文字，只有垃圾桶/对勾/叉） ---------- */
#define CONFIRM_X 250
#define CONFIRM_Y 130
#define CONFIRM_W 300
#define CONFIRM_H 220
#define CONFIRM_YES_CX 330
#define CONFIRM_NO_CX 470
#define CONFIRM_BTN_CY 290
#define CONFIRM_BTN_R 32

/* ===================== 照片路径与显示 ===================== */

/* 把照片文件名拼成完整路径：/mnt/udisk/photo/<name> */
static void make_photo_path(const char *photo_name, char *buf, size_t buf_size)
{
	snprintf(buf, buf_size, "%s/%s", PHOTO_DIR, photo_name);
}

/* 取当前照片的完整路径：收藏夹视图里列表存的本来就是完整路径，
 * 全部视图里存的是裸文件名，需要拼目录。结果写入 buf 返回 */
static const char *photo_full_path(const char *name_or_path, int is_full_path,
	char *buf, size_t buf_size)
{
	if (is_full_path)
		return name_or_path;
	make_photo_path(name_or_path, buf, buf_size);
	return buf;
}

/* 把照片等比缩放显示到指定区域（居中） */
static void show_photo_at(const char *name_or_path, int is_full_path,
	int x, int y, int w, int h)
{
	char full_path[640];
	const char *path = photo_full_path(name_or_path, is_full_path,
		full_path, sizeof(full_path));
	show_bmp_fit(x, y, w, h, path);
}

/* ===================== 收藏夹相关 ===================== */

/* 只加载相册目录下的收藏完整路径，避免音乐、视频占用照片格子。 */
static int refresh_favorite_photos(char ***favorite_photos, size_t *favorite_count)
{
	media_free_favorites(*favorite_photos, *favorite_count);
	*favorite_photos = NULL;
	*favorite_count = 0;
	return media_list_favorites_in_dir(PHOTO_DIR, favorite_photos, favorite_count);
}

/* ===================== 界面绘制 ===================== */

/* 底部"图库 / 收藏夹"切换胶囊（无文字，仅图标） */
static void draw_switch(int showing_favorites)
{
	/* 外胶囊：先画浅灰再内缩画白色，形成描边 */
	ui_fill_rounded_rect(SWITCH_X, SWITCH_Y, SWITCH_W, SWITCH_H, SWITCH_H / 2, UI_FRAME);
	ui_fill_rounded_rect(SWITCH_X + 2, SWITCH_Y + 2, SWITCH_W - 4, SWITCH_H - 4,
		(SWITCH_H - 4) / 2, UI_WHITE);
	/* 选中标签的浅灰圆底 */
	if (!showing_favorites)
	{
		ui_fill_rounded_rect(SWITCH_X + 4, SWITCH_Y + 4, SWITCH_TAB_W - 8, SWITCH_H - 8,
			(SWITCH_H - 8) / 2, UI_TAB_BG);
	}
	else
	{
		ui_fill_rounded_rect(SWITCH_X + SWITCH_TAB_W + 4, SWITCH_Y + 4,
			SWITCH_TAB_W - 8, SWITCH_H - 8, (SWITCH_H - 8) / 2, UI_TAB_BG);
	}
	/* 图库图标：选中为蓝色，未选中为深色 */
	ui_draw_icon_photo(GALLERY_CX, SWITCH_CY, 38,
		showing_favorites ? UI_ICON : UI_BLUE, UI_WHITE);
	/* 与音乐、视频共用心形收藏夹图标：选中为蓝色，未选中为深色 */
	ui_draw_heart(FAVORITE_TAB_CX, SWITCH_CY, 40,
		showing_favorites ? UI_BLUE : UI_ICON);
}

/* 相册网格页 */
static void draw_album_page(char **photo_files, size_t photo_count,
	size_t page, int showing_favorites)
{
	size_t first_photo = page * PHOTO_PAGE_SIZE;
	int row, column;

	ui_fill_screen(UI_WHITE);

	/* 缩略图直接画在白底上，间隙就是白色 */
	for (row = 0; row < PHOTO_ROWS; row++)
	{
		for (column = 0; column < PHOTO_COLUMNS; column++)
		{
			size_t idx = first_photo + row * PHOTO_COLUMNS + column;
			int x = PHOTO_MARGIN_X + column * (PHOTO_CELL_WIDTH + PHOTO_GAP_X);
			int y = PHOTO_GRID_TOP + row * (PHOTO_CELL_HEIGHT + PHOTO_GAP_Y);
			if (idx >= photo_count)
				continue;
			show_photo_at(photo_files[idx], showing_favorites,
				x, y, PHOTO_CELL_WIDTH, PHOTO_CELL_HEIGHT);
		}
	}

	/* 空相册：居中灰色图片占位图标 */
	if (photo_count == 0)
		ui_draw_icon_photo(UI_WIDTH / 2, 190, 72, UI_EMPTY, UI_WHITE);

	/* 底部切换胶囊 + 右下退出箭头 */
	draw_switch(showing_favorites);
	ui_draw_arrow(EXIT_CX, EXIT_CY, EXIT_SIZE, 1, UI_ICON);
}

/* 大图浏览页 */
static void draw_photo_view(char **photo_files, size_t photo_count,
	size_t idx, int is_favorite, int is_full_path)
{
	if (photo_count > 0 && idx < photo_count)
	{
		show_photo_at(photo_files[idx], is_full_path, 0, 0, UI_WIDTH, UI_HEIGHT);
	}
	else
	{
		ui_fill_screen(UI_WHITE);
	}

	/* 左上圆形左箭头：返回相册网格 */
	ui_circle_button(BACK_CX, BACK_CY, VIEW_BTN_R);
	ui_draw_arrow(BACK_CX, BACK_CY, 40, -1, UI_ICON);

	/* 左下圆形收藏：已收藏显示红心，未收藏显示深色心 */
	ui_circle_button(FAV_CX, FAV_CY, VIEW_BTN_R);
	ui_draw_heart(FAV_CX, FAV_CY, 42, is_favorite ? UI_RED : UI_ICON);

	/* 右下圆形删除 */
	ui_circle_button(DEL_CX, DEL_CY, VIEW_BTN_R);
	ui_draw_trash(DEL_CX, DEL_CY, 46, UI_ICON);
}

/* 删除确认卡片：大垃圾桶 + 红色对勾（确认） + 灰色叉（取消） */
static void draw_delete_confirmation(void)
{
	ui_fill_rounded_rect(CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H, 16, UI_FRAME);
	ui_fill_rounded_rect(CONFIRM_X + 2, CONFIRM_Y + 2, CONFIRM_W - 4, CONFIRM_H - 4, 14,
		UI_WHITE);

	ui_draw_trash(UI_WIDTH / 2, CONFIRM_Y + 72, 54, UI_ICON);

	/* 确认：红色圆底白对勾 */
	ui_fill_circle(CONFIRM_YES_CX, CONFIRM_BTN_CY, CONFIRM_BTN_R, UI_RED);
	ui_draw_check(CONFIRM_YES_CX, CONFIRM_BTN_CY, 40, UI_WHITE);

	/* 取消：浅灰圆底深色叉 */
	ui_fill_circle(CONFIRM_NO_CX, CONFIRM_BTN_CY, CONFIRM_BTN_R, UI_TAB_BG);
	ui_draw_cross(CONFIRM_NO_CX, CONFIRM_BTN_CY, 38, UI_ICON);
}

/* ===================== 触摸坐标查找 ===================== */

/* 根据触摸点找到网格中的照片下标，点到空白处返回 SIZE_MAX */
static size_t find_photo_in_grid(size_t page, int touch_x, int touch_y,
	size_t total_count)
{
	size_t first_photo = page * PHOTO_PAGE_SIZE;
	int column = (touch_x - PHOTO_MARGIN_X) / (PHOTO_CELL_WIDTH + PHOTO_GAP_X);
	int row = (touch_y - PHOTO_GRID_TOP) / (PHOTO_CELL_HEIGHT + PHOTO_GAP_Y);
	size_t index, x, y;

	if (column < 0 || column >= PHOTO_COLUMNS || row < 0 || row >= PHOTO_ROWS)
		return SIZE_MAX;

	index = first_photo + (size_t)row * PHOTO_COLUMNS + (size_t)column;
	if (index >= total_count)
		return SIZE_MAX;

	x = PHOTO_MARGIN_X + column * (PHOTO_CELL_WIDTH + PHOTO_GAP_X);
	y = PHOTO_GRID_TOP + row * (PHOTO_CELL_HEIGHT + PHOTO_GAP_Y);
	if (touch_x < (int)x || touch_x >= (int)(x + PHOTO_CELL_WIDTH) ||
		touch_y < (int)y || touch_y >= (int)(y + PHOTO_CELL_HEIGHT))
		return SIZE_MAX;

	return index;
}

/* ===================== 相册主流程（状态机） ===================== */
void play_photo(void)
{
	char **photo_files = NULL;
	size_t photo_count = 0;
	/* 收藏列表归本次相册会话所有，退出释放后不会遗留静态悬空指针。 */
	char **favorite_photos = NULL;
	size_t favorite_count = 0;
	size_t selected_photo = 0;
	size_t page = 0;
	int x = 0, y = 0, x1 = 0, y1 = 0;
	int showing_favorites = 0;
	int showing_photo = 0;
	int delete_confirmation = 0;
	int redraw = 1;
	int running = 1;

	photo_files = read_file_names(PHOTO_DIR, ".bmp", &photo_count);
	if (photo_files == NULL)
	{
		printf("打开相册失败：%s\n", PHOTO_DIR);
		return;
	}

	while (running)
	{
		char **current_files;
		size_t current_count;
		size_t total_pages;

		if (showing_favorites)
		{
			current_files = favorite_photos;
			current_count = favorite_count;
		}
		else
		{
			current_files = photo_files;
			current_count = photo_count;
		}
		total_pages = (current_count + PHOTO_PAGE_SIZE - 1) / PHOTO_PAGE_SIZE;

		/* 按需重绘 */
		if (redraw)
		{
			if (showing_photo)
			{
				int is_favorite = 0;
				if (selected_photo < current_count)
				{
					char path_buf[640];
					const char *path = photo_full_path(current_files[selected_photo],
						showing_favorites, path_buf, sizeof(path_buf));
					is_favorite = (media_is_favorite(path) == 1);
				}
				draw_photo_view(current_files, current_count, selected_photo,
					is_favorite, showing_favorites);
			}
			else
			{
				draw_album_page(current_files, current_count, page, showing_favorites);
			}
			if (delete_confirmation)
				draw_delete_confirmation();
			redraw = 0;
		}

		/* 阻塞等待一次完整的"按下-松开"：x,y 为按下点，x1,y1 为松开点。
		 * 所有按钮/缩略图一律用"按下点"判定，避免边缘点击手指轻微抖动失效；
		 * 只有翻页/切图用按下点与松开点的位移判断手势。 */
		get_pos(&x, &y, &x1, &y1);

		{
			int dx = x1 - x;
			int dy = y1 - y;
			int abs_dx = abs(dx);
			int abs_dy = abs(dy);

			/* ---------- 删除确认对话框：只响应两个圆按钮 ---------- */
			if (delete_confirmation)
			{
				/* 红色对勾：确认删除 */
				if (x >= CONFIRM_YES_CX - CONFIRM_BTN_R &&
					x <= CONFIRM_YES_CX + CONFIRM_BTN_R &&
					y >= CONFIRM_BTN_CY - CONFIRM_BTN_R &&
					y <= CONFIRM_BTN_CY + CONFIRM_BTN_R)
				{
					if (selected_photo < current_count)
					{
						char full_path[640];
						const char *del_path = photo_full_path(
							current_files[selected_photo],
							showing_favorites, full_path, sizeof(full_path));
						media_delete_file(del_path);
					}

					/* 删除后重新加载对应列表 */
					if (showing_favorites)
					{
						refresh_favorite_photos(&favorite_photos, &favorite_count);
					}
					else
					{
						free_file_names(photo_files, photo_count);
						photo_files = read_file_names(PHOTO_DIR, ".bmp", &photo_count);
						if (photo_files == NULL)
							photo_count = 0;
					}

					{
						size_t new_count = showing_favorites ? favorite_count : photo_count;
						if (selected_photo >= new_count)
						{
							if (new_count > 0)
								selected_photo = new_count - 1;
							else
							{
								showing_photo = 0;
								selected_photo = 0;
								page = 0;
							}
						}
					}
					delete_confirmation = 0;
					redraw = 1;
				}
				/* 灰色叉：取消 */
				else if (x >= CONFIRM_NO_CX - CONFIRM_BTN_R &&
					x <= CONFIRM_NO_CX + CONFIRM_BTN_R &&
					y >= CONFIRM_BTN_CY - CONFIRM_BTN_R &&
					y <= CONFIRM_BTN_CY + CONFIRM_BTN_R)
				{
					delete_confirmation = 0;
					redraw = 1;
				}
				continue;
			}

			/* ---------- 大图浏览页（角落按钮优先于滑动） ---------- */
			if (showing_photo)
			{
				/* 左上左箭头：返回相册网格 */
				if (x >= BACK_CX - VIEW_BTN_R && x < BACK_CX + VIEW_BTN_R &&
					y >= BACK_CY - VIEW_BTN_R && y < BACK_CY + VIEW_BTN_R)
				{
					showing_photo = 0;
					page = selected_photo / PHOTO_PAGE_SIZE;
					redraw = 1;
				}
				/* 左下心形：收藏/取消收藏 */
				else if (x >= FAV_CX - VIEW_BTN_R && x < FAV_CX + VIEW_BTN_R &&
					y >= FAV_CY - VIEW_BTN_R && y < FAV_CY + VIEW_BTN_R)
				{
					if (selected_photo < current_count)
					{
						char fav_path[640];
						const char *fav_full = photo_full_path(
							current_files[selected_photo],
							showing_favorites, fav_path, sizeof(fav_path));
						media_toggle_favorite(fav_full);
					}
					/* 收藏夹里取消收藏后，当前照片可能从列表消失，修正下标 */
					if (showing_favorites)
					{
						refresh_favorite_photos(&favorite_photos, &favorite_count);
						if (selected_photo >= favorite_count)
						{
							if (favorite_count > 0)
								selected_photo = favorite_count - 1;
							else
							{
								showing_photo = 0;
								selected_photo = 0;
								page = 0;
							}
						}
					}
					redraw = 1;
				}
				/* 右下垃圾桶：弹出删除确认 */
				else if (x >= DEL_CX - VIEW_BTN_R && x < DEL_CX + VIEW_BTN_R &&
					y >= DEL_CY - VIEW_BTN_R && y < DEL_CY + VIEW_BTN_R)
				{
					delete_confirmation = 1;
					redraw = 1;
				}
				/* 左滑照片区域：下一张 */
				else if (dx < -80 && abs_dx > abs_dy && current_count > 0)
				{
					selected_photo = (selected_photo + 1) % current_count;
					page = selected_photo / PHOTO_PAGE_SIZE;
					redraw = 1;
				}
				/* 右滑照片区域：上一张 */
				else if (dx > 80 && abs_dx > abs_dy && current_count > 0)
				{
					if (selected_photo == 0)
						selected_photo = current_count - 1;
					else
						selected_photo--;
					page = selected_photo / PHOTO_PAGE_SIZE;
					redraw = 1;
				}
			}
			/* ---------- 相册网格页 ---------- */
			else
			{
				/* 右下右箭头：返回主界面。
				 * 注意必须同时限定 x、y，不能只判断 y，否则会挡住右侧箭头区域 */
				if (x >= EXIT_CX - 55 && x < EXIT_CX + 60 &&
					y >= EXIT_CY - 43 && y < EXIT_CY + 42)
				{
					running = 0;
				}
				/* 底部切换胶囊（同时限定 x 和 y，命中才处理） */
				else if (y >= SWITCH_Y && y < SWITCH_Y + SWITCH_H &&
					x >= SWITCH_X && x < SWITCH_X + SWITCH_W)
				{
					/* 图库：显示全部照片 */
					if (x < SWITCH_X + SWITCH_TAB_W)
					{
						if (showing_favorites)
						{
							showing_favorites = 0;
							page = 0;
							selected_photo = 0;
							redraw = 1;
						}
					}
					/* 收藏夹：显示收藏列表 */
					else
					{
						if (!showing_favorites)
						{
							if (refresh_favorite_photos(&favorite_photos, &favorite_count) == -1)
								printf("打开收藏夹失败\n");
							else
							{
								showing_favorites = 1;
								page = 0;
								selected_photo = 0;
								redraw = 1;
							}
						}
					}
				}
				/* 左滑：下一页 */
				else if (dx < -80 && abs_dx > abs_dy && total_pages > 1)
				{
					if (page < total_pages - 1)
						page++;
					else
						page = 0;
					redraw = 1;
				}
				/* 右滑：上一页 */
				else if (dx > 80 && abs_dx > abs_dy && total_pages > 1)
				{
					if (page > 0)
						page--;
					else
						page = total_pages - 1;
					redraw = 1;
				}
				/* 其余按下点：当作点击缩略图（用按下点定位） */
				else
				{
					size_t tapped = find_photo_in_grid(page, x, y, current_count);
					if (tapped < current_count)
					{
						selected_photo = tapped;
						showing_photo = 1;
						redraw = 1;
					}
				}
			}
		}
	}

	/* 释放资源后显示主界面。不能调用 uinit_lcd()：主程序返回后还要继续使用屏幕 */
	free_file_names(photo_files, photo_count);
	media_free_favorites(favorite_photos, favorite_count);
	show_bmp(0, 0, "/mnt/udisk/icons/main.bmp");
}
