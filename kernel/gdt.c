/* =============================================================================
 *  FalconOS — GDT install (v5)
 * -----------------------------------------------------------------------------
 *  In long mode the boot stub already loaded a 64-bit GDT (boot/multiboot2.asm
 *  -> gdt64) before transitioning into 64-bit code, so this function is a
 *  no-op on x86_64.  We retain the symbol so kernel_main() can call it
 *  unconditionally for both architectures.
 *
 *  For ARCH=i386 builds we'd need the legacy 5-entry flat GDT; that path
 *  was retired in v5.  Restore from git history if you bring back i386.
 * ============================================================================= */
#include "falcon.h"

void gdt_install(void)
{
#if defined(ARCH_x86_64)
    /* boot stub already configured */
#else
#  error "v5 only ships a 64-bit GDT — see boot/multiboot2.asm"
#endif
}
