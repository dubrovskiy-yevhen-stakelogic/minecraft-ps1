#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <psxapi.h>
#include <psxgpu.h>
#include <psxpad.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define OT_LENGTH 256
#define BUFFER_LENGTH 65536

#define FIXED_ONE 1024

#define BLOCK_HALF 24
#define BLOCK_SIZE (BLOCK_HALF * 2)

#define VIEW_RADIUS 8
#define VIEW_SIZE ((VIEW_RADIUS * 2) + 1)
#define PADDED_VIEW_SIZE (VIEW_SIZE + 2)
#define GRID_VERTICES_PER_SIDE (VIEW_SIZE + 1)

#define WORLD_HEIGHT 8
#define GRID_Y_LINES (WORLD_HEIGHT + 1)

#define FOCAL_LENGTH 220
#define NEAR_PLANE_Z 24
#define TEXTURED_LOG_MIN_Z 8
#define FAR_PLANE_Z 900
#define FOG_START_Z 260
#define FOG_FULL_Z 780
#define FOG_SKY_R 88
#define FOG_SKY_G 104
#define FOG_SKY_B 124
#define SKY_R 48
#define SKY_G 80
#define SKY_B 130
#define FRUSTUM_MARGIN_X 96
#define FRUSTUM_MARGIN_Y 72

#define MAX_MESH_VERTICES 8192
#define MAX_MESH_FACES 2048

#define MAX_BLOCK_EDITS 256
#define RAYCAST_MAX_DISTANCE 420
#define RAYCAST_STEP 8

#define ANGLE_FULL 4096
#define ANGLE_MASK (ANGLE_FULL - 1)
#define ANGLE_FRAC_BITS 6
#define ANGLE_QUARTER 1024

#define DEFAULT_CAMERA_PITCH (-220)

#define CAMERA_PITCH_MIN (-900)
#define CAMERA_PITCH_MAX 300

#define CAMERA_YAW_SPEED 32
#define CAMERA_PITCH_SPEED 24

#define WALK_MOVE_SPEED 9
#define WALK_STRAFE_SPEED 9

#define FLY_MOVE_SPEED 14
#define FLY_STRAFE_SPEED 12
#define FLY_VERTICAL_SPEED 10

#define PLAYER_EYE_HEIGHT 72
#define PLAYER_COLLISION_RADIUS 36
#define MAX_AUTO_DROP_BLOCKS 1

#define HOTBAR_SLOT_COUNT 9
#define HOTBAR_SLOT_SIZE 20
#define HOTBAR_SLOT_GAP 2
#define HOTBAR_TOTAL_W ((HOTBAR_SLOT_COUNT * HOTBAR_SLOT_SIZE) + ((HOTBAR_SLOT_COUNT - 1) * HOTBAR_SLOT_GAP))
#define HOTBAR_START_X ((SCREEN_W - HOTBAR_TOTAL_W) / 2)
#define HOTBAR_Y 212
#define HEART_COUNT 10
#define HEART_START_X 68
#define HEART_Y 194

#define INVENTORY_STORAGE_COLS 9
#define INVENTORY_STORAGE_ROWS 3
#define INVENTORY_STORAGE_SLOT_COUNT (INVENTORY_STORAGE_COLS * INVENTORY_STORAGE_ROWS)
#define INVENTORY_TOTAL_SLOTS (INVENTORY_STORAGE_SLOT_COUNT + HOTBAR_SLOT_COUNT)
#define INVENTORY_SLOT_SIZE 18
#define INVENTORY_SLOT_GAP 2
#define INVENTORY_STORAGE_START_X 70
#define INVENTORY_STORAGE_START_Y 92
#define INVENTORY_HOTBAR_START_X INVENTORY_STORAGE_START_X
#define INVENTORY_HOTBAR_Y 158
#define INVENTORY_CURSOR_STORAGE_START 0
#define INVENTORY_CURSOR_HOTBAR_START INVENTORY_STORAGE_SLOT_COUNT
#define CRAFT_SLOT_COUNT 4
#define INVENTORY_CURSOR_CRAFT_START (INVENTORY_STORAGE_SLOT_COUNT + HOTBAR_SLOT_COUNT)
#define INVENTORY_CURSOR_CRAFT_OUTPUT (INVENTORY_CURSOR_CRAFT_START + CRAFT_SLOT_COUNT)

#define WORKBENCH_CRAFT_COLS 3
#define WORKBENCH_CRAFT_ROWS 3
#define WORKBENCH_CRAFT_SLOT_COUNT (WORKBENCH_CRAFT_COLS * WORKBENCH_CRAFT_ROWS)
#define WORKBENCH_CURSOR_CRAFT_START 0
#define WORKBENCH_CURSOR_OUTPUT WORKBENCH_CRAFT_SLOT_COUNT
#define WORKBENCH_CURSOR_STORAGE_START (WORKBENCH_CURSOR_OUTPUT + 1)
#define WORKBENCH_CURSOR_HOTBAR_START (WORKBENCH_CURSOR_STORAGE_START + INVENTORY_STORAGE_SLOT_COUNT)

#define STACK_MAX_COUNT 64

#define BLOCK_BREAK_MIN_FRAMES 45
#define BLOCK_BREAK_DIRT_FRAMES 90
#define BLOCK_BREAK_GRASS_FRAMES 100
#define BLOCK_BREAK_SAND_FRAMES 65
#define BLOCK_BREAK_STONE_FRAMES 170

#define MAX_DROPPED_ITEMS 32
#define PICKUP_DISTANCE_XZ 42
#define PICKUP_DISTANCE_Y 90

#define PAD_BUFFER_SIZE 34

#define FILE_MODE_READ 1
#define FILE_MODE_WRITE 2
#define FILE_MODE_CREATE 0x0200

#define SAVE_FILE_PATH "bu00:BASLUS-00000MINE"
#define SAVE_BLOCK_SIZE 8192
#define SAVE_BLOCK_COUNT 1
#define SAVE_DATA_OFFSET 128
#define SAVE_MAGIC 0x31434d50
#define SAVE_VERSION 1

#define MENU_OPTION_NEW_GAME 0
#define MENU_OPTION_LOAD_GAME 1
#define MENU_OPTION_COUNT 2

#define PAUSE_OPTION_RESUME 0
#define PAUSE_OPTION_INVENTORY 1
#define PAUSE_OPTION_TOGGLE_FLY 2
#define PAUSE_OPTION_TOGGLE_HUD 3
#define PAUSE_OPTION_TOGGLE_FOG 4
#define PAUSE_OPTION_TOGGLE_AUTOJUMP 5
#define PAUSE_OPTION_SAVE_GAME 6
#define PAUSE_OPTION_LOAD_GAME 7
#define PAUSE_OPTION_RETURN_MENU 8
#define PAUSE_OPTION_COUNT 9

typedef struct {
    DISPENV disp_env;
    DRAWENV draw_env;
    uint32_t ot[OT_LENGTH];
    uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct {
    RenderBuffer buffers[2];
    uint8_t *next_packet;
    int active_buffer;
} RenderContext;

typedef struct {
    int x;
    int y;
    int z;
} Vec3i;

typedef struct {
    int x;
    int y;
    int z;
} ProjectedVertex;

typedef struct {
    uint16_t v[4];

    uint8_t r;
    uint8_t g;
    uint8_t b;

    uint8_t block_type;
    uint8_t face_type;
} MeshFace;

typedef struct {
    int x;
    int y;
    int z;
    uint8_t type;
} BlockEdit;

typedef struct {
    uint8_t type;
    uint8_t count;
} ItemStack;

typedef struct {
    int active;
    int x;
    int y;
    int z;
    uint8_t type;
    uint8_t count;
    int bob_frame;
} DroppedItem;

typedef struct {
    int found;
    int hit_x;
    int hit_y;
    int hit_z;
    int hit_face;
    int place_x;
    int place_y;
    int place_z;
} RaycastHit;

typedef struct {
    uint32_t magic;
    uint32_t version;

    int camera_pos_x;
    int camera_pos_y;
    int camera_pos_z;
    int camera_yaw;
    int camera_pitch;
    int fly_mode_enabled;

    int block_edit_count;
    BlockEdit block_edits[MAX_BLOCK_EDITS];

    uint32_t checksum;
} SaveData;

enum {
    APP_STATE_MENU = 0,
    APP_STATE_PLAY = 1,
    APP_STATE_PAUSE = 2,
    APP_STATE_INVENTORY = 3,
    APP_STATE_WORKBENCH = 4
};

enum {
    BLOCK_AIR = 0,
    BLOCK_DIRT = 1,
    BLOCK_GRASS = 2,
    BLOCK_STONE = 3,
    BLOCK_SAND = 4,
    BLOCK_LOG = 5,
    BLOCK_PLANKS = 6,
    BLOCK_WORKBENCH = 7,

    ITEM_WOOD_PICKAXE = 32
};

enum {
    FACE_NEG_Z = 0,
    FACE_POS_Z = 1,
    FACE_NEG_X = 2,
    FACE_POS_X = 3,
    FACE_NEG_Y = 4,
    FACE_POS_Y = 5
};


static RenderContext ctx;

static Vec3i mesh_vertices[MAX_MESH_VERTICES];
static Vec3i camera_vertices[MAX_MESH_VERTICES];

static MeshFace mesh_faces[MAX_MESH_FACES];

static int mesh_vertex_count = 0;
static int mesh_face_count = 0;

static int mesh_center_tile_x = 999999;
static int mesh_center_tile_z = 999999;
static int mesh_dirty = 1;

static int vertex_lookup[GRID_Y_LINES][GRID_VERTICES_PER_SIDE][GRID_VERTICES_PER_SIDE];
static uint8_t local_blocks[WORLD_HEIGHT][PADDED_VIEW_SIZE][PADDED_VIEW_SIZE];

static BlockEdit block_edits[MAX_BLOCK_EDITS];
static int block_edit_count = 0;

static uint8_t pad_buffers[2][PAD_BUFFER_SIZE];

static int camera_pos_x = 0;
static int camera_pos_y = 120;
static int camera_pos_z = 0;

static int camera_yaw = 0;
static int camera_pitch = DEFAULT_CAMERA_PITCH;

/*
 * 0 = walk on voxel world
 * 1 = free fly/debug camera
 */
static int fly_mode_enabled = 0;

static int app_state = APP_STATE_MENU;
static int menu_selected_option = MENU_OPTION_NEW_GAME;
static int pause_selected_option = PAUSE_OPTION_RESUME;

/*
 * hud_visible controls only the small debug/help panel.
 * The Minecraft-style hotbar and hearts are part of the game UI and stay visible.
 */
static int hud_visible = 0;
static int fog_enabled = 0;
static int autojump_enabled = 1;
static int selected_hotbar_slot = 0;
static int player_health_hearts = HEART_COUNT;
static int inventory_cursor_slot = INVENTORY_CURSOR_STORAGE_START;
static int workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START;
static ItemStack inventory_held_stack = { BLOCK_AIR, 0 };

static ItemStack hotbar_slot_blocks[HOTBAR_SLOT_COUNT] = {
    { BLOCK_DIRT, STACK_MAX_COUNT },
    { BLOCK_STONE, STACK_MAX_COUNT },
    { BLOCK_SAND, STACK_MAX_COUNT },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 }
};

static ItemStack crafting_slots[CRAFT_SLOT_COUNT] = {
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 }
};

static ItemStack workbench_crafting_slots[WORKBENCH_CRAFT_SLOT_COUNT] = {
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },

    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },

    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 }
};

static ItemStack inventory_storage_blocks[INVENTORY_STORAGE_SLOT_COUNT] = {
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },

    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },

    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 },
    { BLOCK_AIR, 0 }
};

static DroppedItem dropped_items[MAX_DROPPED_ITEMS];

static int breaking_active = 0;
static int breaking_block_x = 0;
static int breaking_block_y = 0;
static int breaking_block_z = 0;
static int breaking_block_face = FACE_POS_Y;
static int breaking_block_type = BLOCK_AIR;
static int breaking_progress = 0;
static int breaking_required_frames = BLOCK_BREAK_MIN_FRAMES;

static uint16_t pad_previous_buttons = 0;

static const char *system_status_text = "";
static int system_status_timer = 0;

static uint8_t save_buffer[SAVE_BLOCK_SIZE];

/* SaveData must fit after memory card header inside 1 block. */


static void snap_camera_to_ground(void);
static int save_game_to_memory_card(void);
static int load_game_from_memory_card(void);
static void reset_world_edits(void);
static int add_items_to_inventory(uint8_t type, int amount);
static void spawn_dropped_item(
    int block_type,
    int block_x,
    int block_y,
    int block_z
);
static void take_crafting_output(void);
static void take_workbench_output(void);
static void set_system_status(const char *text, int frames);
static ItemStack *get_inventory_cursor_stack_ptr(void);
static int quick_craft_output_to_inventory(void);
static int quick_craft_workbench_output_to_inventory(void);
static ItemStack get_crafting_output_stack(void);
static ItemStack get_workbench_output_stack(void);
static void merge_or_swap_inventory_held_with_slot(ItemStack *slot);
static void swap_inventory_held_with_cursor(void);
static void quick_move_inventory_slot_to_hotbar(void);
static void swap_workbench_held_with_cursor(void);
static void quick_move_workbench_cursor_to_hotbar(void);

/*
 * Stage 1 architecture split.
 *
 * These engine files are included directly for now, so the current CMake build
 * that compiles only main.c keeps working. After the project structure settles,
 * they can be compiled as normal separate translation units.
 */
#include "engine/fixed_math.h"
#include "engine/ps1_input.h"
#include "engine/ps1_render.h"
#include "engine/ps1_texture.h"
#include "assets/terrain_atlas.h"

#include "game/world.h"
#include "game/player.h"
#include "game/raycast.h"
#include "game/inventory.h"
#include "game/crafting.h"
#include "game/dropped_item.h"

#include "ui/menu.h"
#include "ui/hud.h"
#include "ui/inventory_ui.h"
#include "ui/workbench_ui.h"

#include "engine/fixed_math.c"
#include "engine/ps1_input.c"
#include "engine/ps1_render.c"
#include "engine/ps1_texture.c"
#include "assets/terrain_atlas.c"

#include "game/world.c"
#include "game/raycast.c"
#include "game/inventory.c"
#include "game/crafting.c"
#include "game/dropped_item.c"
#include "game/player.c"

/*
 * 64-step sine table.
 * Scale: 1024 = 1.0
 *
 * We interpolate between values, so angle resolution is 4096 steps.
 */





/*
 * C integer division truncates toward zero.
 * We need floor division so negative world coordinates map to correct tiles.
 */



/*
 * World Y mapping:
 * block_y = 0 occupies [-48, 0]
 * block_y = 1 occupies [0, 48]
 * ...
 */























static void clear_bytes(uint8_t *data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] = 0;
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, int size) {
    for (int i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static uint32_t checksum_bytes(const uint8_t *data, int size) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static void set_system_status(const char *text, int frames) {
    system_status_text = text;
    system_status_timer = frames;
}

static void tick_system_status(void) {
    if (system_status_timer > 0) {
        system_status_timer--;
    }
}

static void update_clear_color_for_game(void) {
    if (fog_enabled) {
        setRGB0(&(ctx.buffers[0].draw_env), FOG_SKY_R, FOG_SKY_G, FOG_SKY_B);
        setRGB0(&(ctx.buffers[1].draw_env), FOG_SKY_R, FOG_SKY_G, FOG_SKY_B);
    } else {
        setRGB0(&(ctx.buffers[0].draw_env), SKY_R, SKY_G, SKY_B);
        setRGB0(&(ctx.buffers[1].draw_env), SKY_R, SKY_G, SKY_B);
    }
}

static void init_memory_card(void) {
    /*
     * BIOS memory card driver init.
     *
     * The save path uses Memory Card slot 1:
     *     bu00:BASLUS-00000MINE
     */
    InitCARD(1);
    StartCARD();
    _bu_init();
}

static void reset_world_edits(void) {
    block_edit_count = 0;
    mesh_center_tile_x = 999999;
    mesh_center_tile_z = 999999;
    mesh_dirty = 1;
}

static void write_save_icon_header(uint8_t *buffer) {
    /*
     * Minimal PS1 memory card header.
     *
     * The actual game data starts at SAVE_DATA_OFFSET. The header is mostly
     * for making the file look sane in the memory card manager. The save/load
     * code below only depends on our SaveData payload.
     */
    buffer[0] = 'S';
    buffer[1] = 'C';
    buffer[2] = 0x11;
    buffer[3] = 0x00;

    {
        const char title[] = "MINECRAFT PS1 SAVE";

        for (int i = 0; title[i] != 0 && i < 32; i++) {
            buffer[4 + i] = (uint8_t)title[i];
        }
    }
}

static void fill_save_data(SaveData *save) {
    save->magic = SAVE_MAGIC;
    save->version = SAVE_VERSION;

    save->camera_pos_x = camera_pos_x;
    save->camera_pos_y = camera_pos_y;
    save->camera_pos_z = camera_pos_z;
    save->camera_yaw = camera_yaw;
    save->camera_pitch = camera_pitch;
    save->fly_mode_enabled = fly_mode_enabled;

    save->block_edit_count = block_edit_count;

    for (int i = 0; i < MAX_BLOCK_EDITS; i++) {
        if (i < block_edit_count) {
            save->block_edits[i] = block_edits[i];
        } else {
            save->block_edits[i].x = 0;
            save->block_edits[i].y = 0;
            save->block_edits[i].z = 0;
            save->block_edits[i].type = BLOCK_AIR;
        }
    }

    save->checksum = 0;
    save->checksum = checksum_bytes(
        (const uint8_t *)save,
        (int)sizeof(SaveData) - (int)sizeof(uint32_t)
    );
}

static int validate_save_data(const SaveData *save) {
    uint32_t expected_checksum;

    if (save->magic != SAVE_MAGIC) {
        return 0;
    }

    if (save->version != SAVE_VERSION) {
        return 0;
    }

    if (save->block_edit_count < 0 || save->block_edit_count > MAX_BLOCK_EDITS) {
        return 0;
    }

    expected_checksum = checksum_bytes(
        (const uint8_t *)save,
        (int)sizeof(SaveData) - (int)sizeof(uint32_t)
    );

    return expected_checksum == save->checksum;
}

static void apply_save_data(const SaveData *save) {
    camera_pos_x = save->camera_pos_x;
    camera_pos_y = save->camera_pos_y;
    camera_pos_z = save->camera_pos_z;
    camera_yaw = save->camera_yaw & ANGLE_MASK;
    camera_pitch = clamp_int(
        save->camera_pitch,
        CAMERA_PITCH_MIN,
        CAMERA_PITCH_MAX
    );
    fly_mode_enabled = save->fly_mode_enabled ? 1 : 0;

    block_edit_count = save->block_edit_count;

    for (int i = 0; i < MAX_BLOCK_EDITS; i++) {
        if (i < block_edit_count) {
            block_edits[i] = save->block_edits[i];

            if (block_edits[i].y < 0) {
                block_edits[i].y = 0;
            }

            if (block_edits[i].y >= WORLD_HEIGHT) {
                block_edits[i].y = WORLD_HEIGHT - 1;
            }

            if (
                block_edits[i].type != BLOCK_AIR &&
                block_edits[i].type != BLOCK_DIRT &&
                block_edits[i].type != BLOCK_GRASS &&
                block_edits[i].type != BLOCK_STONE &&
                block_edits[i].type != BLOCK_SAND &&
                block_edits[i].type != BLOCK_LOG &&
                block_edits[i].type != BLOCK_PLANKS &&
                block_edits[i].type != BLOCK_WORKBENCH
            ) {
                block_edits[i].type = BLOCK_DIRT;
            }
        } else {
            block_edits[i].x = 0;
            block_edits[i].y = 0;
            block_edits[i].z = 0;
            block_edits[i].type = BLOCK_AIR;
        }
    }

    mesh_center_tile_x = 999999;
    mesh_center_tile_z = 999999;
    mesh_dirty = 1;

    if (!fly_mode_enabled) {
        snap_camera_to_ground();
    }
}

static int create_save_file_on_memory_card(void) {
    int fd;

    /*
     * Important PS1 memory card rule:
     *
     * Creation is a separate operation. The file size is passed in memory-card
     * blocks through the upper 16 bits of the open() mode.
     *
     * Example from old Psy-Q docs:
     *     open("bu00:L01", O_CREAT | (2 << 16))
     *
     * After creation, close the file and reopen it for writing.
     */
    fd = open(
        SAVE_FILE_PATH,
        FILE_MODE_CREATE | (SAVE_BLOCK_COUNT << 16)
    );

    if (fd < 0) {
        return 0;
    }

    close(fd);
    return 1;
}

static int save_game_to_memory_card(void) {
    SaveData save;
    int fd;
    int written;

    clear_bytes(save_buffer, SAVE_BLOCK_SIZE);
    write_save_icon_header(save_buffer);

    fill_save_data(&save);

    copy_bytes(
        &(save_buffer[SAVE_DATA_OFFSET]),
        (const uint8_t *)&save,
        (int)sizeof(SaveData)
    );

    /*
     * Simple one-slot save.
     * Delete old save first, then create a fresh 1-block memory card file.
     */
    erase(SAVE_FILE_PATH);

    if (!create_save_file_on_memory_card()) {
        return 0;
    }

    fd = open(SAVE_FILE_PATH, FILE_MODE_WRITE);

    if (fd < 0) {
        return 0;
    }

    written = write(fd, save_buffer, SAVE_BLOCK_SIZE);
    close(fd);

    return written == SAVE_BLOCK_SIZE;
}

static int load_game_from_memory_card(void) {
    SaveData save;
    int fd;
    int bytes_read;

    clear_bytes(save_buffer, SAVE_BLOCK_SIZE);

    fd = open(SAVE_FILE_PATH, FILE_MODE_READ);

    if (fd < 0) {
        return 0;
    }

    bytes_read = read(fd, save_buffer, SAVE_BLOCK_SIZE);
    close(fd);

    if (bytes_read < (SAVE_DATA_OFFSET + (int)sizeof(SaveData))) {
        return 0;
    }

    copy_bytes(
        (uint8_t *)&save,
        &(save_buffer[SAVE_DATA_OFFSET]),
        (int)sizeof(SaveData)
    );

    if (!validate_save_data(&save)) {
        return 0;
    }

    apply_save_data(&save);

    return 1;
}



















































































static ItemStack *get_inventory_cursor_stack_ptr(void) {
    if (inventory_cursor_slot < INVENTORY_STORAGE_SLOT_COUNT) {
        return &(inventory_storage_blocks[inventory_cursor_slot]);
    }

    if (inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_START) {
        return &(hotbar_slot_blocks[inventory_cursor_slot - INVENTORY_STORAGE_SLOT_COUNT]);
    }

    if (inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_OUTPUT) {
        return &(crafting_slots[inventory_cursor_slot - INVENTORY_CURSOR_CRAFT_START]);
    }

    return &(crafting_slots[0]);
}

































static void start_new_game(void) {
    reset_world_edits();
    reset_camera();
    reset_inventory_items();
    reset_dropped_items();
    reset_block_breaking();
    pause_selected_option = PAUSE_OPTION_RESUME;

    app_state = APP_STATE_PLAY;
    set_system_status("NEW GAME", 90);
}

static void start_loaded_game(void) {
    if (load_game_from_memory_card()) {
        reset_inventory_items();
        reset_dropped_items();
        reset_block_breaking();
        pause_selected_option = PAUSE_OPTION_RESUME;
        app_state = APP_STATE_PLAY;
        set_system_status("LOAD OK", 90);
    } else {
        app_state = APP_STATE_MENU;
        set_system_status("LOAD FAILED", 120);
    }
}



/*
 * Stage 3 UI split.
 *
 * UI .c files are included directly for now, keeping the existing CMake setup.
 * Later they can become normal separate translation units once GameState and
 * shared public headers are introduced.
 */
#include "ui/menu.c"
#include "ui/hud.c"
#include "ui/inventory_ui.c"
#include "ui/workbench_ui.c"

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    ResetGraph(0);
    FntLoad(960, 0);

    /*
     * Sky-ish background.
     */
    setup_context(&ctx, SCREEN_W, SCREEN_H, SKY_R, SKY_G, SKY_B);
    terrain_atlas_upload();
    init_input();
    init_memory_card();

    reset_camera();
    app_state = APP_STATE_MENU;
    set_system_status("", 0);

    for (;;) {
        tick_system_status();
        update_clear_color_for_game();

        if (app_state == APP_STATE_MENU) {
            update_menu_input();
            draw_menu(&ctx);
            flip_buffers(&ctx);
            continue;
        }

        if (app_state == APP_STATE_PAUSE) {
            update_pause_input();
            rebuild_mesh_if_needed();
            transform_all_vertices();
            draw_mesh(&ctx);
            draw_dropped_items(&ctx);
            draw_pause_menu(&ctx);
            flip_buffers(&ctx);
            continue;
        }

        if (app_state == APP_STATE_INVENTORY) {
            update_inventory_input();
            draw_inventory_screen(&ctx);
            flip_buffers(&ctx);
            continue;
        }

        if (app_state == APP_STATE_WORKBENCH) {
            update_workbench_input();
            draw_workbench_screen(&ctx);
            flip_buffers(&ctx);
            continue;
        }

        update_input();

        rebuild_mesh_if_needed();
        transform_all_vertices();
        draw_mesh(&ctx);
        draw_dropped_items(&ctx);
        draw_breaking_overlay(&ctx);
        draw_crosshair(&ctx);
        draw_held_item_in_hand(&ctx);
        draw_game_hud(&ctx);

        flip_buffers(&ctx);
    }

    return 0;
}
