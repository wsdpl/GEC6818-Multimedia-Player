#ifndef __READDIR_H_
#define __READDIR_H_

#include <stddef.h>

/*
 * 读取目录中指定后缀的普通文件名。
 * suffix 只传入：.mp3、.avi 或 .bmp。
 * 返回值是动态数组，file_count 保存数组元素个数。
 */
char **read_file_names(const char *dir_path, const char *suffix,
		size_t *file_count);

/* 释放 read_file_names 返回的数组。 */
void free_file_names(char **file_names, size_t file_count);

#endif
