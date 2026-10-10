/* Based on GCC ARM embedded samples.
   Modified: 2026 Neil Rackett, for the ROTT Accelerator.
   Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    __preinit_array_end
    __init_array_start
    __init_array_end
    __fini_array_start
    __fini_array_end
    __data_end__
    __bss_start__
    __bss_end__
    __end__
    end
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/

MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 256k  /* App code */
    /* MD/ROTT: the microfirmware slot is 1152 KB (0x10000000..0x10120000,
       everything below the Booster). A render-only firmware needs far
       less than MD/DOOM's 372 KB, so the level pack gets the rest: one
       level's walls, sprites, sky and colour maps, built from the WAD on
       the SD card (md_pack.c) and read in place over XIP. FLASH +
       PACK_FLASH must stay below BOOSTER_APP_FLASH at 0x10120000. */
    PACK_FLASH(rw) : ORIGIN = 0x10040000, LENGTH = 896k /* level asset pack */

/* This is the default flash space for the app if you don't need room to store data */
/*    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 1152k  The first 1152kb available */
    RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 192k
    ROM_IN_RAM (rwx) : ORIGIN = 0x20030000, LENGTH = 64K
    /* MD/ROTT: two holes the protocol leaves in the cartridge window
       (rott_md_protocol.h): after the second frame buffer up to the token
       at $F000, and after the result block at $F100. Ordinary SRAM the ST
       never reads, so a few frame-scoped buffers are parked there
       (MD_CART_HOLE in rott/r_local.h), leaving main RAM a heap big enough
       for the settings at boot (see the heap ASSERT below). They overlap
       ROM_IN_RAM, whose section is empty; md_main.c checks the protocol
       still leaves them free. */
    CART_HOLE_A (rw) : ORIGIN = 0x2003E680, LENGTH = 0x980
    CART_HOLE_B (rw) : ORIGIN = 0x2003F200, LENGTH = 0xE00
    SCRATCH_X(rwx) : ORIGIN = 0x20040000, LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20041000, LENGTH = 4k
    BOOSTER_APP_FLASH(r) : ORIGIN = 0x10120000, LENGTH = 768K /* Size of the flash for the booster app */ 
    CONFIG_FLASH(rwx): ORIGIN = 0x101E0000, LENGTH = 120K /* At the top 120Kb of the Flash we have the config information. 30 sectors */
    GLOBAL_LOOKUP_FLASH(r): ORIGIN = 0x101FE000, LENGTH = 4K /* The lookup table with apps UUID and the sector number of their config */
    GLOBAL_CONFIG_FLASH(r): ORIGIN = 0x101FF000, LENGTH = 4K /* At the top 4KB of the Flash we have the global lookup information */
    /* The booster code must be allocated from 0x10120000 to 0x101DFFFF */
}

ENTRY(_entry_point)

SECTIONS
{
    /* Second stage bootloader is prepended to the image. It must be 256 bytes big
       and checksummed. It is usually built by the boot_stage2 target
       in the Raspberry Pi Pico SDK
    */

    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")

    /* The second stage will always enter the image at the start of .text.
       The debugger will use the ELF entry point, which is the _entry_point
       symbol if present, otherwise defaults to start of .text.
       This can be used to transfer control back to the bootrom on debugger
       launches only, to perform proper flash setup.
    */

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.reset))
        /* TODO revisit this now memset/memcpy/float in ROM */
        /* bit of a hack right now to exclude all floating point and time critical (e.g. memset, memcpy) code from
         * FLASH ... we will include any thing excluded here in .data below by default */
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)
        *(.fini)
        /* Pull all c'tors into .text */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        /* Followed by destructors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.eh_frame*)
        . = ALIGN(4);
    } > FLASH

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* Machine inspectable binary information */
    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info :
    {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > FLASH
    __binary_info_end = .;
    . = ALIGN(4);

   .ram_vector_table (NOLOAD): {
        *(.ram_vector_table)
    } > RAM

    .data : {
        __data_start__ = .;
        *(vtable)

        *(.time_critical*)

        /* remaining .text and .rodata; i.e. stuff we exclude above because we want it in RAM */
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);

        *(.data*)

        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__mutex_array_start = .);
        KEEP(*(SORT(.mutex_array.*)))
        KEEP(*(.mutex_array))
        PROVIDE_HIDDEN (__mutex_array_end = .);

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(SORT(.preinit_array.*)))
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        *(SORT(.fini_array.*))
        *(.fini_array)
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.jcr)
        . = ALIGN(4);
        /* All data end */
        __data_end__ = .;
    } > RAM AT> FLASH
    /* __etext is (for backwards compatibility) the name of the .data init source pointer (...) */
    __etext = LOADADDR(.data);

    .uninitialized_data (NOLOAD): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    /* Start and end symbols must be word-aligned */
    .scratch_x : {
        __scratch_x_start__ = .;
        *(.scratch_x.*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    } > SCRATCH_X AT > FLASH
    __scratch_x_source__ = LOADADDR(.scratch_x);

    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(4);
        __scratch_y_end__ = .;
    } > SCRATCH_Y AT > FLASH
    __scratch_y_source__ = LOADADDR(.scratch_y);

    /* RAM used for ROM_IN_RAM */
    .rom_in_ram : {
        __rom_in_ram_start__ = .;
        *(.rom_in_ram.*)
        . = ALIGN(4);
        __rom_in_ram_end__ = .;
    } > ROM_IN_RAM AT > FLASH
    __rom_in_ram_source__ = LOADADDR(.rom_in_ram);

    /* Buffers parked in the cartridge-window holes (CART_HOLE_A/B above).
       NOLOAD: nothing here is zeroed at start. */
    .cart_hole_a (NOLOAD) : { *(.cart_hole_a.*) } > CART_HOLE_A
    .cart_hole_b (NOLOAD) : { *(.cart_hole_b.*) } > CART_HOLE_B

    .bss  : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (NOLOAD):
    {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
        __HeapLimit = .;
    } > RAM

    /* .stack*_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later
     *
     * stack1 section may be empty/missing if platform_launch_core1 is not used */

    /* by default we put core 0 stack at the end of scratch Y, so that if core 1
     * stack is not used then all of SCRATCH_X is free.
     */
    .stack1_dummy (NOLOAD):
    {
        *(.stack1*)
    } > SCRATCH_X
    .stack_dummy (NOLOAD):
    {
        KEEP(*(.stack*))
    } > SCRATCH_Y

    .flash_end : {
        PROVIDE(__flash_binary_end = .);
    } > FLASH

   .pack_flash :
    {
        _pack_flash_start = .;
        KEEP(*(.pack_flash))
        _pack_flash_end = .;
    } > PACK_FLASH
    /* The region LENGTH, not the section content: _pack_flash_start and
       _pack_flash_end bracket what is linked into .pack_flash (nothing),
       so they cannot give the size of the window a level pack is
       programmed into. Exporting it here keeps the bound the app
       enforces and the space the linker reserved from drifting apart. */
    __pack_flash_size__ = LENGTH(PACK_FLASH);

   .booster_app_flash :
    {
        _booster_app_flash_start = .;
        KEEP(*(.booster_app_flash))
        _booster_app_flash_end = .;
    } > BOOSTER_APP_FLASH


   .config_flash :
    {
        _config_flash_start = .;
        KEEP(*(.config_flash))
        _config_flash_end = .;
    } > CONFIG_FLASH

   .global_lookup_flash :
    {
        _global_lookup_flash_start = .;
        KEEP(*(.global_lookup_flash))
        _global_lookup_flash_end = .;
    } > GLOBAL_LOOKUP_FLASH


    .global_config_flash :
    {
        _global_config_flash_start = .;
        KEEP(*(.global_config_flash))
        _global_config_flash_end = .;
    } > GLOBAL_CONFIG_FLASH






    /* stack limit is poorly named, but historically is maximum heap ptr.
     * Must NOT include ROM_IN_RAM: pico-sdk's _sbrk bounds the heap by
     * this symbol, and letting it grow past ORIGIN(ROM_IN_RAM) hands the
     * cartridge region (the code the m68k executes over the cart bus) to
     * malloc -- FatFs LFN buffers then corrupt it at runtime and the ST
     * crashes with bombs. Capped at the RAM/cart boundary so malloc
     * fails cleanly instead. */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = __StackOneTop - SIZEOF(.stack1_dummy);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Check if data + heap + stack exceeds RAM limit */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")

    /* MD/ROTT: the heap must hold the settings at boot (gconfig_init and
       aconfig_init keep 4 KB each and briefly copy their defaults: about
       9 KB at the peak) plus FatFs's LFN buffers. Short of that, main()
       goes back to Booster on every start, so fail the link instead. */
    ASSERT(__StackLimit - __end__ >= 11 * 1024, "heap too small for the settings at boot: free some RAM (see MD_CART_HOLE)")

    ASSERT( __binary_info_header_end - __logical_binary_start <= 256, "Binary info must be in first 256 bytes of the binary")
    /* todo assert on extra code */
}
