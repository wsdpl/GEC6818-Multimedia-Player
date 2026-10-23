#ifndef __AOD_H__
#define __AOD_H__

#include "lcd.h"

#define LCD_W        800
#define LCD_H        480
#define HALF_Y       240
#define AOD_DIM_RATIO 0.15f

extern int aod_mode_flag;
extern int skip_this_touch;

void aod_prepare_bg(void);
void enter_aod(void);
void exit_aod(void);
void aod_touch_handler(int x1, int y1, int x2, int y2);

#endif
