/* Linux 主机回归测试：模拟触摸和图片显示，使用真实收藏读取与 UI 绘制。
 * gcc -g -fsanitize=address,undefined -I inc tests/photo_regression.c \
 *   src/photo.c src/media_actions.c src/ui.c src/font.c src/font_cn.c \
 *   -Wl,--wrap=fopen -o /tmp/photo_regression
 * /tmp/photo_regression
 * 收藏数据通过 tmpfile 提供，不读写实际 U 盘。
 */
#include "photo.h"
#include "readdir.h"
#include "ui.h"

#include <assert.h>
#include <string.h>

static int framebuffer[UI_WIDTH * UI_HEIGHT];
int *mmap_p = framebuffer;
static const char *favorite_records;
static int thumbnail_count;
static int full_photo_count;
static int exit_count;

struct touch_step {
	int x, y;
	int thumbnails, full_photos;
};

static const struct touch_step *steps;
static size_t step_count;
static size_t next_step;

/* 链接器只替换收藏文件的打开操作，真实解析和释放逻辑照常执行。 */
FILE *__wrap_fopen(const char *path, const char *mode)
{
	FILE *file;
	assert(strcmp(path, "/mnt/udisk/favorites.txt") == 0);
	assert(strcmp(mode, "r") == 0);
	file = tmpfile();
	assert(file != NULL);
	assert(fputs(favorite_records, file) >= 0);
	rewind(file);
	return file;
}

char **read_file_names(const char *dir, const char *suffix, size_t *count)
{
	char **files = malloc(2 * sizeof(*files));
	assert(strcmp(dir, "/mnt/udisk/photo") == 0);
	assert(strcmp(suffix, ".bmp") == 0);
	assert(files != NULL);
	files[0] = strdup("first.bmp");
	files[1] = strdup("second.bmp");
	assert(files[0] != NULL && files[1] != NULL);
	*count = 2;
	return files;
}

void free_file_names(char **files, size_t count)
{
	size_t i;
	for (i = 0; i < count; i++)
		free(files[i]);
	free(files);
}

void show_bmp_fit(int x, int y, int w, int h, const char *path)
{
	const char *first = "/mnt/udisk/photo/first.bmp";
	const char *second = "/mnt/udisk/photo/second.bmp";
	/* 任何音乐、视频条目传入图片显示都会令测试失败。 */
	assert(strcmp(path, first) == 0 || strcmp(path, second) == 0);
	if (w == UI_WIDTH && h == UI_HEIGHT)
	{
		assert(x == 0 && y == 0);
		assert(strcmp(path, full_photo_count == 0 ? first : second) == 0);
		full_photo_count++;
	}
	else
	{
		/* 两张收藏必须紧邻左上角排列，不能留下不可见的媒体格子。 */
		assert(x == (thumbnail_count % 2 == 0 ? 6 : 204));
		assert(y == 8 && w == 194 && h == 128);
		assert(strcmp(path, thumbnail_count % 2 == 0 ? first : second) == 0);
		thumbnail_count++;
	}
}

void show_bmp(int x, int y, char *path)
{
	assert(x == 0 && y == 0);
	assert(strcmp(path, "/mnt/udisk/icons/main.bmp") == 0);
	exit_count++;
}

void get_pos(int *x, int *y, int *x1, int *y1)
{
	const struct touch_step *step;
	assert(next_step < step_count);
	step = &steps[next_step++];
	/* 每次触摸前核对上一步显示结果，空白点击不能打开大图。 */
	assert(thumbnail_count == step->thumbnails);
	assert(full_photo_count == step->full_photos);
	*x = *x1 = step->x;
	*y = *y1 = step->y;
}

static void run_session(const struct touch_step *script, size_t count)
{
	steps = script;
	step_count = count;
	next_step = 0;
	thumbnail_count = full_photo_count = exit_count = 0;
	play_photo();
	assert(next_step == step_count && exit_count == 1);
}

int main(void)
{
	int i;
	const struct touch_step reenter[] = {
		{222, 438, 2, 0}, /* 收藏夹 */
		{700, 438, 4, 0}  /* 退出 */
	};
	const struct touch_step mixed[] = {
		{222, 438, 2, 0},
		{500, 50, 4, 0},  /* 空格 */
		{201, 50, 4, 0},  /* 两张照片之间的间隙 */
		{0, 0, 4, 0},     /* 左上边距 */
		{100, 70, 4, 0},  /* 第一张照片 */
		{55, 55, 4, 1},   /* 返回 */
		{300, 70, 6, 1},  /* 第二张照片 */
		{55, 55, 6, 2},
		{82, 438, 8, 2},  /* 图库 */
		{222, 438, 10, 2},
		{700, 438, 12, 2}
	};
	const struct touch_step empty[] = {
		{222, 438, 2, 0},
		{100, 70, 2, 0},  /* 没有照片收藏时，左上格不能进入大图 */
		{700, 438, 2, 0}
	};

	favorite_records = "/mnt/udisk/music/song.mp3\n"
		"/mnt/udisk/video/movie.avi\n"
		"/mnt/udisk/photo/first.bmp\n"
		"/mnt/udisk/music/other.mp3\n"
		"/mnt/udisk/photo/second.bmp\n";
	for (i = 0; i < 25; i++)
		run_session(reenter, sizeof(reenter) / sizeof(reenter[0]));
	run_session(mixed, sizeof(mixed) / sizeof(mixed[0]));
	favorite_records = "/mnt/udisk/music/song.mp3\n/mnt/udisk/video/movie.avi\n";
	run_session(empty, sizeof(empty) / sizeof(empty[0]));
	favorite_records = "";
	run_session(empty, sizeof(empty) / sizeof(empty[0]));
	puts("PASS: 25 reentries, mixed favorites, left-to-right layout, blank taps, empty favorites");
	return 0;
}
