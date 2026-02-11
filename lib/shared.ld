/*
 * Linker script for shared libraries (.so)
 * Position Independent Code (PIC)
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)

SECTIONS
{
    /* Start at 0 - will be relocated at load time */
    . = 0;

    /* Read-only sections */
    .hash : { *(.hash) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rel.dyn : { *(.rel.dyn) }
    .rel.plt : { *(.rel.plt) }

    .plt : { *(.plt) }
    
    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Read-write sections */
    . = ALIGN(4096);

    .dynamic : { *(.dynamic) }
    
    .got : { *(.got) }
    .got.plt : { *(.got.plt) }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}
