/*
 * ui/workbench_ui.c
 *
 * Workbench screen with 3x3 crafting grid and player storage/hotbar.
 * Some recipe logic is still here temporarily and will move to game/crafting.c later.
 */

#include "workbench_ui.h"

static int is_workbench_2x2_plank_recipe_at(int start_col, int start_row) {
    for (int row = 0; row < WORKBENCH_CRAFT_ROWS; row++) {
        for (int col = 0; col < WORKBENCH_CRAFT_COLS; col++) {
            const int index = (row * WORKBENCH_CRAFT_COLS) + col;
            const int inside =
                col >= start_col &&
                col < start_col + 2 &&
                row >= start_row &&
                row < start_row + 2;

            if (inside) {
                if (
                    workbench_crafting_slots[index].type != BLOCK_PLANKS ||
                    workbench_crafting_slots[index].count == 0
                ) {
                    return 0;
                }
            } else if (!stack_is_empty(&(workbench_crafting_slots[index]))) {
                return 0;
            }
        }
    }

    return 1;
}


static int find_workbench_2x2_plank_recipe_start(void) {
    for (int row = 0; row <= 1; row++) {
        for (int col = 0; col <= 1; col++) {
            if (is_workbench_2x2_plank_recipe_at(col, row)) {
                return (row * WORKBENCH_CRAFT_COLS) + col;
            }
        }
    }

    return -1;
}


static ItemStack get_workbench_output_stack(void) {
    ItemStack result;
    int log_slot = -1;
    int non_empty_count = 0;
    const int plank_recipe_start = find_workbench_2x2_plank_recipe_start();

    result.type = BLOCK_AIR;
    result.count = 0;

    for (int i = 0; i < WORKBENCH_CRAFT_SLOT_COUNT; i++) {
        if (stack_is_empty(&(workbench_crafting_slots[i]))) {
            continue;
        }

        non_empty_count++;

        if (
            workbench_crafting_slots[i].type == BLOCK_LOG &&
            workbench_crafting_slots[i].count >= 1 &&
            log_slot < 0
        ) {
            log_slot = i;
        }
    }

    if (non_empty_count == 1 && log_slot >= 0) {
        result.type = BLOCK_PLANKS;
        result.count = 4;
        return result;
    }

    if (plank_recipe_start >= 0) {
        result.type = BLOCK_WORKBENCH;
        result.count = 1;
        return result;
    }

    return result;
}


static void consume_current_workbench_inputs(const ItemStack *output) {
    if (output->type == BLOCK_PLANKS) {
        for (int i = 0; i < WORKBENCH_CRAFT_SLOT_COUNT; i++) {
            if (workbench_crafting_slots[i].type == BLOCK_LOG && workbench_crafting_slots[i].count > 0) {
                workbench_crafting_slots[i].count--;
                normalize_stack(&(workbench_crafting_slots[i]));
                return;
            }
        }
    }

    if (output->type == BLOCK_WORKBENCH) {
        const int start = find_workbench_2x2_plank_recipe_start();

        if (start >= 0) {
            const int start_col = start % WORKBENCH_CRAFT_COLS;
            const int start_row = start / WORKBENCH_CRAFT_COLS;

            for (int row = start_row; row < start_row + 2; row++) {
                for (int col = start_col; col < start_col + 2; col++) {
                    const int index = (row * WORKBENCH_CRAFT_COLS) + col;

                    if (
                        workbench_crafting_slots[index].type == BLOCK_PLANKS &&
                        workbench_crafting_slots[index].count > 0
                    ) {
                        workbench_crafting_slots[index].count--;
                        normalize_stack(&(workbench_crafting_slots[index]));
                    }
                }
            }
        }
    }
}


static void take_workbench_output(void) {
    const ItemStack output = get_workbench_output_stack();

    if (stack_is_empty(&output)) {
        set_system_status("NO RECIPE", 45);
        return;
    }

    if (!can_add_stack_to_hand(&output)) {
        set_system_status("HAND FULL", 45);
        return;
    }

    add_stack_to_hand(&output);
    consume_current_workbench_inputs(&output);

    if (output.type == BLOCK_WORKBENCH) {
        set_system_status("CRAFTED BENCH", 55);
    } else {
        set_system_status("CRAFTED PLANKS", 55);
    }
}


static int quick_craft_workbench_output_to_inventory(void) {
    int crafted_count = 0;
    int guard = 0;

    for (;;) {
        const ItemStack output = get_workbench_output_stack();

        if (stack_is_empty(&output)) {
            break;
        }

        if (get_inventory_accept_capacity(output.type) < output.count) {
            break;
        }

        if (add_items_to_inventory(output.type, output.count) != 0) {
            break;
        }

        consume_current_workbench_inputs(&output);
        crafted_count += output.count;

        guard++;

        if (guard > STACK_MAX_COUNT) {
            break;
        }
    }

    if (crafted_count > 0) {
        set_system_status("SHIFT CRAFTED", 70);
        return 1;
    }

    set_system_status("NO SPACE", 55);
    return 0;
}


static void swap_workbench_held_with_cursor(void) {
    ItemStack *slot;

    if (workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
        take_workbench_output();
        return;
    }

    slot = get_workbench_cursor_stack_ptr();
    merge_or_swap_inventory_held_with_slot(slot);
}


static void quick_move_workbench_cursor_to_hotbar(void) {
    ItemStack *slot;

    if (workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
        return;
    }

    slot = get_workbench_cursor_stack_ptr();

    if (!stack_is_empty(slot)) {
        const int remaining = add_items_to_inventory(slot->type, slot->count);

        if (remaining == 0) {
            slot->type = BLOCK_AIR;
            slot->count = 0;
            set_system_status("QUICK MOVED", 45);
        } else {
            slot->count = (uint8_t)remaining;
            normalize_stack(slot);
            set_system_status("NO SPACE", 45);
        }
    }
}


static void move_workbench_cursor(int dx, int dy) {
    if (workbench_cursor_slot < WORKBENCH_CURSOR_OUTPUT) {
        int row = workbench_cursor_slot / WORKBENCH_CRAFT_COLS;
        int col = workbench_cursor_slot % WORKBENCH_CRAFT_COLS;

        if (dx > 0 && col == WORKBENCH_CRAFT_COLS - 1) {
            workbench_cursor_slot = WORKBENCH_CURSOR_OUTPUT;
            return;
        }

        if (dy > 0 && row == WORKBENCH_CRAFT_ROWS - 1) {
            workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + col;
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

        workbench_cursor_slot = (row * WORKBENCH_CRAFT_COLS) + col;
        return;
    }

    if (workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
        if (dx < 0) {
            workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + 5;
            return;
        }

        if (dy > 0) {
            workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + 4;
            return;
        }

        if (dy < 0) {
            workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + 2;
            return;
        }

        return;
    }

    if (workbench_cursor_slot < WORKBENCH_CURSOR_HOTBAR_START) {
        int local = workbench_cursor_slot - WORKBENCH_CURSOR_STORAGE_START;
        int row = local / INVENTORY_STORAGE_COLS;
        int col = local % INVENTORY_STORAGE_COLS;

        if (dy < 0 && row == 0) {
            if (col < 3) {
                workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START + (2 * WORKBENCH_CRAFT_COLS) + col;
            } else {
                workbench_cursor_slot = WORKBENCH_CURSOR_OUTPUT;
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
            workbench_cursor_slot = WORKBENCH_CURSOR_HOTBAR_START + col;
            return;
        }

        workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + (row * INVENTORY_STORAGE_COLS) + col;
        return;
    }

    {
        int col = workbench_cursor_slot - WORKBENCH_CURSOR_HOTBAR_START;

        col += dx;

        if (col < 0) {
            col = INVENTORY_STORAGE_COLS - 1;
        }

        if (col >= INVENTORY_STORAGE_COLS) {
            col = 0;
        }

        if (dy < 0) {
            workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + ((INVENTORY_STORAGE_ROWS - 1) * INVENTORY_STORAGE_COLS) + col;
        } else if (dy > 0) {
            workbench_cursor_slot = WORKBENCH_CURSOR_STORAGE_START + col;
        } else {
            workbench_cursor_slot = WORKBENCH_CURSOR_HOTBAR_START + col;
        }
    }
}


static void update_workbench_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        app_state = APP_STATE_PLAY;
        pad_previous_buttons = buttons;
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
        if ((buttons & PAD_R2) && workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
            quick_craft_workbench_output_to_inventory();
        } else {
            swap_workbench_held_with_cursor();
        }
    }

    if (pressed_this_frame & PAD_SQUARE) {
        quick_move_workbench_cursor_to_hotbar();
    }

    pad_previous_buttons = buttons;
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
            workbench_crafting_slots[i].type,
            workbench_crafting_slots[i].count,
            workbench_cursor_slot == i,
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
            inventory_storage_blocks[i].type,
            inventory_storage_blocks[i].count,
            workbench_cursor_slot == WORKBENCH_CURSOR_STORAGE_START + i,
            0,
            1000 + i
        );
    }

    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        draw_inventory_slot(
            context,
            storage_x + (i * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP)),
            hotbar_y,
            hotbar_slot_blocks[i].type,
            hotbar_slot_blocks[i].count,
            workbench_cursor_slot == WORKBENCH_CURSOR_HOTBAR_START + i,
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
        workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT,
        stack_is_empty(&output),
        930
    );

    draw_workbench_storage_grid(context);

    draw_text(context, 56, 188, 0, "DPAD MOVE  CROSS/CIRCLE PICK/SWAP/CRAFT");
    draw_text(context, 56, 202, 0, "R2+X SHIFT-CRAFT  START BACK");

    if (!stack_is_empty(&inventory_held_stack)) {
        const int texture_type = block_type_to_icon_texture(inventory_held_stack.type);

        draw_text(context, 214, 56, 0, "HAND");
        draw_inventory_slot(
            context,
            256,
            52,
            inventory_held_stack.type,
            inventory_held_stack.count,
            1,
            0,
            1200 + texture_type
        );
    }

    if (system_status_timer > 0) {
        draw_minecraft_button(context, 104, 226, 112, 14, 0);
        draw_text(context, 122, 227, 0, system_status_text);
    }
}
