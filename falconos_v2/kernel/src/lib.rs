#![no_std]
#![feature(asm_const)]
#![feature(naked_functions)]
#![feature(abi_x86_interrupt)]

//! FalconOS Kernel v2.0 Alpha "Nexus"
//! Pure Rust kernel with Assembly boot code
//! Inspired by Xiaomi HyperOS design philosophy
//! Zero Python - Maximum Performance

extern crate alloc;

use core::panic::PanicInfo;
use x86_64::{
    structures::idt::{InterruptDescriptorTable, InterruptStackFrame},
    instructions::{hlt, interrupts::enable},
    VirtAddr,
};

mod gdt;
mod interrupt_handler;
mod memory;
mod vga_buffer;
mod syscall;
mod process;
mod filesystem;
mod falconbridge;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    // Initialize VGA buffer for early boot messages
    vga_buffer::clear_screen();
    println!("FalconOS v2.0 Alpha 'Nexus' Booting...");
    
    // Initialize GDT (Global Descriptor Table)
    gdt::init();
    println!("[OK] GDT Initialized");
    
    // Initialize IDT (Interrupt Descriptor Table)
    interrupt_handler::init_idt();
    println!("[OK] IDT Initialized");
    
    // Enable interrupts
    unsafe { enable() };
    println!("[OK] Interrupts Enabled");
    
    // Initialize Memory Manager
    memory::init();
    println!("[OK] Memory Manager Initialized");
    
    // Initialize Virtual File System
    filesystem::init();
    println!("[OK] VFS Initialized");
    
    // Initialize FalconBridge (AppImage/DEB runtime)
    falconbridge::init();
    println!("[OK] FalconBridge Initialized");
    
    // Initialize Process Scheduler
    process::init();
    println!("[OK] Process Scheduler Initialized");
    
    // Initialize Syscall Interface
    syscall::init();
    println!("[OK] Syscall Interface Ready");
    
    println!("\n=== FalconOS Desktop Environment Starting ===\n");
    
    // Start GUI Server (userspace)
    // In real implementation, this would spawn the GUI process
    loop {
        hlt();
    }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("\n!!! KERNEL PANIC !!!\n{}", info);
    loop {
        hlt();
    }
}

// VGA printing macro for kernel messages
#[macro_export]
macro_rules! println {
    () => ($crate::print!("\n"));
    ($($arg:tt)*) => ($crate::print!("{}\n", format_args!($($arg)*)));
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => ($crate::vga_buffer::_print(format_args!($($arg)*)));
}
