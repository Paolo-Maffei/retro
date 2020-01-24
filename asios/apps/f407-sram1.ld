/* PlatformIO default linker script template for STM32 F4 */

ENTRY(main)

/* Highest address of the user mode stack */ 
_estack = 0x20010000;    /* end of SRAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Stack_Size = 0x100;     /* required amount of stack */

MEMORY
{
    SRAM (xrw)     : ORIGIN = 0x20000000, LENGTH = 64K
}

SECTIONS
{
  .exe_header :
  {
    . = ALIGN(4);
    KEEP(*(.exe_header)) /* header */
    . = ALIGN(4);
  } >SRAM

  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */

    KEEP (*(.init))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >SRAM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >SRAM

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >SRAM
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >SRAM

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >SRAM
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >SRAM
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >SRAM

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  .data : 
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >SRAM
  
  . = ALIGN(4);
  .bss :
  {
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >SRAM

  ._user_heap_stack :
  {
    . = ALIGN(4);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Stack_Size;
    . = ALIGN(4);
  } >SRAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}


