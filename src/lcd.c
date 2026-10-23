#include "lcd.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>

//全局变量定义
int lcd_fd = -1;
int *mmap_p = NULL;

static uint32_t read_le32(const unsigned char *buffer)
{
	return (uint32_t)buffer[0] |
		((uint32_t)buffer[1] << 8) |
		((uint32_t)buffer[2] << 16) |
		((uint32_t)buffer[3] << 24);
}

static int read_all(int fd, void *buffer, size_t size)
{
	unsigned char *data = buffer;
	size_t total = 0;

	while(total < size)
	{
		ssize_t length = read(fd, data + total, size - total);
		if(length < 0 && errno == EINTR)
			continue;
		if(length <= 0)
			return -1;
		total += (size_t)length;
	}

	return 0;
}



//初始化LCD
void init_lcd(void)
{
    //步骤：
    //打开LCD
    lcd_fd = open(LCD_PATH, O_RDWR);//字符串---->匿名数组--->代表首元素的地址    '/'的地址  char
    if(lcd_fd == -1)
    {
        printf("LCD打开失败\n");
        return ;
    }

    //内存映射
    mmap_p = mmap(NULL, 800*480*4, PROT_READ | PROT_WRITE, MAP_SHARED, lcd_fd, 0);
	if(mmap_p == MAP_FAILED)
    {
        printf("内存映射失败\n");
		mmap_p = NULL;
		close(lcd_fd);
		lcd_fd = -1;
        return ;
    }
}

//指定起点显示图片  由用户自己指定
//x0,y0用户自己指定的起点
void show_bmp(int x0, int y0, char *bmp)
{
	int bmp_fd = -1;
	unsigned char head_buf[54];
	unsigned char *bmp_buf = NULL;
	int32_t width;
	int32_t height_signed;
	int64_t height_value;
	int height;
	int top_down;
	uint16_t bits_per_pixel;
	uint32_t data_offset;
	uint32_t compression;
	size_t row_size;
	size_t data_size;
	uint32_t *framebuffer;
	int y;

	//以只读方式打开图片
	bmp_fd = open(bmp, O_RDONLY);
	if(bmp_fd == -1)
    {
        printf("图片打开失败\n");
        return ;
    }

	if(read_all(bmp_fd, head_buf, sizeof(head_buf)) == -1 ||
		head_buf[0] != 'B' || head_buf[1] != 'M' ||
		read_le32(head_buf + 14) < 40)
    {
		printf("BMP文件头无效\n");
		goto done;
    }

	width = (int32_t)read_le32(head_buf + 18);
	height_signed = (int32_t)read_le32(head_buf + 22);
	bits_per_pixel = (uint16_t)head_buf[28] |
		((uint16_t)head_buf[29] << 8);
	compression = read_le32(head_buf + 30);
	data_offset = read_le32(head_buf + 10);
	height_value = height_signed;
	top_down = height_value < 0;
	if(top_down)
		height_value = -height_value;

	if(width <= 0 || height_value <= 0 ||
		bits_per_pixel != 24 || compression != 0)
    {
		printf("仅支持未压缩的24位BMP图片\n");
		goto done;
    }

	if(x0 < 0 || y0 < 0 || x0 > 800 || y0 > 480 ||
		width > 800 - x0 || height_value > 480 - y0)
	{
		printf("图片将越界\n");
		goto done;
	}
	height = (int)height_value;

	if(data_offset < sizeof(head_buf) || lseek(bmp_fd, data_offset, SEEK_SET) == -1)
	{
		printf("BMP像素数据位置无效\n");
		goto done;
	}

	/* BMP每行按4字节对齐，按整行读取以保留行尾填充字节。 */
	row_size = ((size_t)width * 3 + 3) & ~(size_t)3;
	data_size = row_size * (size_t)height;
	bmp_buf = malloc(data_size);
	if(bmp_buf == NULL)
	{
		printf("图片内存申请失败\n");
		goto done;
	}
	if(read_all(bmp_fd, bmp_buf, data_size) == -1)
	{
		printf("图片像素读取失败\n");
		goto done;
	}

	if(mmap_p == NULL || (void *)mmap_p == MAP_FAILED)
	{
		printf("LCD内存映射不可用\n");
		goto done;
	}

	framebuffer = (uint32_t *)mmap_p;
	for(y = 0; y < height; y++)
	{
		const unsigned char *src = bmp_buf + (size_t)y * row_size;
		int screen_y = y0 + (top_down ? y : height - 1 - y);
		uint32_t *dst = framebuffer + (size_t)screen_y * 800 + x0;
		int x;

		for(x = 0; x < width; x++)
		{
			dst[x] = (uint32_t)src[0] |
				((uint32_t)src[1] << 8) |
				((uint32_t)src[2] << 16);
			src += 3;
		}
	}

done:
	free(bmp_buf);
	close(bmp_fd);
}

/* 按比例缩放BMP并居中显示在指定区域内。 */
void show_bmp_fit(int x0, int y0, int box_width, int box_height,
	const char *bmp)
{
	int bmp_fd = -1;
	unsigned char head_buf[54];
	unsigned char *bmp_buf = NULL;
	int32_t width;
	int32_t height_signed;
	int64_t height_value;
	int height;
	int top_down;
	uint16_t bits_per_pixel;
	uint32_t data_offset;
	uint32_t compression;
	size_t row_size;
	size_t data_size;
	int draw_width;
	int draw_height;
	int draw_x;
	int draw_y;
	uint32_t *framebuffer;
	int y;

	if(bmp == NULL || box_width <= 0 || box_height <= 0 ||
		x0 < 0 || y0 < 0 || x0 > 800 || y0 > 480 ||
		box_width > 800 - x0 || box_height > 480 - y0)
	{
		printf("BMP显示区域无效\n");
		return;
	}

	bmp_fd = open(bmp, O_RDONLY);
	if(bmp_fd == -1)
	{
		printf("图片打开失败：%s\n", bmp);
		return;
	}

	if(read_all(bmp_fd, head_buf, sizeof(head_buf)) == -1 ||
		head_buf[0] != 'B' || head_buf[1] != 'M' ||
		read_le32(head_buf + 14) < 40)
	{
		printf("BMP文件头无效：%s\n", bmp);
		goto done;
	}

	width = (int32_t)read_le32(head_buf + 18);
	height_signed = (int32_t)read_le32(head_buf + 22);
	bits_per_pixel = (uint16_t)head_buf[28] |
		((uint16_t)head_buf[29] << 8);
	compression = read_le32(head_buf + 30);
	data_offset = read_le32(head_buf + 10);
	height_value = height_signed;
	top_down = height_value < 0;
	if(top_down)
		height_value = -height_value;

	if(width <= 0 || height_value <= 0 || height_value > INT_MAX ||
		bits_per_pixel != 24 || compression != 0)
	{
		printf("仅支持未压缩的24位BMP图片：%s\n", bmp);
		goto done;
	}
	height = (int)height_value;

	/* 防止异常尺寸导致行长和像素缓冲区大小溢出。 */
	if((size_t)width > ((size_t)-1 - 3) / 3)
	{
		printf("BMP图片尺寸无效：%s\n", bmp);
		goto done;
	}
	row_size = ((size_t)width * 3 + 3) & ~(size_t)3;
	if((size_t)height > (size_t)-1 / row_size)
	{
		printf("BMP图片尺寸无效：%s\n", bmp);
		goto done;
	}
	data_size = row_size * (size_t)height;

	if(data_offset < sizeof(head_buf) ||
		lseek(bmp_fd, (off_t)data_offset, SEEK_SET) == (off_t)-1)
	{
		printf("BMP像素数据位置无效：%s\n", bmp);
		goto done;
	}

	bmp_buf = malloc(data_size);
	if(bmp_buf == NULL)
	{
		printf("图片内存申请失败\n");
		goto done;
	}
	if(read_all(bmp_fd, bmp_buf, data_size) == -1)
	{
		printf("图片像素读取失败：%s\n", bmp);
		goto done;
	}

	if(mmap_p == NULL || (void *)mmap_p == MAP_FAILED)
	{
		printf("LCD内存映射不可用\n");
		goto done;
	}

	/* 比较宽高比例，确保图片完整显示且不拉伸。 */
	if((int64_t)box_width * height <= (int64_t)box_height * width)
	{
		draw_width = box_width;
		draw_height = (int)((int64_t)height * box_width / width);
	}
	else
	{
		draw_height = box_height;
		draw_width = (int)((int64_t)width * box_height / height);
	}
	if(draw_width < 1)
		draw_width = 1;
	if(draw_height < 1)
		draw_height = 1;
	draw_x = x0 + (box_width - draw_width) / 2;
	draw_y = y0 + (box_height - draw_height) / 2;

	framebuffer = (uint32_t *)mmap_p;
	for(y = 0; y < draw_height; y++)
	{
		int source_y = (int)((int64_t)y * height / draw_height);
		int file_y = top_down ? source_y : height - 1 - source_y;
		const unsigned char *src = bmp_buf + (size_t)file_y * row_size;
		uint32_t *dst = framebuffer + (size_t)(draw_y + y) * 800 + draw_x;
		int x;

		for(x = 0; x < draw_width; x++)
		{
			int source_x = (int)((int64_t)x * width / draw_width);
			const unsigned char *pixel = src + (size_t)source_x * 3;

			dst[x] = (uint32_t)pixel[0] |
				((uint32_t)pixel[1] << 8) |
				((uint32_t)pixel[2] << 16);
		}
	}

done:
	free(bmp_buf);
	close(bmp_fd);
}

//关闭LCD
void uinit_lcd(void)
{
	//解除内存映射
	if(mmap_p != NULL && (void *)mmap_p != MAP_FAILED)
		munmap(mmap_p, 800*480*4);
	if(lcd_fd != -1)
		close(lcd_fd);
	mmap_p = NULL;
	lcd_fd = -1;
}

int show_small_bmp(int x0, int y0, char* bmp_path)
{
    int bmp_fd = open(bmp_path, O_RDONLY);
    if (bmp_fd < 0)
    {
        perror("show_small_bmp open failed");
        printf("文件路径：%s\n", bmp_path);
        return -1;
    }
    char head[54] = { 0 };
    read(bmp_fd, head, 54);
    int w = head[18] | head[19] << 8 | head[20] << 16 | head[21] << 24;
    int h = head[22] | head[23] << 8 | head[24] << 16 | head[25] << 24;
    int bitcnt = head[28] | head[29] << 8;
    printf("图片：%s  w=%d h=%d bit=%d\n", bmp_path, w, h, bitcnt);

    if (x0 + w > 800 || y0 + h > 480)
    {
        printf("图片越界\n");
        close(bmp_fd);
        return -1;
    }

    int bytes_per_pixel = bitcnt / 8;
    int row_pad = (4 - ((w * bytes_per_pixel) % 4)) % 4;
    char buf[w * 4 + 4];

    //BMP底层存储：y=0对应图片最底部，y=h‑1对应图片顶部
    for (int y = 0; y < h; y++)
    {
        read(bmp_fd, buf, w * bytes_per_pixel + row_pad);
        //修正倒置：BMP的y行，映射到屏幕y坐标
        int y_draw = y0 + (h - 1 - y);
        for (int x = 0; x < w; x++)
        {
            unsigned char b = buf[x * bytes_per_pixel + 0];
            unsigned char g = buf[x * bytes_per_pixel + 1];
            unsigned char r = buf[x * bytes_per_pixel + 2];

            //黑色过滤
            if (r == 0 && g == 0 && b == 0)
                continue;

            int color = b | (g << 8) | (r << 16);
            lcd_draw_point(x0 + x, y_draw, color);
        }
    }
    close(bmp_fd);
    return w;
}

void lcd_draw_point(int x, int y, int color)
{
    if (x < 0 || x >= 800 || y < 0 || y >= 480)
        return;
    *(mmap_p + 800 * y + x) = color;
}