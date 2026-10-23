#include "media_actions.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAVORITES_FILE "/mnt/udisk/favorites.txt"
#define FAVORITES_TEMP_FILE "/mnt/udisk/.favorites.txt.tmp"

static int valid_file_path(const char *file_path)
{
	return file_path != NULL && file_path[0] != '\0' &&
		strlen(file_path) < PATH_MAX &&
		strchr(file_path, '\n') == NULL && strchr(file_path, '\r') == NULL;
}

static char *copy_string(const char *text)
{
	char *copy = malloc(strlen(text) + 1);

	if(copy != NULL)
		strcpy(copy, text);
	return copy;
}

void media_free_favorites(char **favorite_paths, size_t favorite_count)
{
	size_t i;

	if(favorite_paths == NULL)
		return;

	for(i = 0; i < favorite_count; i++)
		free(favorite_paths[i]);
	free(favorite_paths);
}

int media_list_favorites(char ***favorite_paths, size_t *favorite_count)
{
	FILE *file;
	char line[PATH_MAX + 2];

	if(favorite_paths == NULL || favorite_count == NULL)
		return -1;

	*favorite_paths = NULL;
	*favorite_count = 0;
	file = fopen(FAVORITES_FILE, "r");
	if(file == NULL)
		return errno == ENOENT ? 0 : -1;

	while(fgets(line, sizeof(line), file) != NULL)
	{
		size_t length = strlen(line);
		char *path_copy;
		char **new_paths;

		if(length == sizeof(line) - 1 && line[length - 1] != '\n' &&
			!feof(file))
		{
			int character;
			while((character = fgetc(file)) != '\n' && character != EOF)
				;
			continue;
		}

		if(length > 0 && line[length - 1] == '\n')
			line[--length] = '\0';
		if(length > 0 && line[length - 1] == '\r')
			line[--length] = '\0';
		if(length == 0 || length >= PATH_MAX)
			continue;

		path_copy = copy_string(line);
		if(path_copy == NULL)
			goto read_error;

		new_paths = realloc(*favorite_paths,
			(*favorite_count + 1) * sizeof(**favorite_paths));
		if(new_paths == NULL)
		{
			free(path_copy);
			goto read_error;
		}

		*favorite_paths = new_paths;
		(*favorite_paths)[*favorite_count] = path_copy;
		(*favorite_count)++;
	}

	if(ferror(file))
		goto read_error;
	if(fclose(file) != 0)
	{
		media_free_favorites(*favorite_paths, *favorite_count);
		*favorite_paths = NULL;
		*favorite_count = 0;
		return -1;
	}

	return 0;

read_error:
	fclose(file);
	media_free_favorites(*favorite_paths, *favorite_count);
	*favorite_paths = NULL;
	*favorite_count = 0;
	return -1;
}

/* 读取收藏列表，但只保留位于指定目录下的完整路径（供音乐/视频收藏夹复用）。
 * 返回的数组与 media_list_favorites 一样，用 media_free_favorites 释放。 */
int media_list_favorites_in_dir(const char *dir_path,
	char ***out_paths, size_t *out_count)
{
	char **all_paths = NULL;
	size_t all_count = 0;
	char **filtered = NULL;
	size_t filtered_count = 0;
	size_t dir_len;
	size_t i;

	if(out_paths == NULL || out_count == NULL || dir_path == NULL)
		return -1;

	*out_paths = NULL;
	*out_count = 0;

	if(media_list_favorites(&all_paths, &all_count) != 0)
		return -1;

	dir_len = strlen(dir_path);
	for(i = 0; i < all_count; i++)
	{
		const char *line = all_paths[i];
		char *path_copy;
		char **new_filtered;

		if(strncmp(line, dir_path, dir_len) != 0 || line[dir_len] != '/')
			continue;

		path_copy = copy_string(line);
		if(path_copy == NULL)
		{
			media_free_favorites(filtered, filtered_count);
			media_free_favorites(all_paths, all_count);
			return -1;
		}

		new_filtered = realloc(filtered,
			(filtered_count + 1) * sizeof(*filtered));
		if(new_filtered == NULL)
		{
			free(path_copy);
			media_free_favorites(filtered, filtered_count);
			media_free_favorites(all_paths, all_count);
			return -1;
		}
		filtered = new_filtered;
		filtered[filtered_count++] = path_copy;
	}

	media_free_favorites(all_paths, all_count);
	*out_paths = filtered;
	*out_count = filtered_count;
	return 0;
}

static int write_favorites(char **favorite_paths, size_t favorite_count)
{
	FILE *file;
	size_t i;
	int write_failed = 0;

	file = fopen(FAVORITES_TEMP_FILE, "w");
	if(file == NULL)
		return -1;

	for(i = 0; i < favorite_count; i++)
	{
		if(fprintf(file, "%s\n", favorite_paths[i]) < 0)
		{
			write_failed = 1;
			break;
		}
	}

	if(fclose(file) != 0)
		write_failed = 1;
	if(write_failed || rename(FAVORITES_TEMP_FILE, FAVORITES_FILE) != 0)
	{
		unlink(FAVORITES_TEMP_FILE);
		return -1;
	}

	return 0;
}

int media_is_favorite(const char *file_path)
{
	char **favorite_paths;
	size_t favorite_count;
	size_t i;
	int found = 0;

	if(!valid_file_path(file_path) ||
		media_list_favorites(&favorite_paths, &favorite_count) == -1)
		return -1;

	for(i = 0; i < favorite_count; i++)
	{
		if(strcmp(favorite_paths[i], file_path) == 0)
		{
			found = 1;
			break;
		}
	}

	media_free_favorites(favorite_paths, favorite_count);
	return found;
}

int media_set_favorite(const char *file_path, int is_favorite)
{
	char **favorite_paths;
	size_t favorite_count;
	size_t i;
	size_t found_index = 0;
	int found = 0;
	int desired_state = is_favorite != 0;

	if(!valid_file_path(file_path) ||
		media_list_favorites(&favorite_paths, &favorite_count) == -1)
		return -1;

	for(i = 0; i < favorite_count; i++)
	{
		if(strcmp(favorite_paths[i], file_path) == 0)
		{
			found = 1;
			found_index = i;
			break;
		}
	}

	if(found == desired_state)
	{
		media_free_favorites(favorite_paths, favorite_count);
		return desired_state;
	}

	if(found)
	{
		free(favorite_paths[found_index]);
		for(i = found_index; i + 1 < favorite_count; i++)
			favorite_paths[i] = favorite_paths[i + 1];
		favorite_count--;
	}
	else
	{
		char *path_copy = copy_string(file_path);
		char **new_paths;

		if(path_copy == NULL)
		{
			media_free_favorites(favorite_paths, favorite_count);
			return -1;
		}

		new_paths = realloc(favorite_paths,
			(favorite_count + 1) * sizeof(*favorite_paths));
		if(new_paths == NULL)
		{
			free(path_copy);
			media_free_favorites(favorite_paths, favorite_count);
			return -1;
		}

		favorite_paths = new_paths;
		favorite_paths[favorite_count++] = path_copy;
	}

	if(write_favorites(favorite_paths, favorite_count) == -1)
	{
		media_free_favorites(favorite_paths, favorite_count);
		return -1;
	}

	media_free_favorites(favorite_paths, favorite_count);
	return desired_state;
}

int media_toggle_favorite(const char *file_path)
{
	int current_state = media_is_favorite(file_path);

	if(current_state == -1)
		return -1;
	return media_set_favorite(file_path, !current_state);
}

int media_delete_file(const char *file_path)
{
	if(!valid_file_path(file_path))
	{
		errno = EINVAL;
		perror("删除媒体文件失败");
		return -1;
	}

	if(unlink(file_path) == -1)
	{
		perror("删除媒体文件失败");
		return -1;
	}

	if(media_set_favorite(file_path, 0) == -1)
		fprintf(stderr, "文件已删除，但收藏记录更新失败：%s\n", file_path);

	return 0;
}
