/* FalconOS V2 Beta — FalconBoot/1 native UEFI fixed-address ELF.
 * FalconBoot loads the flat kernel image to 32 MiB (inside QEMU OVMF RAM).
 * The old multiboot stub remains uninvoked for migration compatibility;
 * no GRUB image, GRUB config or GRUB stage participates in UEFI boot.
 * Changing this physical base MUST update KBASE in the UEFI loader.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    . = 32M;

    .multiboot ALIGN(8) :
    {
        KEEP(*(.multiboot))
    }

    .text ALIGN(4K) :
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
    }
}
