//--------------------------------------------------------------------------------
// main.cpp
//--------------------------------------------------------------------------------
// The main function
//--------------------------------------------------------------------------------
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_math.h"
#include "bn_display.h"
#include "bn_bgs.h"

#include "bn_regular_bg_tiles_item.h"
#include "bn_regular_bg_tiles_ptr.h"
#include "bn_bg_palette_item.h"
#include "bn_bg_palette_ptr.h"
#include "bn_regular_bg_actions.h"
#include "bn_regular_bg_attributes.h"
#include "bn_regular_bg_builder.h"

#include "bn_algorithm.h"
#include "bn_sin_lut.h"

// Why isn't that exposed to us?
#include "../hw/include/bn_hw_memory.h"

#include "bn_regular_bg_items_br_flag.h"
#include "bn_regular_bg_items_us_flag.h"

namespace data
{
    // Flag dimensions
    constexpr int flag_width_pixels = 192;
    constexpr int flag_height_pixels = 128;
    constexpr int flag_width_tiles = flag_width_pixels/8;
    constexpr int flag_height_tiles = flag_height_pixels/8;

    // The flag should be centered
    constexpr int flag_offset_x = (32 - flag_width_tiles)/2;
    constexpr int flag_offset_y = (32 - flag_height_tiles)/2;

    // Allocation numbers
    constexpr int flag_tiles_needed = flag_width_tiles * (flag_height_tiles + 2);

    // Important data to generate the LUT
    constexpr int wave_vertical_amplitude = 4;
    constexpr int wave_horizontal_period = 128;
    constexpr auto wave_horizontal_multiplier = 2048 / wave_horizontal_period;
}

namespace arm
{
    // Copies a vertical strip from a background originally formatted to be 32x32 horizontal
    // into a vertically-oriented tile map; will basically copy a tile strip in a contiguous
    // version of memory. This is needed to deal with Butano's formatting tool that only exports
    // tiles in row-major order, not allowing to export in column-major order
    void copy_vertical_tile_strip_8bpp(void* dest, const void* src, const uint16_t* map_cells, int num_tiles) BN_CODE_IWRAM;
}

static bn::optional<bn::regular_bg_tiles_ptr> flag_tiles;
static bn::optional<bn::bg_palette_ptr> flag_palette;
static bn::optional<bn::regular_bg_map_ptr> flag_maps[2];
static bn::optional<bn::regular_bg_ptr> flag_bg;
static int current_frame = 0;

void init_background()
{
    // Allocate tiles and maps needed for the background
    // The 2 multiplying here is because an 8bpp has double the size as two 4bpp tiles,
    // but the function accepts only 4bpp tiles, so we need to multiply
    auto tiles = bn::regular_bg_tiles_ptr::allocate(2 * (2 * data::flag_tiles_needed + 1), bn::bpp_mode::BPP_8);
    auto palette = bn::regular_bg_items::us_flag.palette_item().create_palette();

    // Create the maps
    for (int i : { 0, 1 })
    {
        // Create the map and first fill it blank
        auto map = bn::regular_bg_map_ptr::allocate(bn::size(32, 32), tiles, palette);
        auto vram = *map.vram().get();
        bn::fill(vram.begin(), vram.end(), bn::regular_bg_map_cell());

        // Fill in the map with the proper values
        for (int x = 0; x < data::flag_width_tiles; x++)
            for (int y = 0; y < data::flag_height_tiles + 2; y++)
            {
                auto tile_x = data::flag_offset_x + x;
                auto tile_y = data::flag_offset_y + y - 1;
                auto tile_index = (data::flag_height_tiles + 2) * x + y;
                vram[32 * tile_y + tile_x] = i * data::flag_tiles_needed + tile_index + 1;
            }

        flag_maps[i] = map;
    }

    flag_tiles = tiles;
    flag_palette = palette;

    // Now, create the background
    bn::regular_bg_builder flag_builder(*flag_maps[0]);
    flag_bg = bn::regular_bg_ptr::create(std::move(flag_builder));
}

// Get the waving flag displacement based on the position and time
inline int displacement(int x, int t)
{
    // This is just to make it beautiful
    int a = data::wave_horizontal_multiplier * (x - t);
    return (data::wave_vertical_amplitude * bn::lut_sin(a & 2047)).round_integer();
}

// Transfer the flag's data to the graphics
void transfer_flag(const bn::regular_bg_item& flag_item)
{
    // Get the necessary data
    auto dst = current_frame&1;
    auto flag_tiles_ptr = flag_item.tiles_item().tiles_ref().data();
    auto flag_map_ptr = &flag_item.map_item().cells_ref();
    // (points to the first non-null tile)
    auto dest_tiles_ptr = flag_tiles->vram()->data() + 2 * (dst * data::flag_tiles_needed + 1);

    // Now, transfer the tiles using a fast ASM routine
    for (int x = 0; x < data::flag_width_tiles; x++)
    {
        // The 2 needs to be here because bn::tile represents a 4bpp tile,
        // and a 8bpp tile is equivalent to two bn::tile
        auto tile_ptr = dest_tiles_ptr + 2 * (data::flag_height_tiles + 2) * x;

        // Compute the pointer to the base line we will be using here
        // uint64_t is 8 bytes, exactly the size of one tile row
        auto disp = displacement(8 * x, current_frame);
        auto line_ptr = reinterpret_cast<uint64_t*>(tile_ptr + 2) + disp;
        auto map_ptr = flag_map_ptr + (32 * data::flag_offset_y + data::flag_offset_x + x);
        arm::copy_vertical_tile_strip_8bpp(line_ptr, flag_tiles_ptr, map_ptr, data::flag_height_tiles);

        // In theory, we should initialize the rest of the words too, but they'll precisely
        // override the other flag
#if 0
        // Zero the first words (this is necessary when changing flags)
        int lines_to_copy = line_ptr - reinterpret_cast<uint64_t*>(tile_ptr);
        int words_to_copy = lines_to_copy * sizeof(uint64_t) / sizeof(uint32_t);
        bn::hw::memory::set_words(0, words_to_copy, tile_ptr);

        // Zero the last words
        auto line_end_ptr = line_ptr + 8 * data::flag_height_tiles;
        auto tile_end_ptr = tile_ptr + 2 * (data::flag_height_tiles + 2);
        lines_to_copy = reinterpret_cast<uint64_t*>(tile_end_ptr) - line_end_ptr;
        words_to_copy = lines_to_copy * sizeof(uint64_t) / sizeof(uint32_t);
        bn::hw::memory::set_words(0, words_to_copy, line_end_ptr);
#endif
    }

    // Fix the palette
    flag_palette = flag_item.palette_item().create_palette();
    for (int i : { 0, 1 }) flag_maps[i]->set_palette(*flag_palette);
    flag_bg->set_palette(*flag_palette);
}

void update_wave()
{
    // Get the dest and the source destinations
    auto src = current_frame&1, dst = src^1;

    // Same thing here, since we're doing 8-bpp tiles, we need the 2* in this place
    auto tiles_base_ptr = flag_tiles->vram()->data();
    auto tiles_src_ptr = tiles_base_ptr + 2 * (data::flag_tiles_needed * src + 1);
    auto tiles_dst_ptr = tiles_base_ptr + 2 * (data::flag_tiles_needed * dst + 1);

    constexpr int tiles_to_copy = data::flag_height_tiles + 2;
    constexpr int words_to_copy = 2 * sizeof(bn::tile) * tiles_to_copy / sizeof(uint32_t);
    // Here, do the "waving flag" displacement, copying the data to the second frame
    for (int x = 0; x < data::flag_width_tiles; x++)
    {
        // Multiply by 2 here to account that bn::tile represents one 4bpp tile,
        // and we need 2 bn::tiles for one 8bpp tile
        auto x_disp = 2 * (data::flag_height_tiles + 2) * x;
        auto col_src_ptr = tiles_src_ptr + x_disp;
        auto col_dst_ptr = tiles_dst_ptr + x_disp;

        // Compute the pointer to the base line we will be using here
        // uint64_t is 8 bytes, exactly the size of one tile row
        auto d_disp = displacement(8*x, current_frame+1) - displacement(8*x, current_frame);
        auto line_dst_ptr = reinterpret_cast<uint64_t*>(col_dst_ptr) + d_disp;
        // That's an unusual arrangement (src, length, dest) but okay
        bn::hw::memory::copy_words(col_src_ptr, words_to_copy, line_dst_ptr);
    }

    // And update the current frame
    current_frame++;
    flag_bg->set_map(*flag_maps[dst]);
}

// Array of references is not permitted
static const bn::regular_bg_item* flags[2] =
{
    &bn::regular_bg_items::br_flag,
    &bn::regular_bg_items::us_flag
};

int main()
{
    bn::core::init();
    init_background();
    transfer_flag(bn::regular_bg_items::br_flag);
    int flag = 0;

    while(true)
    {
        // Toggle the flag when A is pressed
        if (bn::keypad::a_pressed())
            transfer_flag(*flags[flag ^= 1]);

        update_wave();
        bn::core::update();
    }
}
