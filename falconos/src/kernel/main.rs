/* 
 * FalconOS v2.1 "Nexus" - Main Kernel Entry Point
 * Copyright (c) 2024 FalconOS Team
 * License: MIT
 * 
 * A modern, high-performance operating system kernel written in Rust and C
 * Features: Microkernel architecture, WineBridge compatibility layer, 
 *           Virtual File System, Multi-threading support
 */

#![no_std]
#![no_main]
#![feature(asm_const)]
#![feature(naked_functions)]

use core::panic::PanicInfo;

// External C functions for low-level operations
extern "C" {
    fn boot_init() -> i32;
    fn init_memory_manager() -> i32;
    fn init_interrupts() -> i32;
    fn init_scheduler() -> i32;
    fn init_filesystem() -> i32;
    fn init_wine_bridge() -> i32;
    fn init_gui_server() -> i32;
    fn start_kernel_loop();
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    // Initialize boot process
    unsafe {
        if boot_init() != 0 {
            halt();
        }
        
        // Initialize subsystems in order
        let mut status = 0;
        status |= init_memory_manager();
        status |= init_interrupts();
        status |= init_scheduler();
        status |= init_filesystem();
        status |= init_wine_bridge();
        status |= init_gui_server();
        
        if status != 0 {
            halt();
        }
        
        // Start main kernel loop
        start_kernel_loop();
    }
    
    halt()
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    halt()
}

#[no_mangle]
pub extern "C" fn halt() -> ! {
    loop {
        unsafe {
            asm!("hlt", options(nomem, nostack));
        }
    }
}
