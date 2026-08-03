/*
 * FalconOS Interrupt Manager
 * Hardware interrupt handling and IDT management
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

#define MAX_INTERRUPTS 256
#define IRQ_BASE 32

typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
    uint64_t error_code;
    uint64_t interrupt_number;
} interrupt_frame_t;

typedef void (*interrupt_handler_t)(interrupt_frame_t* frame);

// Interrupt manager initialization
int init_interrupts();

// IDT management
int idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags);

// Handler registration
int register_interrupt_handler(uint8_t irq, interrupt_handler_t handler);
int unregister_interrupt_handler(uint8_t irq);

// PIC/APIC management
void pic_send_eoi(uint8_t irq);
void apic_init();

// Exception handlers
void divide_error_handler(interrupt_frame_t* frame);
void general_protection_fault(interrupt_frame_t* frame);
void page_fault_handler(interrupt_frame_t* frame);

#endif // INTERRUPTS_H
