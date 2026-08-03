/*
 * FalconOS GUI Server
 * High-performance graphical user interface server with compositing
 * Supports modern effects, hardware acceleration, and multi-window management
 */

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include <stdint.h>
#include <stddef.h>

#define MAX_WINDOWS 256
#define MAX_MONITORS 8
#define DEFAULT_DPI 96

typedef enum {
    WINDOW_TYPE_NORMAL,
    WINDOW_TYPE_DIALOG,
    WINDOW_TYPE_POPUP,
    WINDOW_TYPE_FULLSCREEN,
    WINDOW_TYPE_SYSTEM
} window_type_t;

typedef struct {
    int32_t x, y;
    uint32_t width, height;
} rect_t;

typedef struct {
    uint32_t red, green, blue, alpha;
} color_t;

typedef struct {
    uint32_t window_id;
    char title[256];
    window_type_t type;
    rect_t position;
    int is_visible;
    int is_focused;
    int is_minimized;
    int is_maximized;
    void* buffer;
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t process_id;
} window_t;

typedef struct {
    uint32_t monitor_id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
    uint32_t dpi_x;
    uint32_t dpi_y;
    int is_primary;
    void* frame_buffer;
} monitor_t;

typedef enum {
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_BUTTON,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_WINDOW_CLOSE,
    EVENT_WINDOW_FOCUS,
    EVENT_WINDOW_RESIZE,
    EVENT_CLIPBOARD_CHANGE
} event_type_t;

typedef struct {
    event_type_t type;
    uint64_t timestamp;
    uint32_t window_id;
    union {
        struct {
            int32_t x, y;
            uint32_t button;
        } mouse;
        struct {
            uint32_t key_code;
            uint32_t modifiers;
        } keyboard;
        struct {
            uint32_t width;
            uint32_t height;
        } resize;
    };
} gui_event_t;

// GUI Server initialization
int init_gui_server();

// Monitor management
monitor_t* get_primary_monitor();
monitor_t* get_monitor_by_id(uint32_t id);
int get_monitor_count();

// Window management
window_t* create_window(const char* title, window_type_t type, uint32_t width, uint32_t height);
int destroy_window(uint32_t window_id);
int show_window(uint32_t window_id);
int hide_window(uint32_t window_id);
int focus_window(uint32_t window_id);
int minimize_window(uint32_t window_id);
int maximize_window(uint32_t window_id);
int move_window(uint32_t window_id, int32_t x, int32_t y);
int resize_window(uint32_t window_id, uint32_t width, uint32_t height);

// Rendering
int begin_render(uint32_t window_id);
int end_render(uint32_t window_id);
int draw_rect(uint32_t window_id, rect_t rect, color_t color);
int draw_text(uint32_t window_id, int32_t x, int32_t y, const char* text, color_t color, uint32_t font_size);
int draw_image(uint32_t window_id, int32_t x, int32_t y, void* image_data, uint32_t width, uint32_t height);
int clear_window(uint32_t window_id, color_t color);

// Event handling
int poll_event(gui_event_t* event);
int wait_for_event(gui_event_t* event, uint64_t timeout_ms);
int dispatch_event(gui_event_t* event);

// Clipboard
int clipboard_set_text(const char* text);
char* clipboard_get_text();

// Screenshot
int take_screenshot(const char* filename);
int capture_window(uint32_t window_id, void* buffer, size_t buffer_size);

// Compositing
int enable_compositing();
int disable_compositing();
int set_composite_opacity(uint32_t window_id, float opacity);

#endif // GUI_SERVER_H
