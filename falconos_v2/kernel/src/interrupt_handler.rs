// FalconOS Interrupt Handler
// Pure Rust - No Python

use x86_64::{
    structures::idt::{InterruptDescriptorTable, InterruptStackFrame},
    instructions::port::Port,
};

use crate::println;

pub static mut IDT: Option<InterruptDescriptorTable> = None;

extern "x86-interrupt" fn breakpoint_handler(stack_frame: InterruptStackFrame) {
    println!("\n!!! BREAKPOINT EXCEPTION !!!");
    println!("Stack Frame: {:?}", stack_frame);
    loop {}
}

extern "x86-interrupt" fn double_fault_handler(
    stack_frame: InterruptStackFrame, 
    _error_code: u64
) -> ! {
    panic!("\n!!! DOUBLE FAULT !!!\n{:?}", stack_frame);
}

extern "x86-interrupt" fn timer_interrupt(_stack_frame: InterruptStackFrame) {
    // Timer tick handler for process scheduler
    // In real implementation, this would increment system time
    // and trigger context switch if needed
}

extern "x86-interrupt" fn keyboard_interrupt(_stack_frame: InterruptStackFrame) {
    // Keyboard input handler
    let mut port = Port::new(0x60);
    let scancode: u8 = unsafe { port.read() };
    // Process scancode in actual implementation
}

extern "x86-interrupt" fn page_fault_handler(
    stack_frame: InterruptStackFrame,
    error_code: u64,
) {
    use x86_64::registers::control::Cr2;
    
    println!("\n!!! PAGE FAULT !!!");
    println!("Accessed Address: {:?}", Cr2::read());
    println!("Error Code: {:?}", error_code);
    println!("{:?}", stack_frame);
    loop {}
}

pub fn init_idt() {
    println!("Initializing IDT...");
    
    let mut idt = InterruptDescriptorTable::new();
    
    // Register interrupt handlers
    idt.breakpoint.set_handler_fn(breakpoint_handler);
    idt.double_fault.set_handler_fn(double_fault_handler);
    idt.page_fault.set_handler_fn(page_fault_handler);
    
    // Hardware interrupts (IRQs)
    idt.timer.set_handler_fn(timer_interrupt);
    idt.keyboard.set_handler_fn(keyboard_interrupt);
    
    unsafe {
        IDT = Some(idt);
        if let Some(ref idt_ref) = IDT {
            idt_ref.load();
        }
    }
    
    println!("IDT loaded successfully");
}
