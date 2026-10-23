#include "aod.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

int aod_mode_flag = 0;
int skip_this_touch = 0;
static int orig_buf[800 * 480];
static int dim_buf[800 * 480];

static char* num_file[10] = {
    "/mnt/udisk/00.bmp",
    "/mnt/udisk/01.bmp",
    "/mnt/udisk/02.bmp",
    "/mnt/udisk/03.bmp",
    "/mnt/udisk/04.bmp",
    "/mnt/udisk/05.bmp",
    "/mnt/udisk/06.bmp",
    "/mnt/udisk/07.bmp",
    "/mnt/udisk/08.bmp",
    "/mnt/udisk/09.bmp"
};
static char* colon_file = "/mnt/udisk/0m.bmp";

static int num_width[10] = {
    89, //00.bmp宽度
    51, //01.bmp宽度
    85, //02.bmp宽度
    85, //03.bmp宽度
    96, //04.bmp宽度
    95, //05.bmp宽度
    89, //06.bmp宽度
    80, //07.bmp宽度
    97, //08.bmp宽度
    85  //09.bmp宽度
};
static int colon_width = 53; //0m.bmp冒号图片实际宽度

extern void lcd_draw_point(int x, int y, int color);
extern int show_small_bmp(int x0, int y0, char* bmp_path);
extern int* mmap_p;

void aod_prepare_bg(void)
{
    int i;
    memmove(orig_buf, mmap_p, LCD_W * LCD_H * 4);
    for (i = 0;i < LCD_W * LCD_H;i++)
    {
        int color = orig_buf[i];
        unsigned char r = (color >> 16) & 0xff;
        unsigned char g = (color >> 8) & 0xff;
        unsigned char b = color & 0xff;
        const float ratio = 0.15f;
        r = (unsigned char)(r * ratio);
        g = (unsigned char)(g * ratio);
        b = (unsigned char)(b * ratio);
        dim_buf[i] = (0x00 << 24) | (r << 16) | (g << 8) | b;
    }
    printf("AOD预计算低亮背景完成\n");
}

void draw_aod_ui(void)
{
    time_t now;
    time(&now);
    //板子快93分钟，减去93*60 =5580秒
    now = now - 5580;
    struct tm* t;
    t = localtime(&now);
    if (t == NULL)
    {
        printf("localtime 获取时间失败!\n");
        return;
    }
    int h1 = t->tm_hour / 10;
    int h2 = t->tm_hour % 10;
    int m1 = t->tm_min / 10;
    int m2 = t->tm_min % 10;
    printf("AOD时间：%02d:%02d （已经减去93分钟时差）\n", t->tm_hour, t->tm_min);
    const int gap = 18;   //加大间隙，防止冒号被遮挡
    const int start_y = 60;
    //计算整套HH:MM画面总宽度，用于居中
    int total_w = num_width[h1] + gap + num_width[h2] + gap + colon_width + gap + num_width[m1] + gap + num_width[m2];
    int cur_x = (LCD_W - total_w) / 2; //自动居中起始X
    show_small_bmp(cur_x, start_y, num_file[h1]);
    cur_x += num_width[h1] + gap;
    show_small_bmp(cur_x, start_y, num_file[h2]);
    cur_x += num_width[h2] + gap;
    show_small_bmp(cur_x, start_y, colon_file);
    cur_x += colon_width + gap;
    show_small_bmp(cur_x, start_y, num_file[m1]);
    cur_x += num_width[m1] + gap;
    show_small_bmp(cur_x, start_y, num_file[m2]);
}


void enter_aod(void)
{
    if (aod_mode_flag == 1)
    {
        printf("enter_aod:已经处于AOD模式，忽略\n");
        return;
    }
    aod_mode_flag = 1;
    skip_this_touch = 1;
    printf("====进入AOD全天候模式====\n");
    memmove(mmap_p, dim_buf, LCD_W * LCD_H * 4);
    draw_aod_ui();
}

void exit_aod(void)
{
    if (aod_mode_flag == 0)
    {
        printf("exit_aod:不在AOD模式，忽略\n");
        return;
    }
    aod_mode_flag = 0;
    skip_this_touch = 1;
    printf("====退出AOD全天候模式====\n");
    memmove(mmap_p, orig_buf, LCD_W * LCD_H * 4);
}

void aod_touch_handler(int x1, int y1, int x2, int y2)
{
    int dy = y2 - y1;
    printf("AOD手势检测 y1=%d dy=%d aod_mode=%d\n", y1, dy, aod_mode_flag);
    if (aod_mode_flag == 0)
    {
        if (y1 <= 130 && dy > HALF_Y)
        {
            enter_aod();
        }
    }
    else
    {
        if (y1 >= 350 && dy < -HALF_Y)
        {
            exit_aod();
        }
    }
}
