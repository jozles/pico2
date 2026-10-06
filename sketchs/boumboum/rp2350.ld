MEMORY
{
    DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
    SRAM (rwx) : ORIGIN = 0x20010000, LENGTH = 448K
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 4M
}

SECTIONS
{
.ram_d1 : 
{
    __ram_d1_load__ = LOADADDR(.ram_d1);
    __ram_d1_start__ = .;
    KEEP(*(.ram_d1*))
    __ram_d1_end__ = .;
} > DTCM AT > FLASH


    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : {
        *(.data*)
    } > SRAM

    .bss : {
        *(.bss*)
    } > SRAM
}
