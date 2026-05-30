#ifndef MCPSX_GAME_GAME_STATE_H
#define MCPSX_GAME_GAME_STATE_H

/*
 * Central game state container.
 *
 * Stage 5 is intentionally transitional:
 * - the data now lives in GameState;
 * - existing systems still access old short names through macros in main.c;
 * - next steps can gradually replace those macros with explicit GameState usage.
 */

typedef struct {
    RenderContext ctx;
} GameRenderState;

typedef struct {
    Vec3i mesh_vertices[MAX_MESH_VERTICES];
    Vec3i camera_vertices[MAX_MESH_VERTICES];
    MeshFace mesh_faces[MAX_MESH_FACES];

    int mesh_vertex_count;
    int mesh_face_count;

    int mesh_center_tile_x;
    int mesh_center_tile_z;
    int mesh_dirty;

    int vertex_lookup[GRID_Y_LINES][GRID_VERTICES_PER_SIDE][GRID_VERTICES_PER_SIDE];
    uint8_t local_blocks[WORLD_HEIGHT][PADDED_VIEW_SIZE][PADDED_VIEW_SIZE];

    BlockEdit block_edits[MAX_BLOCK_EDITS];
    int block_edit_count;
} GameWorldState;

typedef struct {
    uint8_t pad_buffers[2][PAD_BUFFER_SIZE];
    uint16_t pad_previous_buttons;
} GameInputState;

typedef struct {
    int camera_pos_x;
    int camera_pos_y;
    int camera_pos_z;

    int camera_yaw;
    int camera_pitch;

    int fly_mode_enabled;
    int autojump_enabled;

    int player_health_hearts;
} GamePlayerState;

typedef struct {
    int app_state;
    int menu_selected_option;
    int pause_selected_option;

    int hud_visible;
    int fog_enabled;

    const char *system_status_text;
    int system_status_timer;
} GameAppState;

typedef struct {
    int selected_hotbar_slot;
    int inventory_cursor_slot;
    int workbench_cursor_slot;

    ItemStack inventory_held_stack;

    ItemStack hotbar_slot_blocks[HOTBAR_SLOT_COUNT];
    ItemStack crafting_slots[CRAFT_SLOT_COUNT];
    ItemStack workbench_crafting_slots[WORKBENCH_CRAFT_SLOT_COUNT];
    ItemStack inventory_storage_blocks[INVENTORY_STORAGE_SLOT_COUNT];
} GameInventoryState;

typedef struct {
    DroppedItem dropped_items[MAX_DROPPED_ITEMS];
} GameDroppedItemState;

typedef struct {
    int breaking_active;
    int breaking_block_x;
    int breaking_block_y;
    int breaking_block_z;
    int breaking_block_face;
    int breaking_block_type;
    int breaking_progress;
    int breaking_required_frames;
} GameBreakingState;

typedef struct {
    uint8_t save_buffer[SAVE_BLOCK_SIZE];
} GameSaveRuntimeState;

typedef struct {
    GameRenderState render;
    GameWorldState world;
    GameInputState input;
    GamePlayerState player;
    GameAppState app;
    GameInventoryState inventory;
    GameDroppedItemState dropped;
    GameBreakingState breaking;
    GameSaveRuntimeState save;
} GameState;

#endif
