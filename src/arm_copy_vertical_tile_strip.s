@--------------------------------------------------------------------------------
@ arm_copy_vertical_tile_strip.s
@--------------------------------------------------------------------------------
@ Provides the implementation of void arm::copy_vertical_tile_strip
@--------------------------------------------------------------------------------

@ void arm::copy_vertical_tile_strip_8bpp(void* dest, const void* src,
@       const uint16_t* map_cells, int num_tiles);
@ r0: dest - the tile strip to copy the vertical tiles to
@ r1: src - the pointer to the first tile to be copied
@ r2: map_cells
@ r3: num_tiles
    .section .iwram._ZN3arm29copy_vertical_tile_strip_8bppEPvPKvPKti, "ax", %progbits
    .align 2
    .arm
    .global _ZN3arm29copy_vertical_tile_strip_8bppEPvPKvPKti
    .type _ZN3arm29copy_vertical_tile_strip_8bppEPvPKvPKti STT_FUNC
_ZN3arm29copy_vertical_tile_strip_8bppEPvPKvPKti:
    cmp     r3, #0                  @ Return if there isn't any tiles to copy
    bxeq    lr

    push    {r4-r11}                @ Push the necessary registers to stack

.Lcopy_tile:
    ldrh    r12, [r2], #64          @ Get the next tile ID (and add 32*2 to get the next vertical tile)
    add     r12, r1, r12, lsl #6    @ Get the tile address from the tile ID
    ldmia   r12!, {r4-r11}          @ Get the first 32 bytes of the tile
    stmia   r0!, {r4-r11}           @ and transfer them to the storage
    ldmia   r12, {r4-r11}           @ Get the last 32 bytes of the tile
    stmia   r0!, {r4-r11}           @ and transfer them as well
    subs    r3, r3, #1              @ Subtract one from the counter
    bne     .Lcopy_tile             @ and return if there are still bytes to copy 

    pop     {r4-r11}                @ Restore the stack frame
    bx      lr                      @ and return
