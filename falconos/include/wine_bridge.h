/*
 * FalconOS WineBridge - Windows Application Compatibility Layer
 * Modified Wine infrastructure to run Windows applications on Linux
 * Provides seamless Windows API translation and system call interception
 */

#ifndef WINE_BRIDGE_H
#define WINE_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#define WINE_BRIDGE_VERSION "2.1.0"
#define MAX_WINE_PROCESSES 256
#define WINE_DLL_PATH "/system/wine/lib"
#define WINE_REGISTRY_PATH "/system/wine/registry"

typedef struct {
    uint32_t pid;
    char* exe_path;
    void* wine_context;
    int is_running;
    uint64_t start_time;
} wine_process_t;

typedef struct {
    char* dll_name;
    void* handle;
    int is_loaded;
} wine_dll_t;

// WineBridge initialization
int init_wine_bridge();

// Process management
wine_process_t* wine_create_process(const char* exe_path);
int wine_terminate_process(wine_process_t* process);
int wine_wait_for_process(wine_process_t* process, uint64_t timeout_ms);

// DLL loading and management
wine_dll_t* wine_load_dll(const char* dll_name);
void* wine_get_proc_address(wine_dll_t* dll, const char* func_name);
int wine_unload_dll(wine_dll_t* dll);

// Windows API translation
int wine_translate_syscall(uint64_t syscall_num, void* args);
int wine_handle_exception(wine_process_t* process, void* exception_info);

// Registry emulation
int wine_registry_read(const char* key, void* buffer, size_t size);
int wine_registry_write(const char* key, const void* data, size_t size);

// File system redirection
char* wine_redirect_path(const char* windows_path);
int wine_create_file_mapping(const char* path, void** mapping, size_t size);

// Graphics integration
int wine_init_graphics_adapter();
int wine_handle_window_message(void* hwnd, uint32_t msg, uint64_t wParam, uint64_t lParam);

// Performance optimization
int wine_enable_fast_path();
int wine_disable_fast_path();

#endif // WINE_BRIDGE_H
