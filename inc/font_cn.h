#ifndef __FONT_CN_H_
#define __FONT_CN_H_

/* GB2312 中文点阵：HZK16 兼容布局，87区 x 94位，每字32字节。
 * GB2312 双字节 b1(0xA1..0xF7)、b2(0xA1..0xFE)：
 *   区 = b1 - 0xA0 (1..87)，位 = b2 - 0xA0 (1..94)。
 * 每字 16 行 x 2 字节，每字节一行，高位(bit7)在最左。 */
#define FONT_CN_ZONES 87
#define FONT_CN_POS 94
#define FONT_CN_W 16
#define FONT_CN_H 16

extern const unsigned char font_cn16[FONT_CN_ZONES][FONT_CN_POS][32];

#endif
