#include "lcd.h"
#include "event.h"
#include "photo.h"
#include "video.h"
#include "music.h"
#include "aod.h"

int main(int argc, char const *argv[])
{
	
	init_lcd();
	init_event();

	int i = 0;
	int x = -1, y = -1;
	int x1 = -1, y1 = -1;

	show_bmp(0,0,"/mnt/udisk/icons/main.bmp");
	aod_prepare_bg();

	while(1)
	{
		//获取坐标
		get_pos(&x, &y,&x1, &y1);
		aod_touch_handler(x,y,x1,y1);

		if (skip_this_touch == 1)
		{
			skip_this_touch = 0;
			x = -1; y = -1;
			x1 = -1; y1 = -1;
			continue;
		}

		if(aod_mode_flag == 0)
		{
			if(x>=0 && x < 400 && y>=240 && y < 480)
			{
				printf("打开视频\n");
				play_video();
				show_bmp(0,0,"/mnt/udisk/icons/main.bmp");
			}
			if(x>=0 && x < 400 && y>=0 && y < 240)
			{
				printf("打开相册\n");
				play_photo();
				show_bmp(0,0,"/mnt/udisk/icons/main.bmp");
			}
			else if(x>=400 && x < 800 && y>=0 && y < 240)
			{
				printf("打开音乐\n");
				play_music();
				show_bmp(0,0,"/mnt/udisk/icons/main.bmp");
			}
			else if(x>=400 && x < 800 && y>=240 && y < 480)
			{
				break;
			}
		}


	}
	
	//关闭触摸屏
	close_event();
	uinit_lcd();

	return 0;
}