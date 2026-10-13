#pragma once
#include <sys/types.h>
#define FS_FATFS 1
#define FS_MOUNT_FLAG_NO_FORMAT 2
#define FS_MOUNT_FLAG_USE_DISK_ACCESS 4
#define FS_O_CREATE 8
#define FS_O_WRITE 16
struct fs_mount_t { int type;void *fs_data;const char *mnt_point;unsigned flags; };
struct fs_file_t { int unused;unsigned offset; };
struct fs_dirent { int type;unsigned size; };
static inline void fs_file_t_init(struct fs_file_t *f) { f->unused=0; }
int fs_mount(struct fs_mount_t *);
int fs_unmount(struct fs_mount_t *);
int fs_mkdir(const char *);
int fs_stat(const char *,struct fs_dirent *);
int fs_open(struct fs_file_t *,const char *,int);
int fs_sync(struct fs_file_t *);
int fs_close(struct fs_file_t *);
ssize_t fs_write(struct fs_file_t *,const void *,size_t);

#define FS_DIR_ENTRY_FILE 1
#define FS_O_READ 32
#define FS_SEEK_SET 0
int fs_seek(struct fs_file_t *,off_t,int);
ssize_t fs_read(struct fs_file_t *,void *,size_t);
