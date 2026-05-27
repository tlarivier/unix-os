/* Kernel linker script for x86 */
ENTRY(_kernel_entry)

SECTIONS
{
    . = 0x100000;  /* 1MB - standard multiboot load address */

    /* Multiboot headers MUST be first (within 8KB of file start) */
    .multiboot : {
        *(.multiboot1)
        *(.multiboot)
    }

    /* Kernel entry at fixed offset 0x100100 (256 bytes after start) */
    . = 0x100100;
    _kernel_entry = .;
    
    .text : {
        *(.entry)
        *(.text)
    }

    .rodata ALIGN(0x1000) : {
        *(.rodata)
    }

    .data ALIGN(0x1000) : {
        *(.data)
    }

    .bss ALIGN(0x1000) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        _bss_end = .;
    }

    /* End of kernel symbol */
    _kernel_end = .;
}
