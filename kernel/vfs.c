/* =============================================================================
 *  FalconOS — Virtual Filesystem Layer  (FalconOS 2.0 Alpha)
 * =============================================================================
 *  This module provides a simple virtual filesystem abstraction:
 *  - Unified interface for different filesystem types
 *  - In-memory filesystem for temporary files
 *  - Disk-backed filesystem persistence
 *  - Path resolution and navigation
 * ============================================================================= */
#include "falcon.h"
#include "version.h"

#if FEATURE_VFS

#define VFS_MAX_PATH        256
#define VFS_MAX_NAME        64
#define VFS_MAX_OPEN_FILES  32
#define VFS_MAX_MOUNT_POINTS 8

/* File types */
typedef enum {
    VFS_TYPE_NONE   = 0,
    VFS_TYPE_FILE   = 1,
    VFS_TYPE_DIR    = 2,
    VFS_TYPE_LINK   = 3,
    VFS_TYPE_DEVICE = 4
} vfs_type_t;

/* File permissions (simplified Unix-style) */
#define VFS_PERM_READ     (1 << 0)
#define VFS_PERM_WRITE    (1 << 1)
#define VFS_PERM_EXEC     (1 << 2)
#define VFS_PERM_USER     (1 << 3)
#define VFS_PERM_GROUP    (1 << 4)
#define VFS_PERM_OTHER    (1 << 5)

/* File node structure */
typedef struct vfs_node {
    char name[VFS_MAX_NAME];
    vfs_type_t type;
    u8 permissions;
    u32 size;
    u32 created_time;
    u32 modified_time;
    u32 parent_id;
    void *data;              /* Pointer to file data (in-memory FS) */
    struct vfs_node *next;   /* Sibling in directory listing */
    struct vfs_node *child;  /* First child (for directories) */
} vfs_node_t;

/* Mount point structure */
typedef struct {
    char path[VFS_MAX_PATH];
    vfs_node_t *root;
    bool is_mounted;
} vfs_mount_t;

/* Open file descriptor */
typedef struct {
    vfs_node_t *node;
    u32 position;
    u32 flags;
    bool is_open;
} vfs_fd_t;

/* Global VFS state */
static vfs_node_t g_vfs_root;
static vfs_mount_t g_vfs_mounts[VFS_MAX_MOUNT_POINTS];
static vfs_fd_t g_vfs_fds[VFS_MAX_OPEN_FILES];
static u32 g_vfs_node_counter = 0;
static bool g_vfs_initialized = false;

/* Forward declarations */
static vfs_node_t *vfs_find_node(const char *path);
static vfs_node_t *vfs_create_node(const char *name, vfs_type_t type);
static void vfs_destroy_node(vfs_node_t *node);

/* Initialize the VFS subsystem */
void vfs_init(void)
{
    if (g_vfs_initialized) return;
    
    /* Initialize root directory */
    k_memset(&g_vfs_root, 0, sizeof(g_vfs_root));
    k_strcpy(g_vfs_root.name, "/");
    g_vfs_root.type = VFS_TYPE_DIR;
    g_vfs_root.permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
    g_vfs_root.parent_id = 0;
    
    /* Initialize mount points */
    k_memset(g_vfs_mounts, 0, sizeof(g_vfs_mounts));
    g_vfs_mounts[0].root = &g_vfs_root;
    k_strcpy(g_vfs_mounts[0].path, "/");
    g_vfs_mounts[0].is_mounted = true;
    
    /* Initialize file descriptors */
    k_memset(g_vfs_fds, 0, sizeof(g_vfs_fds));
    
    g_vfs_node_counter = 1;
    g_vfs_initialized = true;
}

/* Create a new node in the filesystem */
static vfs_node_t *vfs_create_node(const char *name, vfs_type_t type)
{
    if (!g_vfs_initialized || !name) return NULL;
    
    /* Allocate from memory pool if available, else use static allocation */
    #if FEATURE_PERF_OPT
    vfs_node_t *node = (vfs_node_t *)perf_mem_alloc();
    #else
    static u8 g_static_pool[64 * sizeof(vfs_node_t)];
    static u32 g_static_idx = 0;
    
    if (g_static_idx >= 64) return NULL;
    vfs_node_t *node = (vfs_node_t *)&g_static_pool[g_static_idx++ * sizeof(vfs_node_t)];
    #endif
    
    if (!node) return NULL;
    
    k_memset(node, 0, sizeof(vfs_node_t));
    k_strcpy(node->name, name);
    node->type = type;
    node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    node->parent_id = g_vfs_node_counter++;
    
    if (type == VFS_TYPE_DIR) {
        node->child = NULL;
        node->next = NULL;
    }
    
    return node;
}

/* Destroy a node and its children */
static void vfs_destroy_node(vfs_node_t *node)
{
    if (!node) return;
    
    /* Recursively destroy children */
    vfs_node_t *child = node->child;
    while (child) {
        vfs_node_t *next = child->next;
        vfs_destroy_node(child);
        child = next;
    }
    
    /* Free file data if present */
    if (node->data) {
        #if FEATURE_PERF_OPT
        perf_mem_free(node->data);
        #endif
        node->data = NULL;
    }
    
    /* Free the node itself */
    #if FEATURE_PERF_OPT
    perf_mem_free(node);
    #endif
}

/* Parse a path and find the corresponding node */
static vfs_node_t *vfs_find_node(const char *path)
{
    if (!path || !g_vfs_initialized) return NULL;
    
    /* Handle root path */
    if (path[0] == '/' && path[1] == '\0') {
        return &g_vfs_root;
    }
    
    /* Start from root */
    vfs_node_t *current = &g_vfs_root;
    
    /* Skip leading slash */
    const char *p = path;
    if (*p == '/') p++;
    
    /* Traverse path components */
    while (*p) {
        /* Find end of current component */
        const char *end = p;
        while (*end && *end != '/') end++;
        
        /* Extract component name */
        char component[VFS_MAX_NAME];
        u32 len = (u32)(end - p);
        if (len >= VFS_MAX_NAME) len = VFS_MAX_NAME - 1;
        
        k_memcpy(component, p, len);
        component[len] = '\0';
        
        /* Search for this component in current directory */
        vfs_node_t *found = NULL;
        vfs_node_t *child = current->child;
        while (child) {
            if (perf_strcmp(child->name, component) == 0) {
                found = child;
                break;
            }
            child = child->next;
        }
        
        if (!found) return NULL;
        
        current = found;
        
        /* Move to next component */
        if (*end == '/') end++;
        p = end;
    }
    
    return current;
}

/* Create a directory at the specified path */
bool vfs_mkdir(const char *path)
{
    if (!path || !g_vfs_initialized) return false;
    
    /* Check if already exists */
    if (vfs_find_node(path)) return false;
    
    /* Find parent directory */
    char parent_path[VFS_MAX_PATH];
    k_strcpy(parent_path, path);
    
    char *last_slash = NULL;
    for (char *c = parent_path; *c; c++) {
        if (*c == '/') last_slash = c;
    }
    
    if (!last_slash) return false;
    *last_slash = '\0';
    
    vfs_node_t *parent = vfs_find_node(parent_path);
    if (!parent || parent->type != VFS_TYPE_DIR) return false;
    
    /* Create new directory node */
    char *name = last_slash + 1;
    vfs_node_t *dir = vfs_create_node(name, VFS_TYPE_DIR);
    if (!dir) return false;
    
    /* Add to parent's children */
    dir->next = parent->child;
    parent->child = dir;
    
    return true;
}

/* Create a file at the specified path */
bool vfs_create(const char *path)
{
    if (!path || !g_vfs_initialized) return false;
    
    /* Check if already exists */
    if (vfs_find_node(path)) return false;
    
    /* Find parent directory */
    char parent_path[VFS_MAX_PATH];
    k_strcpy(parent_path, path);
    
    char *last_slash = NULL;
    for (char *c = parent_path; *c; c++) {
        if (*c == '/') last_slash = c;
    }
    
    if (!last_slash) return false;
    *last_slash = '\0';
    
    vfs_node_t *parent = vfs_find_node(parent_path);
    if (!parent || parent->type != VFS_TYPE_DIR) return false;
    
    /* Create new file node */
    char *name = last_slash + 1;
    vfs_node_t *file = vfs_create_node(name, VFS_TYPE_FILE);
    if (!file) return false;
    
    /* Add to parent's children */
    file->next = parent->child;
    parent->child = file;
    
    return true;
}

/* Write data to a file */
i32 vfs_write(const char *path, const void *data, u32 size)
{
    if (!path || !data || !g_vfs_initialized) return -1;
    
    vfs_node_t *node = vfs_find_node(path);
    if (!node || node->type != VFS_TYPE_FILE) return -1;
    
    /* Allocate or reallocate file data */
    #if FEATURE_PERF_OPT
    if (node->data) perf_mem_free(node->data);
    node->data = perf_mem_alloc();
    #else
    static u8 g_file_data[256];  /* Simple fixed buffer for demo */
    node->data = g_file_data;
    #endif
    
    if (!node->data) return -1;
    
    /* Copy data */
    u32 copy_size = (size < 256) ? size : 256;
    perf_memcpy(node->data, data, copy_size);
    node->size = copy_size;
    
    return (i32)copy_size;
}

/* Read data from a file */
i32 vfs_read(const char *path, void *buffer, u32 size)
{
    if (!path || !buffer || !g_vfs_initialized) return -1;
    
    vfs_node_t *node = vfs_find_node(path);
    if (!node || node->type != VFS_TYPE_FILE) return -1;
    
    if (!node->data) return 0;
    
    u32 read_size = (size < node->size) ? size : node->size;
    perf_memcpy(buffer, node->data, read_size);
    
    return (i32)read_size;
}

/* List directory contents */
i32 vfs_readdir(const char *path, char names[][VFS_MAX_NAME], i32 max_entries)
{
    if (!path || !names || !g_vfs_initialized) return -1;
    
    vfs_node_t *dir = vfs_find_node(path);
    if (!dir || dir->type != VFS_TYPE_DIR) return -1;
    
    i32 count = 0;
    vfs_node_t *child = dir->child;
    
    while (child && count < max_entries) {
        k_strcpy(names[count], child->name);
        count++;
        child = child->next;
    }
    
    return count;
}

/* Get file/directory information */
bool vfs_stat(const char *path, void **out_node)
{
    if (!path || !g_vfs_initialized) return false;
    
    vfs_node_t *node = vfs_find_node(path);
    if (!node) return false;
    
    if (out_node) *(vfs_node_t **)out_node = node;
    return true;
}

/* Remove a file or empty directory */
bool vfs_remove(const char *path)
{
    if (!path || !g_vfs_initialized) return false;
    
    vfs_node_t *node = vfs_find_node(path);
    if (!node) return false;
    
    /* Don't allow removing root */
    if (node == &g_vfs_root) return false;
    
    /* Check if directory is empty */
    if (node->type == VFS_TYPE_DIR && node->child) {
        return false;  /* Directory not empty */
    }
    
    /* Find parent and unlink */
    /* Simplified: just mark as destroyed for now */
    vfs_destroy_node(node);
    
    return true;
}

/* Get VFS statistics */
void vfs_stats(u32 *total_nodes, u32 *open_files, u32 *mount_points)
{
    if (total_nodes) *total_nodes = g_vfs_node_counter;
    if (open_files) {
        u32 open_count = 0;
        for (u32 i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (g_vfs_fds[i].is_open) open_count++;
        }
        *open_files = open_count;
    }
    if (mount_points) {
        u32 mount_count = 0;
        for (u32 i = 0; i < VFS_MAX_MOUNT_POINTS; i++) {
            if (g_vfs_mounts[i].is_mounted) mount_count++;
        }
        *mount_points = mount_count;
    }
}

#endif /* FEATURE_VFS */
