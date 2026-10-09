/* FalconOS — hierarchical shfs (shell filesystem) */
#include "shfs.h"

char shfs_cwd[SHFS_PATH] = "/home/falcon";

static bool shfs_path_ok(const char *s);
static shfs_ent_t G[SHFS_MAX_ENTRIES];
static u32 G_REV[SHFS_MAX_ENTRIES];
static void touch_slot(shfs_ent_t *e) { if (e) G_REV[e - G]++; }
shfs_ent_t *shfs_slot(i32 i) { return (i >= 0 && i < SHFS_MAX_ENTRIES) ? &G[i] : NULL; }
u32 shfs_slot_revision(i32 i) { return (i >= 0 && i < SHFS_MAX_ENTRIES) ? G_REV[i] : 0; }
/* Disk replay does not mark records dirty or trigger recursive writes. */
bool shfs_replay_slot(i32 i, const char *path, bool is_dir, const char *data, u32 len) {
    if (i < 0 || i >= SHFS_MAX_ENTRIES || !path || !path[0]) return false;
    if (!shfs_path_ok(path) || len >= SHFS_FBYTES || (is_dir && len)) return false;
    shfs_ent_t *e = &G[i];
    e->used = true; e->is_dir = is_dir; e->len = len;
    k_strcpy(e->path, path);
    if (len && data) k_memcpy(e->data, data, len);
    e->data[len] = 0;
    G_REV[i] = 0;
    return true;
}
void shfs_replay_deleted(i32 i) {
    if (i < 0 || i >= SHFS_MAX_ENTRIES) return;
    G[i].used = false; G[i].path[0] = 0; G[i].len = 0;
    G[i].data[0] = 0; G_REV[i] = 0;
}

static i32 shfs_path_len(const char *p)
{
    i32 n = 0;
    while (p[n]) n++;
    return n;
}

static bool shfs_starts_with(const char *s, const char *pref)
{
    i32 i = 0;
    while (pref[i]) {
        if (s[i] != pref[i]) return false;
        i++;
    }
    return true;
}

/* Refuse overlong paths BEFORE any k_strcpy into fixed SHFS_PATH buffers. */
static bool shfs_path_ok(const char *s)
{
    if (!s || s[0] != '/') return false;
    i32 n = 0, segment_start = 1;
    while (s[n]) {
        if (n >= SHFS_PATH - 1) return false;
        if (s[n] == '/' && n > 0) {
            if (n == segment_start || (n == segment_start + 1 && s[segment_start] == '.') ||
                (n == segment_start + 2 && s[segment_start] == '.' &&
                 s[segment_start + 1] == '.')) return false;
            segment_start = n + 1;
        }
        n++;
    }
    if (n == 0 || (n > 1 && s[n-1] == '/')) return false;
    if ((n == segment_start + 1 && s[segment_start] == '.') ||
        (n == segment_start + 2 && s[segment_start] == '.' &&
         s[segment_start + 1] == '.')) return false;
    return true;
}
static bool g_shfs_initialized = false;
void shfs_init(void)
{
    if (g_shfs_initialized) return;
    g_shfs_initialized = true;
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        G_REV[i]  = 0;
        G[i].used   = false;
        G[i].is_dir = false;
        G[i].path[0] = 0;
        G[i].len    = 0;
        G[i].data[0] = 0;
    }
    k_strcpy(shfs_cwd, "/home/falcon");

    k_strcpy(G[0].path, "/home/falcon/readme.txt");
    k_strcpy(G[0].data,
             "FalconOS 1 — hoş geldiniz / welcome.\n\n"
             "shfs artık /home/falcon hiyerarşisini destekler; "
             "Desktop: /home/falcon/Desktop\n");
    G[0].len  = k_strlen(G[0].data);
    G[0].used = true;
    G[0].is_dir = false;

    k_strcpy(G[1].path, "/home/falcon/hello.sh");
    k_strcpy(G[1].data, "echo Merhaba FalconOS — hello FalconOS\n");
    G[1].len  = k_strlen(G[1].data);
    G[1].used = true;
    G[1].is_dir = false;

    k_strcpy(G[2].path, "/home/falcon/Desktop");
    G[2].len    = 0;
    G[2].data[0]= 0;
    G[2].used   = true;
    G[2].is_dir = true;

    k_strcpy(G[3].path, "/home/falcon/.prg");
    G[3].len    = 0;
    G[3].data[0]= 0;
    G[3].used   = true;
    G[3].is_dir = true;
}

bool shfs_abs_from(const char *cwd, const char *rel, char *out, i32 cap)
{
    if (!rel || !rel[0] || cap < 4) return false;
    if (rel[0] == '/') {
        i32 i = 0;
        while (rel[i] && i < cap - 1) { out[i] = rel[i]; i++; }
        out[i] = 0;
        return !rel[i] && shfs_path_ok(out);
    }
    if (k_strcmp(rel, ".") == 0) {
        k_strcpy(out, cwd);
        return (i32)k_strlen(out) < cap && shfs_path_ok(out);
    }
    if (k_strcmp(rel, "..") == 0) {
        k_strcpy(out, cwd);
        i32 n = shfs_path_len(out);
        while (n > 1 && out[n - 1] != '/') n--;
        if (n > 1) { out[n - 1] = 0; } else { out[1] = 0; }
        return shfs_path_ok(out);
    }
    i32 cn = shfs_path_len(cwd);
    if (cn + 1 + shfs_path_len(rel) >= cap) return false;
    k_strcpy(out, cwd);
    if (out[cn - 1] != '/') { out[cn] = '/'; out[cn + 1] = 0; cn++; }
    k_strcat(out, rel);
    return shfs_path_ok(out);
}

shfs_ent_t *shfs_lookup(const char *abs_path)
{
    if (!abs_path) return NULL;
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++)
        if (G[i].used && k_strcmp(G[i].path, abs_path) == 0)
            return &G[i];
    return NULL;
}

shfs_ent_t *shfs_lookup_rel(const char *cwd, const char *rel)
{
    char tmp[SHFS_PATH];
    if (!shfs_abs_from(cwd, rel, tmp, sizeof tmp)) return NULL;
    return shfs_lookup(tmp);
}

static shfs_ent_t *alloc_slot(void)
{
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++)
        if (!G[i].used) return &G[i];
    return NULL;
}

bool shfs_mkdir_abs(const char *abs_path)
{
    if (!abs_path || !abs_path[0]) return false;
    if (!shfs_path_ok(abs_path)) return false;
    if (shfs_lookup(abs_path)) return true;
    shfs_ent_t *e = alloc_slot();
    if (!e) return false;
    k_strcpy(e->path, abs_path);
    e->len     = 0;
    e->data[0] = 0;
    e->is_dir  = true;
    e->used    = true;
    touch_slot(e);
    return true;
}

bool shfs_touch_abs(const char *abs_path)
{
    if (!shfs_path_ok(abs_path)) return false;
    shfs_ent_t *e = shfs_lookup(abs_path);
    if (e) {
        if (e->is_dir) return false;
        return true;
    }
    e = alloc_slot();
    if (!e) return false;
    k_strcpy(e->path, abs_path);
    e->len     = 0;
    e->data[0] = 0;
    e->is_dir  = false;
    e->used    = true;
    touch_slot(e);
    return true;
}

shfs_ent_t *shfs_open_w_abs(const char *abs_path, bool append)
{
    if (!shfs_path_ok(abs_path)) return NULL;
    shfs_ent_t *f = shfs_lookup(abs_path);
    if (!f) {
        f = alloc_slot();
        if (!f) return NULL;
        f->used = true;
        k_strcpy(f->path, abs_path);
        f->is_dir = false;
        f->len = 0;
        f->data[0] = 0;
    } else if (f->is_dir) {
        return NULL;
    }
    touch_slot(f);
    if (!append) {
        f->len = 0;
        f->data[0] = 0;
    }
    return f;
}

bool shfs_rm_abs(const char *abs_path)
{
    shfs_ent_t *e = shfs_lookup(abs_path);
    if (!e) return false;
    /* refuse removing non-empty directory */
    if (e->is_dir) {
        for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
            if (!G[i].used || &G[i] == e) continue;
            if (!shfs_starts_with(G[i].path, abs_path)) continue;
            i32 pl = shfs_path_len(abs_path);
            if (G[i].path[pl] != '/') continue;
            /* direct child or deeper */
            const char *rest = G[i].path + pl + 1;
            if (rest[0] == 0) continue;
            return false;
        }
    }
    touch_slot(e);
    e->used = false;
    e->path[0] = 0;
    e->len = 0;
    return true;
}

bool shfs_rename_abs(const char *from_abs, const char *to_abs)
{
    if (!shfs_path_ok(from_abs) || !shfs_path_ok(to_abs)) return false;
    shfs_ent_t *e = shfs_lookup(from_abs);
    if (!e || shfs_lookup(to_abs)) return false;
    /* Check every descendant's new path before modifying any entry. */
    if (e->is_dir) {
        i32 oldn = shfs_path_len(from_abs);
        i32 newn = shfs_path_len(to_abs);
        for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
            if (!G[i].used || &G[i] == e ||
                !shfs_starts_with(G[i].path, from_abs) ||
                G[i].path[oldn] != '/') continue;
            if (newn + shfs_path_len(G[i].path + oldn) >= SHFS_PATH) return false;
        }
    }
    touch_slot(e);
    k_strcpy(e->path, to_abs);
    /* rename children prefixes if directory */
    if (e->is_dir) {
        i32 oldl = shfs_path_len(from_abs);
        for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
            if (!G[i].used || &G[i] == e) continue;
            if (!shfs_starts_with(G[i].path, from_abs)) continue;
            if (G[i].path[oldl] != '/' && G[i].path[oldl] != 0) continue;
            char tmp[SHFS_PATH];
            k_strcpy(tmp, to_abs);
            k_strcat(tmp, G[i].path + oldl);
            k_strcpy(G[i].path, tmp);
            touch_slot(&G[i]);
        }
    }
    return true;
}

void shfs_list_children(const char *abs_dir, void (*cb)(const char *name, bool is_dir, void *ud),
                        void *ud)
{
    if (!cb) return;
    i32 dl = shfs_path_len(abs_dir);
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        if (!G[i].used) continue;
        if (k_strcmp(G[i].path, abs_dir) == 0) continue;
        if (!shfs_starts_with(G[i].path, abs_dir)) continue;
        if (G[i].path[dl] != '/') continue;
        /* direct child only */
        const char *rest = G[i].path + dl + 1;
        i32 j = 0;
        while (rest[j] && rest[j] != '/') j++;
        if (rest[j] == '/') continue;
        char base[32];
        if (j >= (i32)sizeof base) continue;
        k_memcpy(base, rest, (u32)j);
        base[j] = 0;
        cb(base, G[i].is_dir, ud);
    }
}

i32 shfs_count_children(const char *abs_dir)
{
    i32 n = 0;
    i32 dl = shfs_path_len(abs_dir);
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        if (!G[i].used) continue;
        if (k_strcmp(G[i].path, abs_dir) == 0) continue;
        if (!shfs_starts_with(G[i].path, abs_dir)) continue;
        if (G[i].path[dl] != '/') continue;
        const char *rest = G[i].path + dl + 1;
        i32 j = 0;
        while (rest[j] && rest[j] != '/') j++;
        if (rest[j] == '/') continue;
        n++;
    }
    return n;
}

bool shfs_desktop_mkdir(const char *basename)
{
    char p[SHFS_PATH];
    k_strcpy(p, SHFS_DESKTOP);
    k_strcat(p, "/");
    k_strcat(p, basename);
    return shfs_mkdir_abs(p);
}

bool shfs_desktop_touch(const char *basename)
{
    char p[SHFS_PATH];
    k_strcpy(p, SHFS_DESKTOP);
    k_strcat(p, "/");
    k_strcat(p, basename);
    return shfs_touch_abs(p);
}

bool shfs_desktop_rename(const char *old_base, const char *new_base)
{
    char a[SHFS_PATH], b[SHFS_PATH];
    k_strcpy(a, SHFS_DESKTOP); k_strcat(a, "/"); k_strcat(a, old_base);
    k_strcpy(b, SHFS_DESKTOP); k_strcat(b, "/"); k_strcat(b, new_base);
    return shfs_rename_abs(a, b);
}

void shfs_foreach_path(void (*cb)(const char *path, bool is_dir, u32 len, void *ud), void *ud)
{
    if (!cb) return;
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        if (!G[i].used) continue;
        cb(G[i].path, G[i].is_dir, G[i].len, ud);
    }
}

typedef struct {
    char *out;
    i32   cap;
    bool  first;
} shfs_ls_ctx_t;

static void shfs_ls_cb(const char *name, bool is_dir, void *ud)
{
    shfs_ls_ctx_t *c = (shfs_ls_ctx_t *)ud;
    if (!c || !c->out || c->cap < 8) return;
    if (!c->first && (u32)k_strlen(c->out) + 2u < (u32)c->cap)
        k_strcat(c->out, "  ");
    c->first = false;
    if ((u32)k_strlen(c->out) + (u32)k_strlen(name) + 3u >= (u32)c->cap) return;
    k_strcat(c->out, name);
    if (is_dir) k_strcat(c->out, "/");
}

void shfs_format_ls(const char *cwd, char *out, i32 cap)
{
    shfs_ls_ctx_t ctx = { out, cap, true };
    if (!out || cap < 4) return;
    out[0] = 0;
    shfs_list_children(cwd, shfs_ls_cb, &ctx);
}

shfs_ent_t *shfs_open_w_rel(const char *cwd, const char *rel, bool append)
{
    char abs[SHFS_PATH];
    if (!shfs_abs_from(cwd, rel, abs, sizeof abs)) return NULL;
    return shfs_open_w_abs(abs, append);
}

void shfs_paths_dump(char *out, i32 cap)
{
    if (!out || cap < 8) return;
    out[0] = 0;
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        if (!G[i].used) continue;
        if ((u32)k_strlen(out) + (u32)k_strlen(G[i].path) + 2u >= (u32)cap) break;
        k_strcat(out, G[i].path);
        k_strcat(out, "\n");
    }
}

void shfs_du_dump(char *out, i32 cap)
{
    if (!out || cap < 16) return;
    out[0] = 0;
    u32 total = 0;
    char num[16];
    for (i32 i = 0; i < SHFS_MAX_ENTRIES; i++) {
        if (!G[i].used || G[i].is_dir) continue;
        total += G[i].len;
        k_itoa(G[i].len, num, 10);
        if ((u32)k_strlen(out) + (u32)k_strlen(G[i].path) + (u32)k_strlen(num) + 8u >= (u32)cap) break;
        k_strcat(out, num);
        k_strcat(out, "\t");
        k_strcat(out, G[i].path);
        k_strcat(out, "\n");
    }
    k_itoa(total, num, 10);
    k_strcat(out, num);
    k_strcat(out, "\ttotal");
}
