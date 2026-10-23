#include "readdir.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define FILE_NAME_ARRAY_SIZE 8

/* 只允许音乐、视频和图片三种后缀。 */
static int is_supported_suffix(const char *suffix)
{
	return suffix != NULL &&
		(strcasecmp(suffix, ".mp3") == 0 ||
		 strcasecmp(suffix, ".avi") == 0 ||
		 strcasecmp(suffix, ".bmp") == 0);
}

/* 判断文件名是否符合调用者传入的后缀。 */
static int is_match_file(const char *file_name, const char *suffix)
{
	const char *file_suffix = strrchr(file_name, '.');

	return file_suffix != NULL && strcasecmp(file_suffix, suffix) == 0;
}

/* 释放已经申请的部分数组。 */
void free_file_names(char **file_names, size_t file_count)
{
	size_t i;

	if(file_names == NULL)
		return;

	for(i = 0; i < file_count; i++)
		free(file_names[i]);

	free(file_names);
}

char **read_file_names(const char *dir_path, const char *suffix,
		size_t *file_count)
{
	DIR *dir;
	struct dirent *entry;
	struct stat file_stat;
	char **file_names = NULL;
	char full_path[PATH_MAX];
	size_t count = 0;
	size_t capacity = 0;

	if(file_count == NULL)
		return NULL;

	*file_count = 0;

	if(dir_path == NULL || !is_supported_suffix(suffix))
	{
		printf("目录或文件后缀参数错误\n");
		return NULL;
	}

	dir = opendir(dir_path);
	if(dir == NULL)
	{
		printf("打开目录失败：%s（%s）\n", dir_path, strerror(errno));
		return NULL;
	}

	while((entry = readdir(dir)) != NULL)
	{
		int path_length;
		char **new_file_names;

		if(strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		if(!is_match_file(entry->d_name, suffix))
			continue;

		path_length = snprintf(full_path, sizeof(full_path),
				"%s/%s", dir_path, entry->d_name);
		if(path_length < 0 || (size_t)path_length >= sizeof(full_path))
		{
			printf("文件路径过长，跳过：%s\n", entry->d_name);
			continue;
		}

		/* 只收集普通文件，不收集同名目录。 */
		if(stat(full_path, &file_stat) == -1 ||
			!S_ISREG(file_stat.st_mode))
			continue;

		if(count == capacity)
		{
			capacity = capacity == 0 ? FILE_NAME_ARRAY_SIZE : capacity * 2;
			new_file_names = realloc(file_names,
					capacity * sizeof(*file_names));
			if(new_file_names == NULL)
			{
				printf("文件名数组申请内存失败\n");
				free_file_names(file_names, count);
				closedir(dir);
				return NULL;
			}
			file_names = new_file_names;
		}

		file_names[count] = malloc(strlen(entry->d_name) + 1);
		if(file_names[count] == NULL)
		{
			printf("文件名申请内存失败\n");
			free_file_names(file_names, count);
			closedir(dir);
			return NULL;
		}

		strcpy(file_names[count], entry->d_name);
		count++;
	}

	closedir(dir);
	*file_count = count;
	return file_names;
}
