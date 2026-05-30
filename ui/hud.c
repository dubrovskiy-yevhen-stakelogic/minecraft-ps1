/*
 * ui/hud.c
 *
 * In-game HUD, crosshair, hotbar, hearts and first-person held item rendering.
 * Transitional stage: included directly from main.c.
 */

#include "hud.h"

static void draw_crosshair(RenderContext *context) {
    const int cx = SCREEN_W / 2;
    const int cy = SCREEN_H / 2;

    draw_line(context, cx - 5, cy, cx - 2, cy, 0, 235, 235, 235);
    draw_line(context, cx + 2, cy, cx + 5, cy, 0, 235, 235, 235);
    draw_line(context, cx, cy - 5, cx, cy - 2, 0, 235, 235, 235);
    draw_line(context, cx, cy + 2, cx, cy + 5, 0, 235, 235, 235);
}

static int block_type_to_icon_texture(int block_type) {
    if (block_type == BLOCK_DIRT) {
        return 1;
    }

    if (block_type == BLOCK_STONE) {
        return 2;
    }

    if (block_type == BLOCK_SAND) {
        return 6;
    }

    if (block_type == BLOCK_LOG) {
        return 4;
    }

    if (block_type == BLOCK_PLANKS) {
        return 7;
    }

    if (block_type == BLOCK_WORKBENCH) {
        return 8;
    }

    if (block_type == BLOCK_GRASS) {
        return 0;
    }

    return -1;
}

static void draw_pickaxe_sprite_rows(
    RenderContext *context,
    int x,
    int y,
    int pixel_size,
    const char * const *rows,
    int row_count,
    int z,
    int shadow_only
) {
    for (int row = 0; row < row_count; row++) {
        const char *line = rows[row];

        for (int col = 0; line[col] != 0; col++) {
            int r = 0;
            int g = 0;
            int b = 0;
            const char c = line[col];

            if (c == '.') {
                continue;
            }

            if (shadow_only) {
                r = 24;
                g = 20;
                b = 18;
            } else if (c == '2') {
                r = 94;
                g = 58;
                b = 30;
            } else if (c == '3') {
                r = 142;
                g = 92;
                b = 48;
            } else if (c == '4') {
                r = 90;
                g = 90;
                b = 96;
            } else if (c == '5') {
                r = 168;
                g = 168;
                b = 176;
            } else if (c == '6') {
                r = 214;
                g = 214;
                b = 220;
            } else if (c == '1') {
                r = 40;
                g = 40;
                b = 44;
            } else {
                continue;
            }

            draw_filled_rect(
                context,
                x + (col * pixel_size),
                y + (row * pixel_size),
                pixel_size,
                pixel_size,
                z,
                r,
                g,
                b
            );
        }
    }
}

static void draw_pickaxe_icon(
    RenderContext *context,
    int x,
    int y,
    int size,
    int z
) {
    static const char * const sprite[16] = {
        "................",
        "....1444441.....",
        "...145555541....",
        "..14455555541...",
        "...1444444441...",
        "......14331.....",
        ".....143331.....",
        "....1433331.....",
        "...14333331.....",
        "..143333331.....",
        ".1433333331.....",
        "..123333331.....",
        "...1233331......",
        "....12331.......",
        ".....121........",
        "................"
    };

    int pixel_size = size / 16;
    int draw_w;
    int draw_h;
    int draw_x;
    int draw_y;

    if (pixel_size < 1) {
        pixel_size = 1;
    }

    draw_w = 16 * pixel_size;
    draw_h = 16 * pixel_size;
    draw_x = x + ((size - draw_w) / 2);
    draw_y = y + ((size - draw_h) / 2);

    draw_pickaxe_sprite_rows(context, draw_x + 1, draw_y + 1, pixel_size, sprite, 16, z + 1, 1);
    draw_pickaxe_sprite_rows(context, draw_x, draw_y, pixel_size, sprite, 16, z, 0);
}

static void draw_pickaxe_held_overlay(RenderContext *context) {
    static const char * const sprite[16] = {
        "................",
        "................",
        "......1444441...",
        ".....145555541..",
        "....14455555541.",
        ".....1444444441.",
        ".........14331..",
        "........143331..",
        ".......1433331..",
        "......14333331..",
        ".....143333331..",
        "....1433333331..",
        ".....123333331..",
        "......1233331...",
        ".......12331....",
        "........121....."
    };

    const int pixel_size = 4;
    const int draw_x = 224;
    const int draw_y = 144;

    /*
     * Better-looking first-person overlay:
     * a large pixel-art sprite with a shadow and a simple hand shape behind it.
     * No behavior changes yet — visual only.
     */
    draw_filled_rect(context, 246, 188, 42, 28, 0, 176, 134, 96);
    draw_filled_rect(context, 252, 192, 34, 22, 0, 202, 158, 116);
    draw_filled_rect(context, 258, 195, 18, 16, 0, 224, 182, 136);

    draw_pickaxe_sprite_rows(context, draw_x + 3, draw_y + 3, pixel_size, sprite, 16, 1, 1);
    draw_pickaxe_sprite_rows(context, draw_x, draw_y, pixel_size, sprite, 16, 0, 0);
}

static void draw_item_icon(
    RenderContext *context,
    int x,
    int y,
    int size,
    uint8_t item_type,
    int z,
    int seed
) {
    const int texture_type = block_type_to_icon_texture(item_type);

    if (item_type == ITEM_WOOD_PICKAXE) {
        draw_pickaxe_icon(context, x, y, size, z);
        return;
    }

    if (texture_type >= 0) {
        draw_minecraft_texture_block(
            context,
            x,
            y,
            size,
            texture_type,
            z,
            seed
        );
    }
}

static void draw_stack_count(RenderContext *context, int x, int y, int count) {
    char text_buffer[4];
    int bg_w = 10;

    if (count <= 1) {
        return;
    }

    if (count >= 10) {
        text_buffer[0] = (char)('0' + (count / 10));
        text_buffer[1] = (char)('0' + (count % 10));
        text_buffer[2] = 0;
        bg_w = 16;
    } else {
        text_buffer[0] = (char)('0' + count);
        text_buffer[1] = 0;
    }

    /*
     * Keep stack numbers readable over noisy block icons.
     * The small dark plate also makes the number feel closer to Minecraft UI.
     */
    draw_filled_rect(context, x - 2, y - 1, bg_w, 8, 1, 12, 12, 12);
    draw_text(context, x, y, 0, text_buffer);
}

static void draw_hotbar_slot(RenderContext *context, int slot_index, int selected) {
    const int x = HOTBAR_START_X + (slot_index * (HOTBAR_SLOT_SIZE + HOTBAR_SLOT_GAP));
    const int y = HOTBAR_Y;
    const ItemStack *stack = &(game_state.inventory.hotbar_slot_blocks[slot_index]);
    const int block_type = stack_is_empty(stack) ? BLOCK_AIR : stack->type;
    const int texture_type = block_type_to_icon_texture(block_type);

    if (selected) {
        draw_filled_rect(context, x - 2, y - 2, HOTBAR_SLOT_SIZE + 4, HOTBAR_SLOT_SIZE + 4, 2, 238, 238, 238);
        draw_filled_rect(context, x, y, HOTBAR_SLOT_SIZE, HOTBAR_SLOT_SIZE, 1, 92, 92, 92);
    } else {
        draw_filled_rect(context, x, y, HOTBAR_SLOT_SIZE, HOTBAR_SLOT_SIZE, 2, 42, 42, 42);
        draw_filled_rect(context, x + 2, y + 2, HOTBAR_SLOT_SIZE - 4, HOTBAR_SLOT_SIZE - 4, 1, 78, 78, 78);
    }

    draw_line(context, x, y, x + HOTBAR_SLOT_SIZE - 1, y, 0, 210, 210, 210);
    draw_line(context, x, y, x, y + HOTBAR_SLOT_SIZE - 1, 0, 210, 210, 210);
    draw_line(context, x + HOTBAR_SLOT_SIZE - 1, y, x + HOTBAR_SLOT_SIZE - 1, y + HOTBAR_SLOT_SIZE - 1, 0, 18, 18, 18);
    draw_line(context, x, y + HOTBAR_SLOT_SIZE - 1, x + HOTBAR_SLOT_SIZE - 1, y + HOTBAR_SLOT_SIZE - 1, 0, 18, 18, 18);

    if (!stack_is_empty(stack)) {
        draw_stack_count(context, x + 8, y + 12, stack->count);

        draw_item_icon(
            context,
            x + 4,
            y + 4,
            HOTBAR_SLOT_SIZE - 8,
            stack->type,
            0,
            200 + slot_index
        );
    }
}

static void draw_hotbar(RenderContext *context) {
    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        draw_hotbar_slot(context, i, i == game_state.inventory.selected_hotbar_slot);
    }
}

static void draw_heart(RenderContext *context, int x, int y, int filled) {
    if (filled) {
        draw_filled_rect(context, x + 2, y, 4, 4, 0, 210, 32, 42);
        draw_filled_rect(context, x + 8, y, 4, 4, 0, 210, 32, 42);
        draw_filled_rect(context, x, y + 3, 14, 5, 0, 210, 32, 42);
        draw_filled_rect(context, x + 2, y + 8, 10, 3, 0, 210, 32, 42);
        draw_filled_rect(context, x + 5, y + 11, 4, 3, 0, 210, 32, 42);

        draw_filled_rect(context, x + 3, y + 2, 2, 2, 0, 255, 118, 118);
        draw_line(context, x, y + 4, x + 13, y + 4, 0, 128, 18, 26);
    } else {
        draw_line(context, x + 2, y, x + 5, y, 0, 80, 22, 26);
        draw_line(context, x + 8, y, x + 11, y, 0, 80, 22, 26);
        draw_line(context, x, y + 4, x + 13, y + 4, 0, 80, 22, 26);
        draw_line(context, x + 2, y + 8, x + 11, y + 8, 0, 80, 22, 26);
        draw_line(context, x + 5, y + 12, x + 8, y + 12, 0, 80, 22, 26);
    }
}

static void draw_hearts(RenderContext *context) {
    for (int i = 0; i < HEART_COUNT; i++) {
        draw_heart(
            context,
            HEART_START_X + (i * 18),
            HEART_Y,
            i < game_state.player.player_health_hearts
        );
    }
}

static void draw_held_item_in_hand(RenderContext *context) {
    const int selected_type = get_selected_hotbar_block_type();

    if (selected_type == ITEM_WOOD_PICKAXE) {
        draw_pickaxe_held_overlay(context);
    } else if (is_placeable_block_type(selected_type)) {
        draw_panel(context, 238, 166, 34, 34, 2, 58, 44, 34, 172, 132, 92);
        draw_item_icon(context, 246, 174, 18, (uint8_t)selected_type, 0, 1300 + selected_type);
    }
}

static void draw_game_hud(RenderContext *context) {
    draw_hearts(context);
    draw_hotbar(context);

    if (game_state.app.hud_visible) {
        draw_panel(context, 6, 8, 148, 64, 2, 32, 36, 42, 174, 174, 174);
        draw_text(context, 16, 18, 0, "MINECRAFT PS1");
        draw_text(context, 16, 34, 0, game_state.player.fly_mode_enabled ? "MODE: FLY" : "MODE: WALK");
        draw_text(context, 16, 50, 0, game_state.player.autojump_enabled ? "SQUARE HOLD BREAK" : "AUTOJUMP OFF");
    }

    if (game_state.app.system_status_timer > 0) {
        draw_minecraft_button(context, 8, game_state.app.hud_visible ? 80 : 8, 112, 18, 0);
        draw_text(context, 18, game_state.app.hud_visible ? 84 : 12, 0, game_state.app.system_status_text);
    }
}
