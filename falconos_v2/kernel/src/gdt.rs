// FalconOS GDT (Global Descriptor Table) Implementation
// Pure Rust - No Python

use x86_64::{
    structures::gdt::{Descriptor, GlobalDescriptorTable, SegmentSelector},
    instructions::segmentation::{self, CS},
    VirtAddr,
};

use crate::println;

pub static mut GDT: Option<GlobalDescriptorTable> = None;

pub fn init() {
    println!("Initializing GDT...");
    
    let mut gdt = GlobalDescriptorTable::new();
    
    // Add code segment descriptor
    gdt.add_descriptor(Descriptor::KernelCodeSegment);
    
    // Add data segment descriptor  
    gdt.add_descriptor(Descriptor::KernelDataSegment);
    
    // Load the GDT
    unsafe {
        GDT = Some(gdt);
        if let Some(ref gdt_ref) = GDT {
            let code_selector = SegmentSelector::new(1, segmentation::PrivilegeLevel::Ring0);
            let data_selector = SegmentSelector::new(2, segmentation::PrivilegeLevel::Ring0);
            
            gdt_ref.load();
            
            unsafe {
                CS::set_reg(code_selector);
                segmentation::DS::set_reg(data_selector);
                segmentation::ES::set_reg(data_selector);
                segmentation::SS::set_reg(data_selector);
            }
        }
    }
    
    println!("GDT loaded successfully");
}
