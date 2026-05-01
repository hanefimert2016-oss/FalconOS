/* FalconOS — x86_64 kernel link script (loaded at 1 MiB by GRUB)
 *
 * The kernel is linked as a 64-bit ELF, but the multiboot2 boot stub is
 * 32-bit and lives in `.multiboot`.  GRUB scans the first 32 KiB of the
 * file for the magic, so we keep `.multiboot` first.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    . = 1M;

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
