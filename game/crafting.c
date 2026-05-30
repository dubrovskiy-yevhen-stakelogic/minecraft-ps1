/*
 * game/crafting.c
 *
 * Player 2x2 crafting and workbench 3x3 crafting recipe logic.
 * Transitional stage: included directly from main.c.
 */

#include "crafting.h"

static ItemStack get_crafting_output_stack(void) {
    ItemStack result;
    int log_slot = -1;
    int non_empty_count = 0;
    int plank_count = 0;

    result.type = BLOCK_AIR;
    result.count = 0;

    for (int i = 0; i < CRAFT_SLOT_COUNT; i++) {
        if (stack_is_empty(&(crafting_slots[i]))) {
            continue;
        }

        non_empty_count++;

        /*
         * Recipe 1:
         * one stack of logs in any crafting slot produces 4 planks.
         * The stack may contain any count; taking the output consumes only 1 log.
         */
        if (crafting_slots[i].type == BLOCK_LOG && crafting_slots[i].count >= 1 && log_slot < 0) {
            log_slot = i;
        }

        /*
         * Recipe 2:
         * four planks, one in each 2x2 slot, produce a workbench.
         * Each slot can contain a stack; crafting consumes 1 plank per slot.
         */
        if (crafting_slots[i].type == BLOCK_PLANKS && crafting_slots[i].count >= 1) {
            plank_count++;
        }
    }

    if (non_empty_count == 1 && log_slot >= 0) {
        result.type = BLOCK_PLANKS;
        result.count = 4;
        return result;
    }

    if (non_empty_count == CRAFT_SLOT_COUNT && plank_count == CRAFT_SLOT_COUNT) {
        result.type = BLOCK_WORKBENCH;
        result.count = 1;
        return result;
    }

    return result;
}


static void consume_current_crafting_inputs(const ItemStack *output) {
    if (output->type == BLOCK_PLANKS) {
        for (int i = 0; i < CRAFT_SLOT_COUNT; i++) {
            if (crafting_slots[i].type == BLOCK_LOG && crafting_slots[i].count > 0) {
                crafting_slots[i].count--;
                normalize_stack(&(crafting_slots[i]));
                return;
            }
        }
    }

    if (output->type == BLOCK_WORKBENCH) {
        for (int i = 0; i < CRAFT_SLOT_COUNT; i++) {
            if (crafting_slots[i].type == BLOCK_PLANKS && crafting_slots[i].count > 0) {
                crafting_slots[i].count--;
                normalize_stack(&(crafting_slots[i]));
            }
        }
    }
}


static void take_crafting_output(void) {
    const ItemStack output = get_crafting_output_stack();

    if (stack_is_empty(&output)) {
        set_system_status("NO RECIPE", 45);
        return;
    }

    if (!can_add_stack_to_hand(&output)) {
        set_system_status("HAND FULL", 45);
        return;
    }

    add_stack_to_hand(&output);
    consume_current_crafting_inputs(&output);

    if (output.type == BLOCK_WORKBENCH) {
        set_system_status("CRAFTED BENCH", 55);
    } else {
        set_system_status("CRAFTED PLANKS", 55);
    }
}


static int quick_craft_output_to_inventory(void) {
    int crafted_count = 0;
    int guard = 0;

    for (;;) {
        const ItemStack output = get_crafting_output_stack();

        if (stack_is_empty(&output)) {
            break;
        }

        if (get_inventory_accept_capacity(output.type) < output.count) {
            break;
        }

        if (add_items_to_inventory(output.type, output.count) != 0) {
            break;
        }

        consume_current_crafting_inputs(&output);
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
