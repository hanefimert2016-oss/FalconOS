// FalconOS Memory Manager
// Pure Rust - No Python

use x86_64::{
    structures::paging::{
        mapper::MapToError, FrameAllocator, Mapper, Page, PageTableFlags, Size4KiB,
    },
    VirtAddr,
};

use crate::println;

pub struct MemoryManager {
    next_frame: usize,
    total_frames: usize,
}

impl MemoryManager {
    pub const fn new() -> Self {
        MemoryManager {
            next_frame: 0,
            total_frames: 1024 * 16, // 16MB default for now
        }
    }

    pub fn allocate_frame(&mut self) -> Option<u64> {
        if self.next_frame < self.total_frames {
            let frame = self.next_frame as u64;
            self.next_frame += 1;
            Some(frame * 4096) // Convert to physical address
        } else {
            None
        }
    }

    pub fn deallocate_frame(&mut self, _addr: u64) {
        // In real implementation, would add back to free list
    }
}

static mut MEMORY_MANAGER: Option<MemoryManager> = None;

pub fn init() {
    println!("Initializing Memory Manager...");
    
    unsafe {
        MEMORY_MANAGER = Some(MemoryManager::new());
    }
    
    println!("Memory Manager initialized with {} frames", unsafe { 
        MEMORY_MANAGER.as_ref().unwrap().total_frames 
    });
}

pub fn allocate_frame() -> Option<u64> {
    unsafe {
        if let Some(ref mut mm) = MEMORY_MANAGER {
            mm.allocate_frame()
        } else {
            None
        }
    }
}

pub fn deallocate_frame(addr: u64) {
    unsafe {
        if let Some(ref mut mm) = MEMORY_MANAGER {
            mm.deallocate_frame(addr);
        }
    }
}

// Simple heap allocator placeholder
pub struct HeapAllocator;

impl HeapAllocator {
    pub const fn new() -> Self {
        HeapAllocator
    }
}

unsafe impl<'a> FrameAllocator<Size4KiB> for &'a mut HeapAllocator {
    fn allocate_frame(&mut self) -> Option<x86_64::structures::paging::PhysFrame> {
        // Placeholder - real implementation would use bitmap or linked list
        None
    }
}
