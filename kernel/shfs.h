/* FalconOS — RAM-backed hierarchical VFS for Terminal + Files + desktop */
#ifndef KERNEL_SHFS_H
#define KERNEL_SHFS_H

#include "falcon.h"

#define SHFS_MAX_ENTRIES 48
#define SHFS_FBYTES      512
#define SHFS_PATH       56

typedef struct {
    bool used;
    bool is_dir;
    char path[SHFS_PATH];
    u32  len;
    char data[SHFS_FBYTES];
} shfs_ent_t;

extern char shfs_cwd[SHFS_PATH];

void shfs_init(void);

bool shfs_abs_from(const char *cwd, const char *rel, char *out, i32 cap);
shfs_ent_t *shfs_lookup(const char *abs_path);
shfs_ent_t *shfs_lookup_rel(const char *cwd, const char *rel);

bool shfs_mkdir_abs(const char *abs_path);
bool shfs_touch_abs(const char *abs_path);
bool shfs_rm_abs(const char *abs_path);
bool shfs_rename_abs(const char *from_abs, const char *to_abs);

/* Allocate writable file by absolute path (truncate unless append). */
shfs_ent_t *shfs_open_w_abs(const char *abs_path, bool append);

/* Direct children of directory abs_dir (no recursion). Calls cb(name, is_dir). */
void shfs_list_children(const char *abs_dir,
                        void (*cb)(const char *name, bool is_dir, void *ud),
                        void *ud);

i32 shfs_count_children(const char *abs_dir);

/* Desktop helpers (Turkish UI uses "masaüstü") */
#define SHFS_DESKTOP "/home/falcon/Desktop"
bool shfs_desktop_mkdir(const char *basename);
bool shfs_desktop_touch(const char *basename);
bool shfs_desktop_rename(const char *old_base, const char *new_base);

void shfs_foreach_path(void (*cb)(const char *path, bool is_dir, u32 len, void *ud), void *ud);
void shfs_format_ls(const char *cwd, char *out, i32 cap);

shfs_ent_t *shfs_open_w_rel(const char *cwd, const char *rel, bool append);

void shfs_paths_dump(char *out, i32 cap);
void shfs_du_dump(char *out, i32 cap);

#endif
