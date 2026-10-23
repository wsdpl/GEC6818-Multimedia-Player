#ifndef __EVENT_H_
#define __EVENT_H_

#include "lcd.h"
#include <linux/input.h>


extern int event_fd;

void init_event(void);
void get_pos(int *x1, int *y1, int *x2, int *y2);
/* 带超时等待一次触摸：返回1拿到坐标，0超时无触摸，-1出错；timeout_ms<0永久等待 */
int get_pos_timeout(int *x1, int *y1, int *x2, int *y2, int timeout_ms);
void close_event(void);



#endif