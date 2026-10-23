#ifndef __LCD_H_
#define __LCD_H_


//其他头文件
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

//宏定义
#define LCD_PATH "/dev/fb0"

//全局变量声明
extern int lcd_fd;
extern int *mmap_p;

//函数声明
void init_lcd(void);
void show_bmp(int x0, int y0, char *bmp);
void show_bmp_fit(int x0, int y0, int box_width, int box_height,
	const char *bmp);
int show_small_bmp(int x0, int y0, char* bmp_path);
void uinit_lcd(void);
void lcd_draw_point(int x, int y, int color);

#endif
