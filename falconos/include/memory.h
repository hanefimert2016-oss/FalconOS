/*
 * FalconOS Memory Manager
 * Advanced memory management with paging, virtual memory, and allocation
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define KERNEL_HEAP_SIZE (64 * 1024 * 1024) // 64MB kernel heap
#define USER_SPACE_START 0x00007FFFFFFFFFFF
#define KERNEL_SPACE_START 0xFFFF800000000000

typedef struct {
    uint64_t* page_directory;
    uint64_t* page_table;
    size_t total_pages;
    size_t free_pages;
} memory_context_t;

typedef struct {
    void* start;
    size_t size;
    int is_free;
} memory_block_t;

// Memory manager initialization
int init_memory_manager();

// Physical memory management
void* phys_alloc(size_t pages);
void phys_free(void* addr, size_t pages);

// Virtual memory management
void* virt_alloc(memory_context_t* ctx, size_t size);
void virt_free(memory_context_t* ctx, void* addr);

// Kernel heap management
void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

// Page table operations
void map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void unmap_page(uint64_t virt_addr);

// Memory context management
memory_context_t* create_memory_context();
void destroy_memory_context(memory_context_t* ctx);
void switch_memory_context(memory_context_t* ctx);

#endif // MEMORY_H
