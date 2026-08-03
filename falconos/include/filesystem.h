/*
 * FalconOS Virtual File System
 * High-performance VFS with journaling, caching, and multiple backend support
 */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PATH_LENGTH 4096
#define MAX_MOUNT_POINTS 32
#define FS_CACHE_SIZE (16 * 1024 * 1024) // 16MB cache

typedef enum {
    FS_TYPE_EXT4,
    FS_TYPE_FAT32,
    FS_TYPE_NTFS,
    FS_TYPE_TMPFS,
    FS_TYPE_PROC,
    FS_TYPE_SYSFS
} fs_type_t;

typedef struct {
    char name[256];
    fs_type_t type;
    char mount_point[MAX_PATH_LENGTH];
    char device_path[MAX_PATH_LENGTH];
    uint64_t total_size;
    uint64_t used_size;
    int is_read_only;
    int is_mounted;
} mount_point_t;

typedef struct {
    char filename[256];
    uint64_t size;
    uint64_t created_time;
    uint64_t modified_time;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    int is_directory;
    int is_symlink;
    char symlink_target[MAX_PATH_LENGTH];
} file_info_t;

typedef struct {
    int fd;
    char path[MAX_PATH_LENGTH];
    uint64_t position;
    int flags;
    mount_point_t* mount;
} file_descriptor_t;

// Filesystem initialization
int init_filesystem();

// Mount management
int mount_device(const char* device, const char* mount_point, fs_type_t type);
int unmount_device(const char* mount_point);
mount_point_t* get_mount_point(const char* path);

// File operations
int fs_open(const char* path, int flags);
int fs_close(int fd);
int fs_read(int fd, void* buffer, size_t count);
int fs_write(int fd, const void* buffer, size_t count);
int fs_seek(int fd, int64_t offset, int whence);

// Directory operations
int fs_mkdir(const char* path, uint32_t permissions);
int fs_rmdir(const char* path);
int fs_readdir(int fd, file_info_t* info);

// File metadata
int fs_stat(const char* path, file_info_t* info);
int fs_chmod(const char* path, uint32_t permissions);
int fs_chown(const char* path, uint32_t uid, uint32_t gid);

// File system utilities
int fs_remove(const char* path);
int fs_rename(const char* old_path, const char* new_path);
int fs_copy(const char* src, const char* dst);

// Cache management
void fs_flush_cache();
void fs_invalidate_cache(const char* path);

// Special filesystems
int init_proc_fs();
int init_sysfs();
int init_tmpfs();

#endif // FILESYSTEM_H
