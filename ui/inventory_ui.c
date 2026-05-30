/*
 * ui/inventory_ui.c
 *
 * Player inventory screen, 2x2 crafting screen, and inventory cursor actions.
 * Some gameplay-adjacent inventory logic still lives here temporarily and will
 * move to game/inventory.c and game/crafting.c in a later refactor.
 */

#include "inventory_ui.h"

static void move_inventory_cursor(int dx, int dy) {
    if (game_state.inventory.inventory_cursor_slot < INVENTORY_STORAGE_SLOT_COUNT) {
        int row = game_state.inventory.inventory_cursor_slot / INVENTORY_STORAGE_COLS;
        int col = game_state.inventory.inventory_cursor_slot % INVENTORY_STORAGE_COLS;

        if (dy < 0 && row == 0) {
            if (col >= 6 && col <= 7) {
                game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + (1 * 2) + (col - 6);
                return;
            }

            if (col >= 8) {
                game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_OUTPUT;
                return;
            }
        }

        col += dx;
        row += dy;

        if (col < 0) {
            col = INVENTORY_STORAGE_COLS - 1;
        }

        if (col >= INVENTORY_STORAGE_COLS) {
            col = 0;
        }

        if (row < 0) {
            row = INVENTORY_STORAGE_ROWS;
        }

        if (row > INVENTORY_STORAGE_ROWS) {
            row = 0;
        }

        if (row < INVENTORY_STORAGE_ROWS) {
            game_state.inventory.inventory_cursor_slot = (row * INVENTORY_STORAGE_COLS) + col;
        } else {
            game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_HOTBAR_START + col;
        }

        return;
    }

    if (game_state.inventory.inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_START) {
        int col = game_state.inventory.inventory_cursor_slot - INVENTORY_CURSOR_HOTBAR_START;

        col += dx;

        if (col < 0) {
            col = INVENTORY_STORAGE_COLS - 1;
        }

        if (col >= INVENTORY_STORAGE_COLS) {
            col = 0;
        }

        if (dy < 0) {
            game_state.inventory.inventory_cursor_slot = ((INVENTORY_STORAGE_ROWS - 1) * INVENTORY_STORAGE_COLS) + col;
        } else if (dy > 0) {
            game_state.inventory.inventory_cursor_slot = col;
        } else {
            game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_HOTBAR_START + col;
        }

        return;
    }

    if (game_state.inventory.inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_OUTPUT) {
        int local = game_state.inventory.inventory_cursor_slot - INVENTORY_CURSOR_CRAFT_START;
        int row = local / 2;
        int col = local % 2;

        if (dx > 0 && col == 1) {
            game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_OUTPUT;
            return;
        }

        if (dy > 0 && row == 1) {
            game_state.inventory.inventory_cursor_slot = col + 6;
            return;
        }

        col += dx;
        row += dy;

        if (col < 0) {
            col = 1;
        }

        if (col > 1) {
            col = 0;
        }

        if (row < 0) {
            row = 1;
        }

        if (row > 1) {
            row = 0;
        }

        game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + (row * 2) + col;
        return;
    }

    /*
     * Crafting output slot.
     */
    if (dx < 0) {
        game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + 3;
        return;
    }

    if (dy > 0) {
        game_state.inventory.inventory_cursor_slot = 8;
        return;
    }

    if (dy < 0) {
        game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + 1;
        return;
    }
}




















static void draw_inventory_slot(
    RenderContext *context,
    int x,
    int y,
    uint8_t block_type,
    uint8_t count,
    int selected,
    int disabled,
    int seed
) {
    if (selected) {
        draw_filled_rect(context, x - 2, y - 2, INVENTORY_SLOT_SIZE + 4, INVENTORY_SLOT_SIZE + 4, 1, 244, 244, 244);
    }

    if (disabled) {
        draw_filled_rect(context, x, y, INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE, 3, 46, 46, 50);
        draw_filled_rect(context, x + 2, y + 2, INVENTORY_SLOT_SIZE - 4, INVENTORY_SLOT_SIZE - 4, 2, 36, 36, 40);
    } else {
        draw_filled_rect(context, x, y, INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE, 3, 66, 66, 70);
        draw_filled_rect(context, x + 2, y + 2, INVENTORY_SLOT_SIZE - 4, INVENTORY_SLOT_SIZE - 4, 2, 42, 42, 46);
    }

    draw_line(context, x, y, x + INVENTORY_SLOT_SIZE - 1, y, 0, 198, 198, 198);
    draw_line(context, x, y, x, y + INVENTORY_SLOT_SIZE - 1, 0, 198, 198, 198);
    draw_line(context, x + INVENTORY_SLOT_SIZE - 1, y, x + INVENTORY_SLOT_SIZE - 1, y + INVENTORY_SLOT_SIZE - 1, 0, 22, 22, 22);
    draw_line(context, x, y + INVENTORY_SLOT_SIZE - 1, x + INVENTORY_SLOT_SIZE - 1, y + INVENTORY_SLOT_SIZE - 1, 0, 22, 22, 22);

    if (count > 0) {
        draw_stack_count(context, x + 7, y + 10, count);

        draw_item_icon(
            context,
            x + 3,
            y + 3,
            INVENTORY_SLOT_SIZE - 6,
            block_type,
            0,
            seed
        );
    }
}


static void draw_inventory_grid(RenderContext *context) {
    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        const int row = i / INVENTORY_STORAGE_COLS;
        const int col = i % INVENTORY_STORAGE_COLS;
        const int x = INVENTORY_STORAGE_START_X + (col * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP));
        const int y = INVENTORY_STORAGE_START_Y + (row * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP));

        draw_inventory_slot(
            context,
            x,
            y,
            game_state.inventory.inventory_storage_blocks[i].type,
            game_state.inventory.inventory_storage_blocks[i].count,
            game_state.inventory.inventory_cursor_slot == i,
            0,
            300 + i
        );
    }

    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        const int x = INVENTORY_HOTBAR_START_X + (i * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP));
        const int y = INVENTORY_HOTBAR_Y;
        const int cursor_index = INVENTORY_CURSOR_HOTBAR_START + i;

        draw_inventory_slot(
            context,
            x,
            y,
            game_state.inventory.hotbar_slot_blocks[i].type,
            game_state.inventory.hotbar_slot_blocks[i].count,
            game_state.inventory.inventory_cursor_slot == cursor_index,
            0,
            500 + i
        );
    }
}


static void draw_inventory_crafting_area(RenderContext *context) {
    const ItemStack output = get_crafting_output_stack();

    draw_text(context, 188, 34, 0, "CRAFTING");

    draw_inventory_slot(
        context,
        190,
        48,
        game_state.inventory.crafting_slots[0].type,
        game_state.inventory.crafting_slots[0].count,
        game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START,
        0,
        600
    );
    draw_inventory_slot(
        context,
        210,
        48,
        game_state.inventory.crafting_slots[1].type,
        game_state.inventory.crafting_slots[1].count,
        game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 1,
        0,
        601
    );
    draw_inventory_slot(
        context,
        190,
        68,
        game_state.inventory.crafting_slots[2].type,
        game_state.inventory.crafting_slots[2].count,
        game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 2,
        0,
        602
    );
    draw_inventory_slot(
        context,
        210,
        68,
        game_state.inventory.crafting_slots[3].type,
        game_state.inventory.crafting_slots[3].count,
        game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 3,
        0,
        603
    );

    draw_text(context, 234, 59, 0, ">");
    draw_inventory_slot(
        context,
        250,
        58,
        output.type,
        output.count,
        game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT,
        stack_is_empty(&output),
        604
    );
}


static void draw_inventory_armor_area(RenderContext *context) {
    draw_text(context, 26, 42, 0, "ARMOR");
    draw_inventory_slot(context, 28, 58, BLOCK_AIR, 0, 0, 1, 700);
    draw_inventory_slot(context, 28, 78, BLOCK_AIR, 0, 0, 1, 701);
    draw_inventory_slot(context, 28, 98, BLOCK_AIR, 0, 0, 1, 702);
    draw_inventory_slot(context, 28, 118, BLOCK_AIR, 0, 0, 1, 703);
}


static void draw_inventory_player_preview(RenderContext *context) {
    draw_panel(context, 74, 40, 84, 44, 2, 42, 50, 60, 174, 174, 174);
    draw_minecraft_texture_block(context, 84, 50, 18, 0, 1, 740);
    draw_minecraft_texture_block(context, 104, 50, 18, 1, 1, 741);
    draw_minecraft_texture_block(context, 124, 50, 18, 2, 1, 742);
    draw_text(context, 82, 72, 0, "BLOCK PLAYER");
}


static void draw_inventory_screen(RenderContext *context) {
    draw_filled_rect(context, 0, 0, SCREEN_W, SCREEN_H, 7, 16, 16, 20);
    draw_panel(context, 16, 16, 288, 206, 4, 88, 88, 88, 214, 214, 214);

    draw_text(context, 124, 24, 0, "INVENTORY");

    draw_inventory_armor_area(context);
    draw_inventory_player_preview(context);
    draw_inventory_crafting_area(context);

    draw_text(context, 70, 82, 0, "STORAGE");
    draw_inventory_grid(context);

    draw_text(context, 70, 182, 0, "DPAD MOVE  CROSS/CIRCLE PICK/SWAP/CRAFT");
    draw_text(context, 70, 196, 0, "R2+X SHIFT-CRAFT  START BACK");

    if (!stack_is_empty(&game_state.inventory.inventory_held_stack)) {
        const int texture_type = block_type_to_icon_texture(game_state.inventory.inventory_held_stack.type);

        draw_text(context, 214, 182, 0, "HAND");
        draw_inventory_slot(
            context,
            256,
            178,
            game_state.inventory.inventory_held_stack.type,
            game_state.inventory.inventory_held_stack.count,
            1,
            0,
            800 + texture_type
        );
    }

    if (game_state.app.system_status_timer > 0) {
        draw_minecraft_button(context, 104, 224, 112, 16, 0);
        draw_text(context, 122, 226, 0, game_state.app.system_status_text);
    }
}


static void update_inventory_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        game_state.app.app_state = APP_STATE_PLAY;
        pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_UP) {
        move_inventory_cursor(0, -1);
    }

    if (pressed_this_frame & PAD_DOWN) {
        move_inventory_cursor(0, 1);
    }

    if (pressed_this_frame & PAD_LEFT) {
        move_inventory_cursor(-1, 0);
    }

    if (pressed_this_frame & PAD_RIGHT) {
        move_inventory_cursor(1, 0);
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE)
    ) {
        if ((buttons & PAD_R2) && game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
            quick_craft_output_to_inventory();
        } else {
            swap_inventory_held_with_cursor();
        }
    }

    if (pressed_this_frame & PAD_SQUARE) {
        quick_move_inventory_slot_to_hotbar();
    }

    pad_previous_buttons = buttons;
}
