/*
 * game/inventory.c
 *
 * ItemStack helpers, hotbar/storage operations, block placement helpers and
 * inventory stack transfer logic.
 * Transitional stage: included directly from main.c.
 */

#include "inventory.h"

static void normalize_stack(ItemStack *stack) {
    if (stack->count == 0 || stack->type == BLOCK_AIR) {
        stack->type = BLOCK_AIR;
        stack->count = 0;
        return;
    }

    if (stack->count > STACK_MAX_COUNT) {
        stack->count = STACK_MAX_COUNT;
    }
}

static int stack_is_empty(const ItemStack *stack) {
    return stack->type == BLOCK_AIR || stack->count == 0;
}

static int is_placeable_block_type(int item_type) {
    return
        item_type == BLOCK_DIRT ||
        item_type == BLOCK_GRASS ||
        item_type == BLOCK_COBBLESTONE ||
        item_type == BLOCK_SAND ||
        item_type == BLOCK_LOG ||
        item_type == BLOCK_PLANKS ||
        item_type == BLOCK_WORKBENCH;
}

static int add_to_existing_stack_array(ItemStack *stacks, int count, uint8_t type, int amount) {
    int remaining = amount;

    if (type == BLOCK_AIR || amount <= 0) {
        return 0;
    }

    for (int i = 0; i < count && remaining > 0; i++) {
        if (stacks[i].type == type && stacks[i].count < STACK_MAX_COUNT) {
            const int space = STACK_MAX_COUNT - stacks[i].count;
            const int add_count = remaining < space ? remaining : space;

            stacks[i].count = (uint8_t)(stacks[i].count + add_count);
            remaining -= add_count;
        }
    }

    return remaining;
}

static int add_to_empty_stack_array(ItemStack *stacks, int count, uint8_t type, int amount) {
    int remaining = amount;

    if (type == BLOCK_AIR || amount <= 0) {
        return 0;
    }

    for (int i = 0; i < count && remaining > 0; i++) {
        if (stack_is_empty(&(stacks[i]))) {
            const int add_count = remaining < STACK_MAX_COUNT ? remaining : STACK_MAX_COUNT;

            stacks[i].type = type;
            stacks[i].count = (uint8_t)add_count;
            remaining -= add_count;
        }
    }

    return remaining;
}

static int get_inventory_accept_capacity(uint8_t type) {
    int capacity = 0;

    if (type == BLOCK_AIR) {
        return 0;
    }

    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        if (game_state.inventory.hotbar_slot_blocks[i].type == type) {
            capacity += STACK_MAX_COUNT - game_state.inventory.hotbar_slot_blocks[i].count;
        } else if (stack_is_empty(&(game_state.inventory.hotbar_slot_blocks[i]))) {
            capacity += STACK_MAX_COUNT;
        }
    }

    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        if (game_state.inventory.inventory_storage_blocks[i].type == type) {
            capacity += STACK_MAX_COUNT - game_state.inventory.inventory_storage_blocks[i].count;
        } else if (stack_is_empty(&(game_state.inventory.inventory_storage_blocks[i]))) {
            capacity += STACK_MAX_COUNT;
        }
    }

    return capacity;
}

static int add_items_to_inventory(uint8_t type, int amount) {
    int remaining;

    if (type == BLOCK_AIR || amount <= 0) {
        return 0;
    }

    /*
     * Minecraft-like quick transfer behavior:
     * first merge into existing stacks anywhere, then create new stacks.
     */
    remaining = add_to_existing_stack_array(
        game_state.inventory.hotbar_slot_blocks,
        HOTBAR_SLOT_COUNT,
        type,
        amount
    );

    remaining = add_to_existing_stack_array(
        game_state.inventory.inventory_storage_blocks,
        INVENTORY_STORAGE_SLOT_COUNT,
        type,
        remaining
    );

    remaining = add_to_empty_stack_array(
        game_state.inventory.hotbar_slot_blocks,
        HOTBAR_SLOT_COUNT,
        type,
        remaining
    );

    remaining = add_to_empty_stack_array(
        game_state.inventory.inventory_storage_blocks,
        INVENTORY_STORAGE_SLOT_COUNT,
        type,
        remaining
    );

    return remaining;
}

static int get_selected_hotbar_block_type(void) {
    if (game_state.inventory.selected_hotbar_slot < 0 || game_state.inventory.selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        return BLOCK_AIR;
    }

    if (stack_is_empty(&(game_state.inventory.hotbar_slot_blocks[game_state.inventory.selected_hotbar_slot]))) {
        return BLOCK_AIR;
    }

    return game_state.inventory.hotbar_slot_blocks[game_state.inventory.selected_hotbar_slot].type;
}

static void consume_selected_hotbar_block(void) {
    ItemStack *stack;

    if (game_state.inventory.selected_hotbar_slot < 0 || game_state.inventory.selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        return;
    }

    stack = &(game_state.inventory.hotbar_slot_blocks[game_state.inventory.selected_hotbar_slot]);

    if (stack_is_empty(stack)) {
        return;
    }

    if (stack->count > 0) {
        stack->count--;
    }

    normalize_stack(stack);
}

static void select_previous_hotbar_slot(void) {
    game_state.inventory.selected_hotbar_slot--;

    if (game_state.inventory.selected_hotbar_slot < 0) {
        game_state.inventory.selected_hotbar_slot = HOTBAR_SLOT_COUNT - 1;
    }
}

static void select_next_hotbar_slot(void) {
    game_state.inventory.selected_hotbar_slot++;

    if (game_state.inventory.selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        game_state.inventory.selected_hotbar_slot = 0;
    }
}

static ItemStack *get_workbench_cursor_stack_ptr(void) {
    if (game_state.inventory.workbench_cursor_slot < WORKBENCH_CURSOR_OUTPUT) {
        return &(game_state.inventory.workbench_crafting_slots[game_state.inventory.workbench_cursor_slot]);
    }

    if (game_state.inventory.workbench_cursor_slot < WORKBENCH_CURSOR_HOTBAR_START) {
        return &(game_state.inventory.inventory_storage_blocks[game_state.inventory.workbench_cursor_slot - WORKBENCH_CURSOR_STORAGE_START]);
    }

    return &(game_state.inventory.hotbar_slot_blocks[game_state.inventory.workbench_cursor_slot - WORKBENCH_CURSOR_HOTBAR_START]);
}

static void reset_inventory_items(void) {
    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        game_state.inventory.hotbar_slot_blocks[i].type = BLOCK_AIR;
        game_state.inventory.hotbar_slot_blocks[i].count = 0;
    }

    game_state.inventory.hotbar_slot_blocks[0].type = BLOCK_DIRT;
    game_state.inventory.hotbar_slot_blocks[0].count = STACK_MAX_COUNT;

    game_state.inventory.hotbar_slot_blocks[1].type = BLOCK_COBBLESTONE;
    game_state.inventory.hotbar_slot_blocks[1].count = STACK_MAX_COUNT;

    game_state.inventory.hotbar_slot_blocks[2].type = BLOCK_SAND;
    game_state.inventory.hotbar_slot_blocks[2].count = STACK_MAX_COUNT;

    game_state.inventory.hotbar_slot_blocks[3].type = ITEM_WOOD_PICKAXE;
    game_state.inventory.hotbar_slot_blocks[3].count = 1;

    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        game_state.inventory.inventory_storage_blocks[i].type = BLOCK_AIR;
        game_state.inventory.inventory_storage_blocks[i].count = 0;
    }

    game_state.inventory.inventory_storage_blocks[0].type = BLOCK_LOG;
    game_state.inventory.inventory_storage_blocks[0].count = 16;

    for (int i = 0; i < CRAFT_SLOT_COUNT; i++) {
        game_state.inventory.crafting_slots[i].type = BLOCK_AIR;
        game_state.inventory.crafting_slots[i].count = 0;
    }

    for (int i = 0; i < WORKBENCH_CRAFT_SLOT_COUNT; i++) {
        game_state.inventory.workbench_crafting_slots[i].type = BLOCK_AIR;
        game_state.inventory.workbench_crafting_slots[i].count = 0;
    }

    game_state.inventory.inventory_held_stack.type = BLOCK_AIR;
    game_state.inventory.inventory_held_stack.count = 0;
    game_state.inventory.inventory_cursor_slot = INVENTORY_CURSOR_STORAGE_START;
    game_state.inventory.selected_hotbar_slot = 0;
}

static void merge_or_swap_inventory_held_with_slot(ItemStack *slot) {
    if (stack_is_empty(&game_state.inventory.inventory_held_stack)) {
        game_state.inventory.inventory_held_stack = *slot;
        slot->type = BLOCK_AIR;
        slot->count = 0;

        normalize_stack(&game_state.inventory.inventory_held_stack);
        normalize_stack(slot);
        set_system_status(stack_is_empty(&game_state.inventory.inventory_held_stack) ? "HAND EMPTY" : "ITEM HELD", 45);
        return;
    }

    if (stack_is_empty(slot)) {
        *slot = game_state.inventory.inventory_held_stack;
        game_state.inventory.inventory_held_stack.type = BLOCK_AIR;
        game_state.inventory.inventory_held_stack.count = 0;

        normalize_stack(slot);
        normalize_stack(&game_state.inventory.inventory_held_stack);
        set_system_status("ITEM PLACED", 45);
        return;
    }

    if (slot->type == game_state.inventory.inventory_held_stack.type && slot->count < STACK_MAX_COUNT) {
        const int space = STACK_MAX_COUNT - slot->count;
        const int move_count = game_state.inventory.inventory_held_stack.count < space ? game_state.inventory.inventory_held_stack.count : space;

        slot->count = (uint8_t)(slot->count + move_count);
        game_state.inventory.inventory_held_stack.count = (uint8_t)(game_state.inventory.inventory_held_stack.count - move_count);

        normalize_stack(slot);
        normalize_stack(&game_state.inventory.inventory_held_stack);
        set_system_status(stack_is_empty(&game_state.inventory.inventory_held_stack) ? "STACK MERGED" : "STACK PARTIAL", 45);
        return;
    }

    {
        const ItemStack previous = *slot;

        *slot = game_state.inventory.inventory_held_stack;
        game_state.inventory.inventory_held_stack = previous;

        normalize_stack(slot);
        normalize_stack(&game_state.inventory.inventory_held_stack);
        set_system_status("ITEM SWAPPED", 45);
    }
}

static void swap_inventory_held_with_cursor(void) {
    ItemStack *slot;

    if (game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
        take_crafting_output();
        return;
    }

    slot = get_inventory_cursor_stack_ptr();
    merge_or_swap_inventory_held_with_slot(slot);
}

static void quick_move_inventory_slot_to_hotbar(void) {
    ItemStack *slot = get_inventory_cursor_stack_ptr();
    ItemStack *target = &(game_state.inventory.hotbar_slot_blocks[game_state.inventory.selected_hotbar_slot]);
    const ItemStack previous = *target;

    if (game_state.inventory.inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
        return;
    }

    *target = *slot;
    *slot = previous;

    normalize_stack(slot);
    normalize_stack(target);

    set_system_status("MOVED TO HOTBAR", 55);
}

static int can_add_stack_to_hand(const ItemStack *stack) {
    if (stack_is_empty(stack)) {
        return 0;
    }

    if (stack_is_empty(&game_state.inventory.inventory_held_stack)) {
        return 1;
    }

    if (game_state.inventory.inventory_held_stack.type != stack->type) {
        return 0;
    }

    return (game_state.inventory.inventory_held_stack.count + stack->count) <= STACK_MAX_COUNT;
}

static void add_stack_to_hand(const ItemStack *stack) {
    if (stack_is_empty(&game_state.inventory.inventory_held_stack)) {
        game_state.inventory.inventory_held_stack = *stack;
        normalize_stack(&game_state.inventory.inventory_held_stack);
        return;
    }

    if (game_state.inventory.inventory_held_stack.type == stack->type) {
        game_state.inventory.inventory_held_stack.count = (uint8_t)(game_state.inventory.inventory_held_stack.count + stack->count);
        normalize_stack(&game_state.inventory.inventory_held_stack);
    }
}

static void swap_workbench_held_with_cursor(void) {
    ItemStack *slot;

    if (game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
        take_workbench_output();
        return;
    }

    slot = get_workbench_cursor_stack_ptr();
    merge_or_swap_inventory_held_with_slot(slot);
}

static void quick_move_workbench_cursor_to_hotbar(void) {
    ItemStack *slot;

    if (game_state.inventory.workbench_cursor_slot == WORKBENCH_CURSOR_OUTPUT) {
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
