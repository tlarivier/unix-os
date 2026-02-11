/*
 * Linker script for dynamically linked executables with shared libraries
 * 
 * Single PT_LOAD for code starting at 0x08048000, includes ELF header.
 * Separate PT_LOAD for writable data.
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5);  /* R+X with ELF headers */
    data PT_LOAD FLAGS(6);                 /* R+W */
    dynamic PT_DYNAMIC FLAGS(6);
    interp PT_INTERP;
}

SECTIONS
{
    . = 0x08048000 + SIZEOF_HEADERS;

    .interp : { *(.interp) } :text :interp
    .hash : { *(.hash) } :text
    .dynsym : { *(.dynsym) } :text
    .dynstr : { *(.dynstr) } :text
    .rel.dyn : { *(.rel.dyn) *(.rel*) } :text
    .rel.plt : { *(.rel.plt) } :text
    .plt : { *(.plt) } :text

    .text : {
        *(.text)
        *(.text.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .dynamic : { *(.dynamic) } :data :dynamic
    .got : { *(.got) } :data
    .got.plt : { *(.got.plt) } :data
    .data : {
        *(.data)
        *(.data.*)
    } :data
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
