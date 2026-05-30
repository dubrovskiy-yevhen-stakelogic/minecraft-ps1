/*
 * ui/workbench_ui.c
 *
 * Workbench screen with 3x3 crafting grid and player storage/hotbar.
 * Some recipe logic is still here temporarily and will move to game/crafting.c later.
 */

#include "workbench_ui.h"

















static void move_workbench_cursor(int dx, int dy) {
    if (game_state.inventory.workbench_cursor_slot < WORKBENCH_CURSOR_OUTPUT) {
        int row = game_state.inventory.workbench_cursor_slot / WORKBENCH_CRAFT_COLS;
        int col = game_state.inventory.workbench_cursor_slot % WORKBENCH_CRAFT_COLS;

        if (dx > 0 && col == WORKBENCH_CRAFT_COLS - 1) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_OUTPUT;
            return;
        }

        if (dy > 0 && row == WORKBENCH_CRAFT_ROWS - 1) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + col;
            return;
        }

        col += dx;
        row += dy;

        if (col < 0) {
            col = WORKBENCH_CRAFT_COLS - 1;
        }

        if (col >= WORKBENCH_CRAFT_COLS) {
            col = 0;
        }

        if (row < 0) {
            row = WORKBENCH_CRAFT_ROWS - 1;
        }

        if (row >= WORKBENCH_CRAFT_ROWS) {
            row = 0;
        }

        game_state.inventory.workbench_cursor_slot = (row * WORKBENCH_CRAFT_COLS) + col;
        return;
    }

    if (game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
        if (dx < 0) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + 5;
            return;
        }

        if (dy > 0) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + 4;
            return;
        }

        if (dy < 0) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + 2;
            return;
        }

        return;
    }

    if (game_state.inventory.workbench_cursor_slot < WORKBENCH_CURSOR_HOTBAR_START) {
        int local = game_state.inventory.workbench_cursor_slot - WORKBENCH_CURSOR_STORAGE_START;
        int row = local / INVENTORY_STORAGE_COLS;
        int col = local % INVENTORY_STORAGE_COLS;

        if (dy < 0 && row == 0) {
            if (col < 3) {
                game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + (2 * WORKBENCH_CRAFT_COLS) + col;
            } else {
                game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_OUTPUT;
            }
            return;
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
            row = INVENTORY_STORAGE_ROWS - 1;
        }

        if (row >= INVENTORY_STORAGE_ROWS) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_HOTBAR_START + col;
            return;
        }

        game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + (row * INVENTORY_STORAGE_COLS) + col;
        return;
    }

    {
        int col = game_state.inventory.workbench_cursor_slot - WORKBENCH_CURSOR_HOTBAR_START;

        col += dx;

        if (col < 0) {
            col = INVENTORY_STORAGE_COLS - 1;
        }

        if (col >= INVENTORY_STORAGE_COLS) {
            col = 0;
        }

        if (dy < 0) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + ((INVENTORY_STORAGE_ROWS - 1) * INVENTORY_STORAGE_COLS) + col;
        } else if (dy > 0) {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + col;
        } else {
            game_state.inventory.workbench_cursor_slot = WORKBENCH_CURSOR_HOTBAR_START + col;
        }
    }
}


static void update_workbench_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~game_state.input.pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        game_state.app.app_state = APP_STATE_PLAY;
        game_state.input.pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_UP) {
        move_workbench_cursor(0, -1);
    }

    if (pressed_this_frame & PAD_DOWN) {
        move_workbench_cursor(0, 1);
    }

    if (pressed_this_frame & PAD_LEFT) {
        move_workbench_cursor(-1, 0);
    }

    if (pressed_this_frame & PAD_RIGHT) {
        move_workbench_cursor(1, 0);
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE)
    ) {
        if ((buttons & PAD_R2) && game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
            quick_craft_workbench_output_to_inventory();
        } else {
            swap_workbench_held_with_cursor();
        }
    }

    if (pressed_this_frame & PAD_SQUARE) {
        quick_move_workbench_cursor_to_hotbar();
    }

    game_state.input.pad_previous_buttons = buttons;
}


static void draw_workbench_crafting_grid(RenderContext *context) {
    const int start_x = 54;
    const int start_y = 36;

    draw_text(context, 54, 24, 0, "CRAFTING 3X3");

    for (int i = 0; i < WORKBENCH_CRAFT_SLOT_COUNT; i++) {
        const int row = i / WORKBENCH_CRAFT_COLS;
        const int col = i % WORKBENCH_CRAFT_COLS;

        draw_inventory_slot(
            context,
            start_x + (col * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            start_y + (row * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            game_state.inventory.workbench_crafting_slots[i].type,
            game_state.inventory.workbench_crafting_slots[i].count,
            game_state.inventory.workbench_cursor_slot == i,
            0,
            900 + i
        );
    }

    draw_text(context, 126, 58, 0, ">");
}


static void draw_workbench_storage_grid(RenderContext *context) {
    const int storage_x = 70;
    const int storage_y = 98;
    const int hotbar_y = 164;

    draw_text(context, storage_x, 88, 0, "STORAGE");

    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        const int row = i / INVENTORY_STORAGE_COLS;
        const int col = i % INVENTORY_STORAGE_COLS;

        draw_inventory_slot(
            context,
            storage_x + (col * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            storage_y + (row * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            game_state.inventory.inventory_storage_blocks[i].type,
            game_state.inventory.inventory_storage_blocks[i].count,
            game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_STORAGE_START + i,
            0,
            1000 + i
        );
    }

    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        draw_inventory_slot(
            context,
            storage_x + (i * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            hotbar_y,
            game_state.inventory.hotbar_slot_blocks[i].type,
            game_state.inventory.hotbar_slot_blocks[i].count,
            game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_HOTBAR_START + i,
            0,
            1100 + i
        );
    }
}


static void draw_workbench_screen(RenderContext *context) {
    const ItemStack output = get_workbench_output_stack();

    draw_filled_rect(context, 0, 0, SCREEN_W, SCREEN_H, 7, 16, 16, 20);
    draw_panel(context, 16, 12, 288, 216, 4, 88, 88, 88, 214, 214, 214);

    draw_text(context, 120, 20, 0, "WORKBENCH");

    draw_workbench_crafting_grid(context);

    draw_inventory_slot(
        context,
        150,
        52,
        output.type,
        output.count,
        game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT,
        stack_is_empty(&output),
        930
    );

    draw_workbench_storage_grid(context);

    draw_text(context, 56, 188, 0, "DPAD MOVE  CROSS/CIRCLE PICK/SWAP/CRAFT");
    draw_text(context, 56, 202, 0, "R2+X SHIFT-CRAFT  START BACK");

    if (!stack_is_empty(&game_state.inventory.inventory_held_stack)) {
        const int texture_type = block_type_to_icon_texture(game_state.inventory.inventory_held_stack.type);

        draw_text(context, 214, 56, 0, "HAND");
        draw_inventory_slot(
            context,
            256,
            52,
            game_state.inventory.inventory_held_stack.type,
            game_state.inventory.inventory_held_stack.count,
            1,
            0,
            1200 + texture_type
        );
    }

    if (game_state.app.system_status_timer > 0) {
        draw_minecraft_button(context, 104, 226, 112, 14, 0);
        draw_text(context, 122, 227, 0, game_state.app.system_status_text);
    }
}
