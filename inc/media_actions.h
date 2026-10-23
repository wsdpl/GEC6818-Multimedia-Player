#ifndef __MEDIA_ACTIONS_H_
#define __MEDIA_ACTIONS_H_

#include <stddef.h>

/* 收藏记录以完整路径保存在 /mnt/udisk/favorites.txt。 */
int media_list_favorites(char ***favorite_paths, size_t *favorite_count);
/* 只列出指定目录下的收藏完整路径（音乐/视频收藏夹使用），用 media_free_favorites 释放 */
int media_list_favorites_in_dir(const char *dir_path,
	char ***favorite_paths, size_t *favorite_count);
void media_free_favorites(char **favorite_paths, size_t favorite_count);

/* 查询返回 1（已收藏）、0（未收藏）或 -1（读取失败）。 */
int media_is_favorite(const char *file_path);

/* 设置或切换收藏状态，成功返回新的状态，失败返回 -1。 */
int media_set_favorite(const char *file_path, int is_favorite);
int media_toggle_favorite(const char *file_path);

/* 删除文件，并尽力从收藏列表中清除对应路径。成功返回 0。 */
int media_delete_file(const char *file_path);

#endif
