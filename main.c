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

#define TEXTURE_ATLAS_X 640
#define TEXTURE_ATLAS_Y 0
#define TEXTURE_ATLAS_W 32
#define TEXTURE_ATLAS_H 16
#define LOG_SIDE_U 0
#define LOG_TOP_U 16
#define LOG_TILE_V 0
#define LOG_TILE_SIZE 16

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

/*
 * Real PS1 texture atlas.
 * Extracted from uploaded terrain sprite:
 * row 2, tile 5 = log side
 * row 2, tile 6 = log top/bottom
 *
 * Atlas layout, 16bpp:
 * u 00..15 = log side
 * u 16..31 = log top
 */
static const uint16_t terrain_log_atlas_16bpp[TEXTURE_ATLAS_H][TEXTURE_ATLAS_W] = {
    { 0x10e8, 0x194d, 0x194d, 0x25f3, 0x0cc7, 0x196d, 0x21d2, 0x10c8, 0x194c, 0x194d, 0x194c, 0x1d8f, 0x196d, 0x1d8f, 0x194d, 0x25f3, 0x194d, 0x194d, 0x0ca7, 0x194d, 0x194d, 0x194c, 0x0ca7, 0x0ca7, 0x0ca7, 0x194d, 0x194d, 0x0ca7, 0x1d6d, 0x194d, 0x194d, 0x196d },
    { 0x0cc7, 0x154c, 0x1d6d, 0x0ca7, 0x0cc7, 0x25f3, 0x10c8, 0x194c, 0x25f3, 0x0cc7, 0x194d, 0x25f3, 0x10e9, 0x1d8f, 0x10e9, 0x21d2, 0x194d, 0x2e56, 0x2a36, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2a35, 0x2a36, 0x2e56, 0x2e56, 0x2e57, 0x3257, 0x2e56, 0x152c },
    { 0x0ca7, 0x152b, 0x152c, 0x10c8, 0x194d, 0x25f3, 0x0cc7, 0x194c, 0x25f3, 0x0ca7, 0x194c, 0x1d8f, 0x10e9, 0x1d8f, 0x10e9, 0x25f3, 0x0ca7, 0x2e56, 0x2613, 0x2613, 0x2613, 0x21f2, 0x2613, 0x2613, 0x2a34, 0x2613, 0x2613, 0x2613, 0x2613, 0x2a14, 0x2e56, 0x194d },
    { 0x196d, 0x10e8, 0x152c, 0x10c8, 0x1d8f, 0x1d8f, 0x0cc7, 0x194d, 0x25f3, 0x0ca7, 0x194d, 0x25f3, 0x0cc7, 0x25f3, 0x10c8, 0x1d8f, 0x194d, 0x2e56, 0x2a34, 0x2e56, 0x3277, 0x3277, 0x3277, 0x3277, 0x3277, 0x2e57, 0x3277, 0x2e57, 0x3277, 0x2613, 0x2e56, 0x0ca7 },
    { 0x194d, 0x10c8, 0x194d, 0x194c, 0x1d8f, 0x1d8f, 0x194d, 0x194c, 0x1d6d, 0x10c8, 0x194d, 0x25f3, 0x0cc7, 0x25f3, 0x0ca7, 0x1d8f, 0x10e9, 0x2e36, 0x2613, 0x3277, 0x2e56, 0x2e57, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2e57, 0x3277, 0x2613, 0x2e56, 0x194d },
    { 0x194d, 0x152b, 0x194d, 0x194c, 0x25f3, 0x194d, 0x194c, 0x194d, 0x194d, 0x0cc7, 0x194d, 0x21b1, 0x0cc7, 0x1d8f, 0x10c8, 0x194c, 0x0ca7, 0x2e56, 0x2613, 0x3277, 0x2e56, 0x2613, 0x2a34, 0x2a34, 0x2613, 0x2613, 0x2613, 0x2e56, 0x2e57, 0x2613, 0x2e56, 0x194d },
    { 0x194d, 0x1d8f, 0x10e9, 0x194d, 0x25d2, 0x152c, 0x194d, 0x0ca7, 0x21d2, 0x0ca7, 0x1d8f, 0x150b, 0x08a6, 0x1d8f, 0x0cc7, 0x1d6d, 0x0ca7, 0x2e56, 0x2613, 0x3257, 0x2e56, 0x2613, 0x3257, 0x3257, 0x3277, 0x3277, 0x2613, 0x2e56, 0x2e57, 0x2613, 0x2e56, 0x0ca7 },
    { 0x194d, 0x25f3, 0x0cc7, 0x194d, 0x25f3, 0x0cc7, 0x25f3, 0x10c8, 0x25f3, 0x0ca7, 0x1d8f, 0x10e9, 0x194c, 0x1d8f, 0x0cc7, 0x1d6d, 0x0ca7, 0x2e56, 0x2613, 0x3699, 0x2e56, 0x2613, 0x3277, 0x2e56, 0x2e56, 0x3257, 0x2614, 0x2a35, 0x3277, 0x2613, 0x2e56, 0x1d6e },
    { 0x194d, 0x25f3, 0x0cc7, 0x1d6e, 0x21d2, 0x10c8, 0x194d, 0x10c8, 0x194d, 0x10c8, 0x21d2, 0x0ca7, 0x194d, 0x194d, 0x0cc7, 0x1d8f, 0x194d, 0x2e56, 0x2e55, 0x3277, 0x2e56, 0x2613, 0x3257, 0x2e56, 0x2e56, 0x3277, 0x2613, 0x2e56, 0x3277, 0x2613, 0x2e57, 0x194d },
    { 0x194d, 0x25f3, 0x10e8, 0x194c, 0x196d, 0x21d2, 0x10c8, 0x194d, 0x194d, 0x0ca7, 0x21d2, 0x10c8, 0x194d, 0x21d2, 0x10c8, 0x1d8f, 0x194d, 0x2e56, 0x2613, 0x3277, 0x2e36, 0x25f3, 0x3277, 0x3277, 0x3277, 0x3277, 0x2613, 0x2e56, 0x3277, 0x2613, 0x2e56, 0x194d },
    { 0x10e8, 0x1d8f, 0x10c8, 0x194d, 0x194c, 0x21d2, 0x0ca7, 0x152c, 0x0cc7, 0x152b, 0x29f3, 0x10c8, 0x194d, 0x25f3, 0x10c8, 0x21d2, 0x194c, 0x2a36, 0x2613, 0x2e56, 0x2e56, 0x2613, 0x2613, 0x2613, 0x2613, 0x2613, 0x2613, 0x2e57, 0x3277, 0x2613, 0x2e56, 0x194d },
    { 0x0cc7, 0x1d8f, 0x0cc7, 0x194c, 0x1d6d, 0x25f3, 0x10c8, 0x25f3, 0x0ca7, 0x152b, 0x25f3, 0x0cc7, 0x194d, 0x25d2, 0x0ca7, 0x25f3, 0x0ca7, 0x2e36, 0x2613, 0x3277, 0x2e36, 0x2e56, 0x2e56, 0x2e36, 0x2e56, 0x2e56, 0x2e56, 0x2e57, 0x3277, 0x2a34, 0x2e56, 0x154c },
    { 0x0ca7, 0x25f3, 0x0ca7, 0x194c, 0x194d, 0x25f3, 0x0ca7, 0x194d, 0x0cc7, 0x154c, 0x25d2, 0x0ca7, 0x194d, 0x152c, 0x10c8, 0x25f3, 0x0ca7, 0x2e56, 0x2613, 0x3277, 0x3277, 0x2e56, 0x3257, 0x3277, 0x3277, 0x3277, 0x3277, 0x3277, 0x3257, 0x2613, 0x2e56, 0x0ca7 },
    { 0x10c8, 0x196d, 0x152b, 0x194d, 0x194d, 0x1d8f, 0x10e9, 0x1d6d, 0x196d, 0x152c, 0x194c, 0x0cc7, 0x194d, 0x21d2, 0x25f3, 0x0ca7, 0x0ca7, 0x2e56, 0x21f3, 0x2613, 0x2a34, 0x2613, 0x2613, 0x2613, 0x25f3, 0x2614, 0x2613, 0x2a34, 0x2a34, 0x2a34, 0x2e36, 0x0ca7 },
    { 0x0ca7, 0x194c, 0x25d2, 0x10c8, 0x194d, 0x1d8f, 0x10e9, 0x0cc7, 0x21d2, 0x194d, 0x25f3, 0x0ca7, 0x154c, 0x1d8f, 0x0cc7, 0x194c, 0x10e9, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x2a35, 0x2a35, 0x2e56, 0x2e56, 0x2a15, 0x2e56, 0x2e56, 0x2e56, 0x2e56, 0x194d },
    { 0x152b, 0x194d, 0x1d8f, 0x0cc7, 0x152c, 0x194d, 0x194d, 0x10c8, 0x194d, 0x194d, 0x194c, 0x08a6, 0x194c, 0x194d, 0x154c, 0x1d8f, 0x194d, 0x1d6d, 0x0ca7, 0x194d, 0x194d, 0x194d, 0x0ca7, 0x194d, 0x194d, 0x194d, 0x194d, 0x194d, 0x0ca7, 0x0ca7, 0x0ca7, 0x194d },
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

/*
 * 64-step sine table.
 * Scale: 1024 = 1.0
 *
 * We interpolate between values, so angle resolution is 4096 steps.
 */
static const int16_t sin_table[64] = {
    0, 100, 200, 297, 391, 483, 569, 650,
    724, 792, 851, 903, 946, 980, 1004, 1019,
    1024, 1019, 1004, 980, 946, 903, 851, 792,
    724, 650, 569, 483, 391, 297, 200, 100,
    0, -100, -200, -297, -391, -483, -569, -650,
    -724, -792, -851, -903, -946, -980, -1004, -1019,
    -1024, -1019, -1004, -980, -946, -903, -851, -792,
    -724, -650, -569, -483, -391, -297, -200, -100
};

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static int iabs(int value) {
    return value < 0 ? -value : value;
}

static int isin(int angle) {
    angle &= ANGLE_MASK;

    const int index = (angle >> ANGLE_FRAC_BITS) & 63;
    const int frac = angle & ((1 << ANGLE_FRAC_BITS) - 1);

    const int a = sin_table[index];
    const int b = sin_table[(index + 1) & 63];

    return a + (((b - a) * frac) >> ANGLE_FRAC_BITS);
}

static int icos(int angle) {
    return isin(angle + ANGLE_QUARTER);
}

/*
 * C integer division truncates toward zero.
 * We need floor division so negative world coordinates map to correct tiles.
 */
static int floor_div(int value, int divisor) {
    if (value >= 0) {
        return value / divisor;
    }

    return -(((-value) + divisor - 1) / divisor);
}

static int world_to_tile(int value) {
    return floor_div(value + BLOCK_HALF, BLOCK_SIZE);
}

static int tile_to_world_center(int tile) {
    return tile * BLOCK_SIZE;
}

/*
 * World Y mapping:
 * block_y = 0 occupies [-48, 0]
 * block_y = 1 occupies [0, 48]
 * ...
 */
static int world_to_block_y(int value) {
    return floor_div(value + BLOCK_SIZE, BLOCK_SIZE);
}

static int block_y_to_world_min(int block_y) {
    return -BLOCK_SIZE + (block_y * BLOCK_SIZE);
}

static int block_y_to_world_top(int block_y) {
    return block_y_to_world_min(block_y) + BLOCK_SIZE;
}

static int get_generated_block_type(int x, int y, int z) {
    (void)x;
    (void)z;

    if (y < 0 || y >= WORLD_HEIGHT) {
        return BLOCK_AIR;
    }

    if (y == 0) {
        return BLOCK_DIRT;
    }

    if (y == 1) {
        return BLOCK_GRASS;
    }

    return BLOCK_AIR;
}

static int get_block_edit_index(int x, int y, int z) {
    for (int i = 0; i < block_edit_count; i++) {
        if (
            block_edits[i].x == x &&
            block_edits[i].y == y &&
            block_edits[i].z == z
        ) {
            return i;
        }
    }

    return -1;
}

static int get_block_type(int x, int y, int z) {
    const int edit_index = get_block_edit_index(x, y, z);

    if (edit_index >= 0) {
        return block_edits[edit_index].type;
    }

    return get_generated_block_type(x, y, z);
}

static void remove_block_edit_at_index(int index) {
    if (index < 0 || index >= block_edit_count) {
        return;
    }

    block_edit_count--;

    if (index != block_edit_count) {
        block_edits[index] = block_edits[block_edit_count];
    }
}

static void set_block_type(int x, int y, int z, int type) {
    if (y < 0 || y >= WORLD_HEIGHT) {
        return;
    }

    const int generated_type = get_generated_block_type(x, y, z);
    const int edit_index = get_block_edit_index(x, y, z);

    if (type == generated_type) {
        if (edit_index >= 0) {
            remove_block_edit_at_index(edit_index);
        }

        mesh_dirty = 1;
        return;
    }

    if (edit_index >= 0) {
        block_edits[edit_index].type = (uint8_t)type;
        mesh_dirty = 1;
        return;
    }

    if (block_edit_count >= MAX_BLOCK_EDITS) {
        return;
    }

    block_edits[block_edit_count].x = x;
    block_edits[block_edit_count].y = y;
    block_edits[block_edit_count].z = z;
    block_edits[block_edit_count].type = (uint8_t)type;
    block_edit_count++;

    mesh_dirty = 1;
}

static int get_top_solid_block_y(int tile_x, int tile_z) {
    for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
        if (get_block_type(tile_x, y, tile_z) != BLOCK_AIR) {
            return y;
        }
    }

    return -1;
}

static void setup_context(RenderContext *context, int w, int h, int r, int g, int b) {
    SetDefDrawEnv(&(context->buffers[0].draw_env), 0, 0, w, h);
    SetDefDispEnv(&(context->buffers[0].disp_env), 0, 0, w, h);

    SetDefDrawEnv(&(context->buffers[1].draw_env), 0, h, w, h);
    SetDefDispEnv(&(context->buffers[1].disp_env), 0, h, w, h);

    setRGB0(&(context->buffers[0].draw_env), r, g, b);
    setRGB0(&(context->buffers[1].draw_env), r, g, b);

    context->buffers[0].draw_env.isbg = 1;
    context->buffers[1].draw_env.isbg = 1;

    context->active_buffer = 0;
    context->next_packet = context->buffers[0].buffer;

    ClearOTagR(context->buffers[0].ot, OT_LENGTH);

    SetDispMask(1);
}

static void upload_texture_assets(void) {
    RECT atlas_rect;

    setRECT(
        &atlas_rect,
        TEXTURE_ATLAS_X,
        TEXTURE_ATLAS_Y,
        TEXTURE_ATLAS_W,
        TEXTURE_ATLAS_H
    );

    LoadImage(&atlas_rect, (uint32_t *)terrain_log_atlas_16bpp);
    DrawSync(0);
}

static void flip_buffers(RenderContext *context) {
    DrawSync(0);
    VSync(0);

    RenderBuffer *draw_buffer = &(context->buffers[context->active_buffer]);
    RenderBuffer *disp_buffer = &(context->buffers[context->active_buffer ^ 1]);

    PutDispEnv(&(disp_buffer->disp_env));
    DrawOTagEnv(&(draw_buffer->ot[OT_LENGTH - 1]), &(draw_buffer->draw_env));

    context->active_buffer ^= 1;
    context->next_packet = disp_buffer->buffer;

    ClearOTagR(disp_buffer->ot, OT_LENGTH);
}

static void *new_primitive(RenderContext *context, int z, size_t size) {
    if (z < 0) {
        z = 0;
    }

    if (z >= OT_LENGTH) {
        z = OT_LENGTH - 1;
    }

    RenderBuffer *buffer = &(context->buffers[context->active_buffer]);
    uint8_t *prim = context->next_packet;

    addPrim(&(buffer->ot[z]), prim);

    context->next_packet += size;
    assert(context->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));

    return (void *)prim;
}

static void draw_text(RenderContext *context, int x, int y, int z, const char *text) {
    RenderBuffer *buffer = &(context->buffers[context->active_buffer]);

    context->next_packet = (uint8_t *)FntSort(
        &(buffer->ot[z]),
        context->next_packet,
        x,
        y,
        text
    );

    assert(context->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

static void draw_line(
    RenderContext *context,
    int x0,
    int y0,
    int x1,
    int y1,
    int z,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    LINE_F2 *line = (LINE_F2 *)new_primitive(context, z, sizeof(LINE_F2));

    setLineF2(line);
    setRGB0(line, r, g, b);
    setXY2(line, x0, y0, x1, y1);
}

static void draw_filled_rect(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int z,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    TILE *tile = (TILE *)new_primitive(context, z, sizeof(TILE));

    setTile(tile);
    setRGB0(tile, r, g, b);
    setXY0(tile, x, y);
    setWH(tile, w, h);
}

static void draw_panel(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int z,
    uint8_t fill_r,
    uint8_t fill_g,
    uint8_t fill_b,
    uint8_t border_r,
    uint8_t border_g,
    uint8_t border_b
) {
    draw_filled_rect(context, x + 3, y + 3, w, h, z + 2, 20, 20, 24);
    draw_filled_rect(context, x, y, w, h, z + 1, fill_r, fill_g, fill_b);

    draw_line(context, x, y, x + w - 1, y, z, border_r, border_g, border_b);
    draw_line(context, x, y, x, y + h - 1, z, border_r, border_g, border_b);
    draw_line(context, x + w - 1, y, x + w - 1, y + h - 1, z, 28, 28, 32);
    draw_line(context, x, y + h - 1, x + w - 1, y + h - 1, z, 28, 28, 32);

    draw_line(context, x + 1, y + 1, x + w - 2, y + 1, z, 210, 210, 210);
    draw_line(context, x + 1, y + 1, x + 1, y + h - 2, z, 210, 210, 210);
}

static void draw_crosshair(RenderContext *context) {
    const int cx = SCREEN_W / 2;
    const int cy = SCREEN_H / 2;

    draw_line(context, cx - 5, cy, cx - 2, cy, 0, 235, 235, 235);
    draw_line(context, cx + 2, cy, cx + 5, cy, 0, 235, 235, 235);
    draw_line(context, cx, cy - 5, cx, cy - 2, 0, 235, 235, 235);
    draw_line(context, cx, cy + 2, cx, cy + 5, 0, 235, 235, 235);
}

static int project_breaking_world_point(
    int world_x,
    int world_y,
    int world_z,
    ProjectedVertex *projected
) {
    const int sin_y = isin(-camera_yaw);
    const int cos_y = icos(-camera_yaw);

    const int sin_x = isin(camera_pitch);
    const int cos_x = icos(camera_pitch);

    const int rel_x = world_x - camera_pos_x;
    const int rel_y = world_y - camera_pos_y;
    const int rel_z = world_z - camera_pos_z;

    const int x1 = ((rel_x * cos_y) + (rel_z * sin_y)) / FIXED_ONE;
    const int z1 = ((-rel_x * sin_y) + (rel_z * cos_y)) / FIXED_ONE;

    const int y2 = ((rel_y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
    const int z2 = ((rel_y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

    if (z2 < NEAR_PLANE_Z || z2 > FAR_PLANE_Z) {
        return 0;
    }

    projected->x = (SCREEN_W / 2) + ((x1 * FOCAL_LENGTH) / z2);
    projected->y = (SCREEN_H / 2) - ((y2 * FOCAL_LENGTH) / z2);
    projected->z = z2;

    return 1;
}

static ProjectedVertex interpolate_breaking_face_point(
    const ProjectedVertex *p0,
    const ProjectedVertex *p1,
    const ProjectedVertex *p3,
    int u,
    int v
) {
    ProjectedVertex result;

    result.x = p0->x + (((p1->x - p0->x) * u) / 16) + (((p3->x - p0->x) * v) / 16);
    result.y = p0->y + (((p1->y - p0->y) * u) / 16) + (((p3->y - p0->y) * v) / 16);
    result.z = p0->z + (((p1->z - p0->z) * u) / 16) + (((p3->z - p0->z) * v) / 16);

    return result;
}

static void draw_breaking_crack_line(
    RenderContext *context,
    const ProjectedVertex *p0,
    const ProjectedVertex *p1,
    const ProjectedVertex *p3,
    int u0,
    int v0,
    int u1,
    int v1
) {
    const ProjectedVertex a = interpolate_breaking_face_point(p0, p1, p3, u0, v0);
    const ProjectedVertex b = interpolate_breaking_face_point(p0, p1, p3, u1, v1);

    /*
     * Two very close dark lines make the crack readable on noisy block colors.
     */
    draw_line(context, a.x, a.y, b.x, b.y, 0, 8, 8, 8);
    draw_line(context, a.x + 1, a.y, b.x + 1, b.y, 0, 42, 42, 42);
}

static int get_breaking_face_vertices(ProjectedVertex out[4]) {
    const int x0 = tile_to_world_center(breaking_block_x) - BLOCK_HALF;
    const int x1 = tile_to_world_center(breaking_block_x) + BLOCK_HALF;
    const int y0 = block_y_to_world_min(breaking_block_y);
    const int y1 = block_y_to_world_top(breaking_block_y);
    const int z0 = tile_to_world_center(breaking_block_z) - BLOCK_HALF;
    const int z1 = tile_to_world_center(breaking_block_z) + BLOCK_HALF;

    int wx[4];
    int wy[4];
    int wz[4];

    switch (breaking_block_face) {
        case FACE_NEG_X:
            wx[0] = x0; wy[0] = y0; wz[0] = z1;
            wx[1] = x0; wy[1] = y1; wz[1] = z1;
            wx[2] = x0; wy[2] = y1; wz[2] = z0;
            wx[3] = x0; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_X:
            wx[0] = x1; wy[0] = y0; wz[0] = z0;
            wx[1] = x1; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z1;
            wx[3] = x1; wy[3] = y0; wz[3] = z1;
            break;

        case FACE_NEG_Z:
            wx[0] = x0; wy[0] = y0; wz[0] = z0;
            wx[1] = x0; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z0;
            wx[3] = x1; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_Z:
            wx[0] = x1; wy[0] = y0; wz[0] = z1;
            wx[1] = x1; wy[1] = y1; wz[1] = z1;
            wx[2] = x0; wy[2] = y1; wz[2] = z1;
            wx[3] = x0; wy[3] = y0; wz[3] = z1;
            break;

        case FACE_NEG_Y:
            wx[0] = x0; wy[0] = y0; wz[0] = z1;
            wx[1] = x1; wy[1] = y0; wz[1] = z1;
            wx[2] = x1; wy[2] = y0; wz[2] = z0;
            wx[3] = x0; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_Y:
        default:
            wx[0] = x0; wy[0] = y1; wz[0] = z0;
            wx[1] = x1; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z1;
            wx[3] = x0; wy[3] = y1; wz[3] = z1;
            break;
    }

    for (int i = 0; i < 4; i++) {
        if (!project_breaking_world_point(wx[i], wy[i], wz[i], &(out[i]))) {
            return 0;
        }
    }

    return 1;
}

static void draw_breaking_overlay(RenderContext *context) {
    if (!breaking_active || breaking_required_frames <= 0) {
        return;
    }

    ProjectedVertex face[4];
    int stage = (breaking_progress * 7) / breaking_required_frames;

    if (stage < 1) {
        stage = 1;
    }

    if (stage > 7) {
        stage = 7;
    }

    if (!get_breaking_face_vertices(face)) {
        return;
    }

    /*
     * Minecraft-like cracks projected onto the actual targeted block face.
     * No progress bar and no cursor-space sticks.
     */
    draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 7, 2, 8, 7);

    if (stage >= 2) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 5, 10);
    }

    if (stage >= 3) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 12, 9);
    }

    if (stage >= 4) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 5, 10, 3, 14);
    }

    if (stage >= 5) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 12, 9, 14, 13);
    }

    if (stage >= 6) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 7, 2, 4, 4);
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 10, 4);
    }

    if (stage >= 7) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 5, 10, 8, 14);
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 12, 9, 11, 15);
    }
}

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


static void init_input(void) {
    InitPAD(
        pad_buffers[0],
        PAD_BUFFER_SIZE,
        pad_buffers[1],
        PAD_BUFFER_SIZE
    );

    StartPAD();
    ChangeClearPAD(1);
}

static uint16_t read_pad_buttons(void) {
    PADTYPE *pad = (PADTYPE *)pad_buffers[0];

    if (pad->stat != 0) {
        return 0;
    }

    if (
        pad->type != PAD_ID_DIGITAL &&
        pad->type != PAD_ID_ANALOG_STICK &&
        pad->type != PAD_ID_ANALOG
    ) {
        return 0;
    }

    /*
     * PS1 pad buttons are active-low:
     * bit = 0 means pressed.
     * Invert it so pressed buttons become 1.
     */
    return (uint16_t)(~pad->btn);
}

static int get_surface_top_block_y_at_world_position(int world_x, int world_z) {
    return get_top_solid_block_y(
        world_to_tile(world_x),
        world_to_tile(world_z)
    );
}

static int get_player_center_top_block_y(void) {
    return get_surface_top_block_y_at_world_position(
        camera_pos_x,
        camera_pos_z
    );
}

static int is_player_footprint_clear_at(
    int world_x,
    int world_z,
    int target_top_block_y
) {
    int sample_x[9];
    int sample_z[9];

    /*
     * Square-ish collision footprint.
     *
     * This is intentionally not a renderer fix. The issue is that the camera
     * can get close enough to a side wall that the near plane clips into the
     * voxel. When that happens, blue background leaks through while turning.
     *
     * We keep the camera farther from block faces by testing a small player
     * body footprint before accepting movement.
     */
    sample_x[0] = world_x;
    sample_z[0] = world_z;

    sample_x[1] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[1] = world_z;

    sample_x[2] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[2] = world_z;

    sample_x[3] = world_x;
    sample_z[3] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[4] = world_x;
    sample_z[4] = world_z + PLAYER_COLLISION_RADIUS;

    sample_x[5] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[5] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[6] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[6] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[7] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[7] = world_z + PLAYER_COLLISION_RADIUS;

    sample_x[8] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[8] = world_z + PLAYER_COLLISION_RADIUS;

    for (int i = 0; i < 9; i++) {
        const int sample_top_block_y = get_surface_top_block_y_at_world_position(
            sample_x[i],
            sample_z[i]
        );

        if (sample_top_block_y < 0) {
            return 0;
        }

        /*
         * Footprint rule:
         *
         * Without autojump, higher blocks inside the player's collision radius
         * are walls.
         *
         * With autojump enabled, allow the footprint to touch a block that is
         * one level higher. Otherwise the player can never get close enough for
         * the center point to cross onto the higher tile.
         */
        if (sample_top_block_y > target_top_block_y) {
            if (!autojump_enabled) {
                return 0;
            }

            if ((sample_top_block_y - target_top_block_y) > 1) {
                return 0;
            }
        }
    }

    return 1;
}

static void snap_camera_to_ground(void) {
    const int top_block_y = get_player_center_top_block_y();

    if (top_block_y < 0) {
        return;
    }

    camera_pos_y = block_y_to_world_top(top_block_y) + PLAYER_EYE_HEIGHT;
}

static void reset_camera(void) {
    fly_mode_enabled = 0;

    camera_pos_x = 0;
    camera_pos_z = 0;
    camera_yaw = 0;
    camera_pitch = DEFAULT_CAMERA_PITCH;

    snap_camera_to_ground();

    mesh_center_tile_x = 999999;
    mesh_center_tile_z = 999999;
    mesh_dirty = 1;
}

static int is_walk_target_valid(int next_x, int next_z) {
    const int current_top_block_y = get_player_center_top_block_y();
    const int next_top_block_y = get_surface_top_block_y_at_world_position(
        next_x,
        next_z
    );

    if (current_top_block_y < 0 || next_top_block_y < 0) {
        return 0;
    }

    /*
     * Optional Minecraft-like autojump.
     * ON: allow stepping up exactly one block.
     * OFF: higher blocks behave like walls.
     */
    if (next_top_block_y > current_top_block_y) {
        if (!autojump_enabled) {
            return 0;
        }

        if ((next_top_block_y - current_top_block_y) > 1) {
            return 0;
        }
    }

    /*
     * Allow a small drop, but do not walk off large cliffs/holes yet.
     */
    if ((current_top_block_y - next_top_block_y) > MAX_AUTO_DROP_BLOCKS) {
        return 0;
    }

    if (!is_player_footprint_clear_at(
        next_x,
        next_z,
        next_top_block_y
    )) {
        return 0;
    }

    return 1;
}

static void try_walk_move(int delta_x, int delta_z) {
    const int next_x = camera_pos_x + delta_x;
    const int next_z = camera_pos_z + delta_z;

    if (!is_walk_target_valid(next_x, next_z)) {
        return;
    }

    camera_pos_x = next_x;
    camera_pos_z = next_z;

    snap_camera_to_ground();
}

static void get_camera_forward_direction(Vec3i *dir) {
    const int cos_pitch = icos(camera_pitch);

    dir->x = (isin(camera_yaw) * cos_pitch) / FIXED_ONE;
    dir->y = isin(camera_pitch);
    dir->z = (icos(camera_yaw) * cos_pitch) / FIXED_ONE;
}

static int is_valid_block_position(int x, int y, int z) {
    (void)x;
    (void)z;
    return (y >= 0 && y < WORLD_HEIGHT);
}

static int get_hit_face_from_empty_block(
    int empty_x,
    int empty_y,
    int empty_z,
    int block_x,
    int block_y,
    int block_z
) {
    if (empty_x < block_x) {
        return FACE_NEG_X;
    }

    if (empty_x > block_x) {
        return FACE_POS_X;
    }

    if (empty_z < block_z) {
        return FACE_NEG_Z;
    }

    if (empty_z > block_z) {
        return FACE_POS_Z;
    }

    if (empty_y < block_y) {
        return FACE_NEG_Y;
    }

    return FACE_POS_Y;
}

static RaycastHit raycast_block(void) {
    RaycastHit hit;
    Vec3i dir;

    hit.found = 0;
    hit.hit_x = 0;
    hit.hit_y = 0;
    hit.hit_z = 0;
    hit.hit_face = FACE_POS_Y;
    hit.place_x = 0;
    hit.place_y = 0;
    hit.place_z = 0;

    get_camera_forward_direction(&dir);

    {
        int last_empty_valid = 0;
        int last_empty_x = 0;
        int last_empty_y = 0;
        int last_empty_z = 0;

        int previous_x = 999999;
        int previous_y = 999999;
        int previous_z = 999999;

        for (int distance = 8; distance <= RAYCAST_MAX_DISTANCE; distance += RAYCAST_STEP) {
            const int sample_x = camera_pos_x + ((dir.x * distance) / FIXED_ONE);
            const int sample_y = camera_pos_y + ((dir.y * distance) / FIXED_ONE);
            const int sample_z = camera_pos_z + ((dir.z * distance) / FIXED_ONE);

            const int block_x = world_to_tile(sample_x);
            const int block_y = world_to_block_y(sample_y);
            const int block_z = world_to_tile(sample_z);

            if (
                block_x == previous_x &&
                block_y == previous_y &&
                block_z == previous_z
            ) {
                continue;
            }

            previous_x = block_x;
            previous_y = block_y;
            previous_z = block_z;

            if (!is_valid_block_position(block_x, block_y, block_z)) {
                continue;
            }

            if (get_block_type(block_x, block_y, block_z) != BLOCK_AIR) {
                hit.found = 1;
                hit.hit_x = block_x;
                hit.hit_y = block_y;
                hit.hit_z = block_z;

                if (last_empty_valid) {
                    hit.hit_face = get_hit_face_from_empty_block(
                        last_empty_x,
                        last_empty_y,
                        last_empty_z,
                        block_x,
                        block_y,
                        block_z
                    );
                    hit.place_x = last_empty_x;
                    hit.place_y = last_empty_y;
                    hit.place_z = last_empty_z;
                } else {
                    hit.hit_face = FACE_POS_Y;
                    hit.place_x = block_x;
                    hit.place_y = block_y + 1;
                    hit.place_z = block_z;
                }

                return hit;
            }

            last_empty_valid = 1;
            last_empty_x = block_x;
            last_empty_y = block_y;
            last_empty_z = block_z;
        }
    }

    return hit;
}

static int get_block_hardness_frames(int block_type) {
    if (block_type == BLOCK_SAND) {
        return BLOCK_BREAK_SAND_FRAMES;
    }

    if (block_type == BLOCK_STONE) {
        return BLOCK_BREAK_STONE_FRAMES;
    }

    if (block_type == BLOCK_GRASS) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_LOG) {
        return BLOCK_BREAK_STONE_FRAMES;
    }

    if (block_type == BLOCK_PLANKS) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_WORKBENCH) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_DIRT) {
        return BLOCK_BREAK_DIRT_FRAMES;
    }

    return BLOCK_BREAK_MIN_FRAMES;
}

static void reset_block_breaking(void) {
    breaking_active = 0;
    breaking_block_x = 0;
    breaking_block_y = 0;
    breaking_block_z = 0;
    breaking_block_face = FACE_POS_Y;
    breaking_block_type = BLOCK_AIR;
    breaking_progress = 0;
    breaking_required_frames = BLOCK_BREAK_MIN_FRAMES;
}

static void update_block_breaking(int is_break_button_down) {
    const RaycastHit hit = raycast_block();

    if (!is_break_button_down || !hit.found) {
        reset_block_breaking();
        return;
    }

    const int block_type = get_block_type(hit.hit_x, hit.hit_y, hit.hit_z);

    if (block_type == BLOCK_AIR) {
        reset_block_breaking();
        return;
    }

    if (
        !breaking_active ||
        breaking_block_x != hit.hit_x ||
        breaking_block_y != hit.hit_y ||
        breaking_block_z != hit.hit_z ||
        breaking_block_face != hit.hit_face ||
        breaking_block_type != block_type
    ) {
        breaking_active = 1;
        breaking_block_x = hit.hit_x;
        breaking_block_y = hit.hit_y;
        breaking_block_z = hit.hit_z;
        breaking_block_face = hit.hit_face;
        breaking_block_type = block_type;
        breaking_progress = 0;
        breaking_required_frames = get_block_hardness_frames(block_type);
        set_system_status("BREAKING...", 30);
    }

    /*
     * Keep this intentionally slow and visible.
     * Earlier values felt identical to instant block deletion.
     */
    if (breaking_progress < breaking_required_frames) {
        breaking_progress++;
    }

    if (breaking_progress >= breaking_required_frames) {
        set_block_type(hit.hit_x, hit.hit_y, hit.hit_z, BLOCK_AIR);
        spawn_dropped_item(block_type, hit.hit_x, hit.hit_y, hit.hit_z);

        if (!fly_mode_enabled) {
            snap_camera_to_ground();
        }

        set_system_status("BLOCK BROKEN", 45);
        reset_block_breaking();
    }
}

static int get_block_drop_type(int block_type) {
    if (block_type == BLOCK_GRASS) {
        return BLOCK_DIRT;
    }

    return block_type;
}

static void reset_dropped_items(void) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        dropped_items[i].active = 0;
        dropped_items[i].x = 0;
        dropped_items[i].y = 0;
        dropped_items[i].z = 0;
        dropped_items[i].type = BLOCK_AIR;
        dropped_items[i].count = 0;
        dropped_items[i].bob_frame = 0;
    }
}

static void spawn_dropped_item(int block_type, int block_x, int block_y, int block_z) {
    const int drop_type = get_block_drop_type(block_type);

    if (drop_type == BLOCK_AIR) {
        return;
    }

    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            dropped_items[i].active = 1;
            dropped_items[i].x = tile_to_world_center(block_x);
            dropped_items[i].y = block_y_to_world_min(block_y) + BLOCK_HALF;
            dropped_items[i].z = tile_to_world_center(block_z);
            dropped_items[i].type = (uint8_t)drop_type;
            dropped_items[i].count = 1;
            dropped_items[i].bob_frame = 0;
            return;
        }
    }

    /*
     * Fallback: if the world is full of drops, reward the item directly.
     */
    add_items_to_inventory((uint8_t)drop_type, 1);
}

static void update_dropped_items(void) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            continue;
        }

        dropped_items[i].bob_frame++;

        if (
            iabs(dropped_items[i].x - camera_pos_x) <= PICKUP_DISTANCE_XZ &&
            iabs(dropped_items[i].z - camera_pos_z) <= PICKUP_DISTANCE_XZ &&
            iabs(dropped_items[i].y - camera_pos_y) <= PICKUP_DISTANCE_Y
        ) {
            const int remaining = add_items_to_inventory(
                dropped_items[i].type,
                dropped_items[i].count
            );

            if (remaining <= 0) {
                dropped_items[i].active = 0;
                set_system_status("PICKED UP", 35);
            } else {
                dropped_items[i].count = (uint8_t)remaining;
            }
        }
    }
}

static int block_intersects_player_footprint(int block_x, int block_y, int block_z) {
    const int player_top_block_y = get_player_center_top_block_y();

    /*
     * Only care about blocks around player's body height. This prevents placing
     * a block into your own collision space.
     */
    if (player_top_block_y >= 0 && block_y <= player_top_block_y) {
        return 0;
    }

    const int block_min_x = tile_to_world_center(block_x) - BLOCK_HALF;
    const int block_max_x = tile_to_world_center(block_x) + BLOCK_HALF;
    const int block_min_z = tile_to_world_center(block_z) - BLOCK_HALF;
    const int block_max_z = tile_to_world_center(block_z) + BLOCK_HALF;

    const int player_min_x = camera_pos_x - PLAYER_COLLISION_RADIUS;
    const int player_max_x = camera_pos_x + PLAYER_COLLISION_RADIUS;
    const int player_min_z = camera_pos_z - PLAYER_COLLISION_RADIUS;
    const int player_max_z = camera_pos_z + PLAYER_COLLISION_RADIUS;

    if (player_max_x <= block_min_x || player_min_x >= block_max_x) {
        return 0;
    }

    if (player_max_z <= block_min_z || player_min_z >= block_max_z) {
        return 0;
    }

    return 1;
}

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
        item_type == BLOCK_STONE ||
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
        if (hotbar_slot_blocks[i].type == type) {
            capacity += STACK_MAX_COUNT - hotbar_slot_blocks[i].count;
        } else if (stack_is_empty(&(hotbar_slot_blocks[i]))) {
            capacity += STACK_MAX_COUNT;
        }
    }

    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        if (inventory_storage_blocks[i].type == type) {
            capacity += STACK_MAX_COUNT - inventory_storage_blocks[i].count;
        } else if (stack_is_empty(&(inventory_storage_blocks[i]))) {
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
        hotbar_slot_blocks,
        HOTBAR_SLOT_COUNT,
        type,
        amount
    );

    remaining = add_to_existing_stack_array(
        inventory_storage_blocks,
        INVENTORY_STORAGE_SLOT_COUNT,
        type,
        remaining
    );

    remaining = add_to_empty_stack_array(
        hotbar_slot_blocks,
        HOTBAR_SLOT_COUNT,
        type,
        remaining
    );

    remaining = add_to_empty_stack_array(
        inventory_storage_blocks,
        INVENTORY_STORAGE_SLOT_COUNT,
        type,
        remaining
    );

    return remaining;
}

static int get_selected_hotbar_block_type(void) {
    if (selected_hotbar_slot < 0 || selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        return BLOCK_AIR;
    }

    if (stack_is_empty(&(hotbar_slot_blocks[selected_hotbar_slot]))) {
        return BLOCK_AIR;
    }

    return hotbar_slot_blocks[selected_hotbar_slot].type;
}

static void consume_selected_hotbar_block(void) {
    ItemStack *stack;

    if (selected_hotbar_slot < 0 || selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        return;
    }

    stack = &(hotbar_slot_blocks[selected_hotbar_slot]);

    if (stack_is_empty(stack)) {
        return;
    }

    if (stack->count > 0) {
        stack->count--;
    }

    normalize_stack(stack);
}

static void select_previous_hotbar_slot(void) {
    selected_hotbar_slot--;

    if (selected_hotbar_slot < 0) {
        selected_hotbar_slot = HOTBAR_SLOT_COUNT - 1;
    }
}

static void select_next_hotbar_slot(void) {
    selected_hotbar_slot++;

    if (selected_hotbar_slot >= HOTBAR_SLOT_COUNT) {
        selected_hotbar_slot = 0;
    }
}

static void add_target_block(void) {
    const RaycastHit hit = raycast_block();
    const int selected_block_type = get_selected_hotbar_block_type();

    if (selected_block_type == BLOCK_AIR) {
        set_system_status("EMPTY SLOT", 45);
        return;
    }

    if (!is_placeable_block_type(selected_block_type)) {
        set_system_status("NOT A BLOCK", 45);
        return;
    }

    if (!hit.found) {
        return;
    }

    if (!is_valid_block_position(hit.place_x, hit.place_y, hit.place_z)) {
        return;
    }

    if (get_block_type(hit.place_x, hit.place_y, hit.place_z) != BLOCK_AIR) {
        return;
    }

    if (block_intersects_player_footprint(
        hit.place_x,
        hit.place_y,
        hit.place_z
    )) {
        return;
    }

    set_block_type(hit.place_x, hit.place_y, hit.place_z, selected_block_type);
    consume_selected_hotbar_block();
}

static void update_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    const int forward_x = isin(camera_yaw);
    const int forward_z = icos(camera_yaw);

    const int right_x = icos(camera_yaw);
    const int right_z = -isin(camera_yaw);

    if (pressed_this_frame & PAD_START) {
        reset_block_breaking();
        app_state = APP_STATE_PAUSE;
        pause_selected_option = PAUSE_OPTION_RESUME;
        pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_SELECT) {
        if (save_game_to_memory_card()) {
            set_system_status("SAVE OK", 90);
        } else {
            set_system_status("SAVE FAILED", 120);
        }
    }

    update_dropped_items();
    update_block_breaking((buttons & PAD_SQUARE) != 0);

    if (pressed_this_frame & PAD_CIRCLE) {
        reset_block_breaking();
        add_target_block();
    }

    if (pressed_this_frame & PAD_CROSS) {
        const RaycastHit use_hit = raycast_block();

        if (
            use_hit.found &&
            get_block_type(use_hit.hit_x, use_hit.hit_y, use_hit.hit_z) == BLOCK_WORKBENCH
        ) {
            reset_block_breaking();
            app_state = APP_STATE_WORKBENCH;
            workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START;
            set_system_status("WORKBENCH", 45);
            pad_previous_buttons = buttons;
            return;
        }
    }

    if (buttons & PAD_LEFT) {
        camera_yaw = (camera_yaw - CAMERA_YAW_SPEED) & ANGLE_MASK;
    }

    if (buttons & PAD_RIGHT) {
        camera_yaw = (camera_yaw + CAMERA_YAW_SPEED) & ANGLE_MASK;
    }

    if (buttons & PAD_TRIANGLE) {
        camera_pitch -= CAMERA_PITCH_SPEED;
    }

    if (buttons & PAD_CROSS) {
        camera_pitch += CAMERA_PITCH_SPEED;
    }

    if (!fly_mode_enabled) {
        if (pressed_this_frame & PAD_L2) {
            select_previous_hotbar_slot();
        }

        if (pressed_this_frame & PAD_R2) {
            select_next_hotbar_slot();
        }
    }

    camera_pitch = clamp_int(
        camera_pitch,
        CAMERA_PITCH_MIN,
        CAMERA_PITCH_MAX
    );

    if (fly_mode_enabled) {
        if (buttons & PAD_UP) {
            camera_pos_x += (forward_x * FLY_MOVE_SPEED) / FIXED_ONE;
            camera_pos_z += (forward_z * FLY_MOVE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_DOWN) {
            camera_pos_x -= (forward_x * FLY_MOVE_SPEED) / FIXED_ONE;
            camera_pos_z -= (forward_z * FLY_MOVE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_L1) {
            camera_pos_x -= (right_x * FLY_STRAFE_SPEED) / FIXED_ONE;
            camera_pos_z -= (right_z * FLY_STRAFE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_R1) {
            camera_pos_x += (right_x * FLY_STRAFE_SPEED) / FIXED_ONE;
            camera_pos_z += (right_z * FLY_STRAFE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_L2) {
            camera_pos_y += FLY_VERTICAL_SPEED;
        }

        if (buttons & PAD_R2) {
            camera_pos_y -= FLY_VERTICAL_SPEED;
        }
    } else {
        if (buttons & PAD_UP) {
            try_walk_move(
                (forward_x * WALK_MOVE_SPEED) / FIXED_ONE,
                (forward_z * WALK_MOVE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_DOWN) {
            try_walk_move(
                -(forward_x * WALK_MOVE_SPEED) / FIXED_ONE,
                -(forward_z * WALK_MOVE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_L1) {
            try_walk_move(
                -(right_x * WALK_STRAFE_SPEED) / FIXED_ONE,
                -(right_z * WALK_STRAFE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_R1) {
            try_walk_move(
                (right_x * WALK_STRAFE_SPEED) / FIXED_ONE,
                (right_z * WALK_STRAFE_SPEED) / FIXED_ONE
            );
        }

        snap_camera_to_ground();
    }

    pad_previous_buttons = buttons;
}

static void clear_vertex_lookup(void) {
    for (int y = 0; y < GRID_Y_LINES; y++) {
        for (int z = 0; z < GRID_VERTICES_PER_SIDE; z++) {
            for (int x = 0; x < GRID_VERTICES_PER_SIDE; x++) {
                vertex_lookup[y][z][x] = -1;
            }
        }
    }
}

static void build_local_block_cache(int center_tile_x, int center_tile_z) {
    const int start_tile_x = center_tile_x - VIEW_RADIUS;
    const int start_tile_z = center_tile_z - VIEW_RADIUS;

    /*
     * local_blocks has a 1-block border around the visible 17x17 area.
     * That lets us test neighbor blocks in O(1) without calling get_block_type()
     * thousands of times during mesh generation.
     */
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int z = 0; z < PADDED_VIEW_SIZE; z++) {
            for (int x = 0; x < PADDED_VIEW_SIZE; x++) {
                const int world_x = start_tile_x + x - 1;
                const int world_z = start_tile_z + z - 1;

                local_blocks[y][z][x] = (uint8_t)get_generated_block_type(
                    world_x,
                    y,
                    world_z
                );
            }
        }
    }

    /*
     * Apply sparse edits only once per rebuild.
     * Previous version scanned edits for every block lookup; this is what made
     * each placed block progressively slow the game down.
     */
    for (int i = 0; i < block_edit_count; i++) {
        const int local_x = block_edits[i].x - start_tile_x + 1;
        const int local_z = block_edits[i].z - start_tile_z + 1;
        const int y = block_edits[i].y;

        if (
            local_x < 0 ||
            local_x >= PADDED_VIEW_SIZE ||
            local_z < 0 ||
            local_z >= PADDED_VIEW_SIZE ||
            y < 0 ||
            y >= WORLD_HEIGHT
        ) {
            continue;
        }

        local_blocks[y][local_z][local_x] = block_edits[i].type;
    }
}

static int get_local_block_type(int visible_x, int y, int visible_z) {
    /*
     * visible_x/visible_z are usually 0..16.
     * -1 and VIEW_SIZE are allowed for neighbor checks because local_blocks
     * has a 1-block border.
     */
    const int local_x = visible_x + 1;
    const int local_z = visible_z + 1;

    if (
        local_x < 0 ||
        local_x >= PADDED_VIEW_SIZE ||
        local_z < 0 ||
        local_z >= PADDED_VIEW_SIZE ||
        y < 0 ||
        y >= WORLD_HEIGHT
    ) {
        return BLOCK_AIR;
    }

    return local_blocks[y][local_z][local_x];
}

static int add_grid_vertex(
    int local_x_line,
    int y_line,
    int local_z_line,
    int start_world_x,
    int start_world_z
) {
    int *lookup_entry = &(vertex_lookup[y_line][local_z_line][local_x_line]);

    if (*lookup_entry >= 0) {
        return *lookup_entry;
    }

    if (mesh_vertex_count >= MAX_MESH_VERTICES) {
        return 0;
    }

    Vec3i *vertex = &(mesh_vertices[mesh_vertex_count]);

    vertex->x = start_world_x + (local_x_line * BLOCK_SIZE);
    vertex->y = block_y_to_world_min(y_line);
    vertex->z = start_world_z + (local_z_line * BLOCK_SIZE);

    *lookup_entry = mesh_vertex_count;
    mesh_vertex_count++;

    return *lookup_entry;
}

static void push_face_by_grid_vertices(
    int x0,
    int y0,
    int z0,
    int x1,
    int y1,
    int z1,
    int x2,
    int y2,
    int z2,
    int x3,
    int y3,
    int z3,
    int start_world_x,
    int start_world_z,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t block_type,
    uint8_t face_type
) {
    if (mesh_face_count >= MAX_MESH_FACES) {
        return;
    }

    if (
        x0 < 0 || x0 >= GRID_VERTICES_PER_SIDE ||
        x1 < 0 || x1 >= GRID_VERTICES_PER_SIDE ||
        x2 < 0 || x2 >= GRID_VERTICES_PER_SIDE ||
        x3 < 0 || x3 >= GRID_VERTICES_PER_SIDE ||
        z0 < 0 || z0 >= GRID_VERTICES_PER_SIDE ||
        z1 < 0 || z1 >= GRID_VERTICES_PER_SIDE ||
        z2 < 0 || z2 >= GRID_VERTICES_PER_SIDE ||
        z3 < 0 || z3 >= GRID_VERTICES_PER_SIDE ||
        y0 < 0 || y0 >= GRID_Y_LINES ||
        y1 < 0 || y1 >= GRID_Y_LINES ||
        y2 < 0 || y2 >= GRID_Y_LINES ||
        y3 < 0 || y3 >= GRID_Y_LINES
    ) {
        return;
    }

    MeshFace *face = &(mesh_faces[mesh_face_count]);

    face->v[0] = (uint16_t)add_grid_vertex(x0, y0, z0, start_world_x, start_world_z);
    face->v[1] = (uint16_t)add_grid_vertex(x1, y1, z1, start_world_x, start_world_z);
    face->v[2] = (uint16_t)add_grid_vertex(x2, y2, z2, start_world_x, start_world_z);
    face->v[3] = (uint16_t)add_grid_vertex(x3, y3, z3, start_world_x, start_world_z);

    face->r = r;
    face->g = g;
    face->b = b;
    face->block_type = block_type;
    face->face_type = face_type;

    mesh_face_count++;
}

static void get_face_color(int block_type, int face, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (block_type == BLOCK_GRASS) {
        if (face == FACE_POS_Y) {
            *r = 64;
            *g = 175;
            *b = 64;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 45;
            *g = 30;
            *b = 18;
            return;
        }

        *r = 118;
        *g = 76;
        *b = 38;
        return;
    }

    if (block_type == BLOCK_DIRT) {
        if (face == FACE_POS_Y) {
            *r = 125;
            *g = 82;
            *b = 44;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 45;
            *g = 30;
            *b = 18;
            return;
        }

        *r = 105;
        *g = 68;
        *b = 35;
        return;
    }

    if (block_type == BLOCK_STONE) {
        if (face == FACE_POS_Y) {
            *r = 128;
            *g = 130;
            *b = 128;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 58;
            *g = 60;
            *b = 58;
            return;
        }

        *r = 100;
        *g = 102;
        *b = 100;
        return;
    }

    if (block_type == BLOCK_SAND) {
        if (face == FACE_POS_Y) {
            *r = 218;
            *g = 202;
            *b = 118;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 118;
            *g = 102;
            *b = 58;
            return;
        }

        *r = 184;
        *g = 166;
        *b = 92;
        return;
    }

    if (block_type == BLOCK_LOG) {
        if (face == FACE_POS_Y || face == FACE_NEG_Y) {
            *r = 148;
            *g = 104;
            *b = 58;
            return;
        }

        *r = 100;
        *g = 68;
        *b = 36;
        return;
    }

    if (block_type == BLOCK_PLANKS) {
        if (face == FACE_POS_Y) {
            *r = 184;
            *g = 132;
            *b = 70;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 92;
            *g = 58;
            *b = 30;
            return;
        }

        *r = 148;
        *g = 96;
        *b = 48;
        return;
    }

    if (block_type == BLOCK_WORKBENCH) {
        if (face == FACE_POS_Y) {
            *r = 172;
            *g = 116;
            *b = 56;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 76;
            *g = 48;
            *b = 24;
            return;
        }

        *r = 126;
        *g = 78;
        *b = 38;
        return;
    }

    *r = 180;
    *g = 180;
    *b = 180;
}

static void push_block_face(
    int local_x,
    int block_y,
    int local_z,
    int face,
    int block_type,
    int start_world_x,
    int start_world_z
) {
    const int x0 = local_x;
    const int x1 = local_x + 1;
    const int y0 = block_y;
    const int y1 = block_y + 1;
    const int z0 = local_z;
    const int z1 = local_z + 1;

    uint8_t r;
    uint8_t g;
    uint8_t b_col;

    get_face_color(block_type, face, &r, &g, &b_col);

    switch (face) {
        case FACE_NEG_Z:
            push_face_by_grid_vertices(
                x0, y0, z0,
                x0, y1, z0,
                x1, y1, z0,
                x1, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_Z:
            push_face_by_grid_vertices(
                x1, y0, z1,
                x1, y1, z1,
                x0, y1, z1,
                x0, y0, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_NEG_X:
            push_face_by_grid_vertices(
                x0, y0, z1,
                x0, y1, z1,
                x0, y1, z0,
                x0, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_X:
            push_face_by_grid_vertices(
                x1, y0, z0,
                x1, y1, z0,
                x1, y1, z1,
                x1, y0, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_NEG_Y:
            push_face_by_grid_vertices(
                x0, y0, z1,
                x1, y0, z1,
                x1, y0, z0,
                x0, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_Y:
        default:
            push_face_by_grid_vertices(
                x0, y1, z0,
                x1, y1, z0,
                x1, y1, z1,
                x0, y1, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;
    }
}

static void build_world_mesh(int center_tile_x, int center_tile_z) {
    const int start_tile_x = center_tile_x - VIEW_RADIUS;
    const int start_tile_z = center_tile_z - VIEW_RADIUS;

    const int start_world_x = tile_to_world_center(start_tile_x) - BLOCK_HALF;
    const int start_world_z = tile_to_world_center(start_tile_z) - BLOCK_HALF;

    mesh_vertex_count = 0;
    mesh_face_count = 0;

    clear_vertex_lookup();
    build_local_block_cache(center_tile_x, center_tile_z);

    for (int z = 0; z < VIEW_SIZE; z++) {
        for (int x = 0; x < VIEW_SIZE; x++) {
            for (int y = 0; y < WORLD_HEIGHT; y++) {
                const int block_type = get_local_block_type(x, y, z);

                if (block_type == BLOCK_AIR) {
                    continue;
                }

                if (get_local_block_type(x, y + 1, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_Y,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                /*
                 * Skip bottom face at world floor to save some performance.
                 * Floating blocks still get bottom faces.
                 */
                if (y > 0 && get_local_block_type(x, y - 1, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_Y,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x, y, z - 1) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_Z,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x, y, z + 1) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_Z,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x - 1, y, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_X,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x + 1, y, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_X,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }
            }
        }
    }

    mesh_center_tile_x = center_tile_x;
    mesh_center_tile_z = center_tile_z;
    mesh_dirty = 0;
}

static void rebuild_mesh_if_needed(void) {
    const int center_tile_x = world_to_tile(camera_pos_x);
    const int center_tile_z = world_to_tile(camera_pos_z);

    if (
        mesh_dirty ||
        center_tile_x != mesh_center_tile_x ||
        center_tile_z != mesh_center_tile_z
    ) {
        build_world_mesh(center_tile_x, center_tile_z);
    }
}

static void transform_all_vertices(void) {
    const int sin_y = isin(-camera_yaw);
    const int cos_y = icos(-camera_yaw);

    const int sin_x = isin(camera_pitch);
    const int cos_x = icos(camera_pitch);

    for (int i = 0; i < mesh_vertex_count; i++) {
        const Vec3i *world = &(mesh_vertices[i]);

        const int rel_x = world->x - camera_pos_x;
        const int rel_y = world->y - camera_pos_y;
        const int rel_z = world->z - camera_pos_z;

        const int x1 = ((rel_x * cos_y) + (rel_z * sin_y)) / FIXED_ONE;
        const int z1 = ((-rel_x * sin_y) + (rel_z * cos_y)) / FIXED_ONE;

        const int y2 = ((rel_y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
        const int z2 = ((rel_y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

        camera_vertices[i].x = x1;
        camera_vertices[i].y = y2;
        camera_vertices[i].z = z2;
    }
}

static int depth_to_ot(int depth) {
    int ot_z = depth / 4;

    if (ot_z < 1) {
        ot_z = 1;
    }

    if (ot_z >= OT_LENGTH) {
        ot_z = OT_LENGTH - 1;
    }

    return ot_z;
}

static uint8_t fog_blend_channel(uint8_t color, uint8_t fog_color, int fog_amount) {
    const int color_part = ((int)color * (256 - fog_amount));
    const int fog_part = ((int)fog_color * fog_amount);

    return (uint8_t)((color_part + fog_part) >> 8);
}

static void apply_distance_fog(
    int depth,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b
) {
    int fog_amount;

    if (!fog_enabled) {
        return;
    }

    if (depth <= FOG_START_Z) {
        return;
    }

    if (depth >= FOG_FULL_Z) {
        fog_amount = 256;
    } else {
        fog_amount = ((depth - FOG_START_Z) * 256) / (FOG_FULL_Z - FOG_START_Z);
    }

    /*
     * Classic PS1 fog: distant polygons are simply recolored toward the
     * background. No screen-space stripes, no alpha, no texture.
     */
    *r = fog_blend_channel(*r, FOG_SKY_R, fog_amount);
    *g = fog_blend_channel(*g, FOG_SKY_G, fog_amount);
    *b = fog_blend_channel(*b, FOG_SKY_B, fog_amount);
}

static Vec3i intersect_near_plane(const Vec3i *a, const Vec3i *b) {
    Vec3i result;

    const int dz = b->z - a->z;
    const int t_num = NEAR_PLANE_Z - a->z;

    if (dz == 0) {
        result = *a;
        result.z = NEAR_PLANE_Z;
        return result;
    }

    result.x = a->x + (((b->x - a->x) * t_num) / dz);
    result.y = a->y + (((b->y - a->y) * t_num) / dz);
    result.z = NEAR_PLANE_Z;

    return result;
}

static int clip_triangle_to_near_plane(const Vec3i input[3], Vec3i output[4]) {
    int output_count = 0;

    Vec3i previous = input[2];
    int previous_inside = previous.z >= NEAR_PLANE_Z;

    for (int i = 0; i < 3; i++) {
        const Vec3i current = input[i];
        const int current_inside = current.z >= NEAR_PLANE_Z;

        if (current_inside) {
            if (!previous_inside) {
                output[output_count++] = intersect_near_plane(&previous, &current);
            }

            output[output_count++] = current;
        } else if (previous_inside) {
            output[output_count++] = intersect_near_plane(&previous, &current);
        }

        previous = current;
        previous_inside = current_inside;
    }

    return output_count;
}

static void project_camera_vertex(const Vec3i *camera, ProjectedVertex *projected) {
    int z = camera->z;

    if (z < NEAR_PLANE_Z) {
        z = NEAR_PLANE_Z;
    }

    projected->x = (SCREEN_W / 2) + ((camera->x * FOCAL_LENGTH) / z);
    projected->y = (SCREEN_H / 2) - ((camera->y * FOCAL_LENGTH) / z);
    projected->z = camera->z;
}

static int triangle_depth(const Vec3i *a, const Vec3i *b, const Vec3i *c) {
    return (a->z + b->z + c->z) / 3;
}

static void draw_projected_triangle(
    RenderContext *context,
    const ProjectedVertex *a,
    const ProjectedVertex *b,
    const ProjectedVertex *c,
    uint8_t r,
    uint8_t g,
    uint8_t b_col,
    int ot_z
) {
    POLY_F3 *poly = (POLY_F3 *)new_primitive(context, ot_z, sizeof(POLY_F3));

    setPolyF3(poly);
    setRGB0(poly, r, g, b_col);

    setXY3(
        poly,
        a->x, a->y,
        b->x, b->y,
        c->x, c->y
    );
}

static void draw_camera_triangle_clipped(
    RenderContext *context,
    const Vec3i *a,
    const Vec3i *b,
    const Vec3i *c,
    uint8_t r,
    uint8_t g,
    uint8_t b_col
) {
    Vec3i input[3];
    Vec3i clipped[4];

    input[0] = *a;
    input[1] = *b;
    input[2] = *c;

    const int count = clip_triangle_to_near_plane(input, clipped);

    if (count < 3) {
        return;
    }

    ProjectedVertex p0;
    ProjectedVertex p1;
    ProjectedVertex p2;
    ProjectedVertex p3;

    project_camera_vertex(&(clipped[0]), &p0);
    project_camera_vertex(&(clipped[1]), &p1);
    project_camera_vertex(&(clipped[2]), &p2);

    int depth = triangle_depth(&(clipped[0]), &(clipped[1]), &(clipped[2]));
    int ot_z = depth_to_ot(depth);

    uint8_t fogged_r = r;
    uint8_t fogged_g = g;
    uint8_t fogged_b = b_col;

    apply_distance_fog(depth, &fogged_r, &fogged_g, &fogged_b);

    draw_projected_triangle(
        context,
        &p0,
        &p1,
        &p2,
        fogged_r,
        fogged_g,
        fogged_b,
        ot_z
    );

    if (count == 4) {
        project_camera_vertex(&(clipped[3]), &p3);

        depth = triangle_depth(&(clipped[0]), &(clipped[2]), &(clipped[3]));
        ot_z = depth_to_ot(depth);

        fogged_r = r;
        fogged_g = g;
        fogged_b = b_col;

        apply_distance_fog(depth, &fogged_r, &fogged_g, &fogged_b);

        draw_projected_triangle(
            context,
            &p0,
            &p2,
            &p3,
            fogged_r,
            fogged_g,
            fogged_b,
            ot_z
        );
    }
}

static int face_center_z(const MeshFace *face) {
    return (
        camera_vertices[face->v[0]].z +
        camera_vertices[face->v[1]].z +
        camera_vertices[face->v[2]].z +
        camera_vertices[face->v[3]].z
    ) / 4;
}

static int face_is_outside_frustum(const MeshFace *face) {
    int all_behind = 1;
    int all_too_far = 1;

    int outside_left = 1;
    int outside_right = 1;
    int outside_top = 1;
    int outside_bottom = 1;

    for (int i = 0; i < 4; i++) {
        const Vec3i *v = &(camera_vertices[face->v[i]]);
        int z = v->z;

        if (z >= NEAR_PLANE_Z) {
            all_behind = 0;
        }

        if (z <= FAR_PLANE_Z) {
            all_too_far = 0;
        }

        if (z < NEAR_PLANE_Z) {
            z = NEAR_PLANE_Z;
        }

        const int max_x = (((SCREEN_W / 2) + FRUSTUM_MARGIN_X) * z) / FOCAL_LENGTH;
        const int max_y = (((SCREEN_H / 2) + FRUSTUM_MARGIN_Y) * z) / FOCAL_LENGTH;

        if (v->x >= -max_x) {
            outside_left = 0;
        }

        if (v->x <= max_x) {
            outside_right = 0;
        }

        if (v->y >= -max_y) {
            outside_top = 0;
        }

        if (v->y <= max_y) {
            outside_bottom = 0;
        }
    }

    return (
        all_behind ||
        all_too_far ||
        outside_left ||
        outside_right ||
        outside_top ||
        outside_bottom
    );
}


static int face_is_safe_for_textured_quad(
    const Vec3i *v0,
    const Vec3i *v1,
    const Vec3i *v2,
    const Vec3i *v3
) {
    /*
     * Keep log texture visible when the player stands right next to the block.
     * project_camera_vertex() already clamps projection to NEAR_PLANE_Z, so we
     * only need to reject faces that are almost on/behind the camera.
     */
    return (
        v0->z > TEXTURED_LOG_MIN_Z &&
        v1->z > TEXTURED_LOG_MIN_Z &&
        v2->z > TEXTURED_LOG_MIN_Z &&
        v3->z > TEXTURED_LOG_MIN_Z
    );
}

static int log_face_texture_u(int face_type) {
    if (face_type == FACE_POS_Y || face_type == FACE_NEG_Y) {
        return LOG_TOP_U;
    }

    return LOG_SIDE_U;
}

static void draw_projected_textured_triangle(
    RenderContext *context,
    const ProjectedVertex *a,
    const ProjectedVertex *b,
    const ProjectedVertex *c,
    int u0,
    int v0,
    int u1,
    int v1,
    int u2,
    int v2,
    int ot_z
) {
    POLY_FT3 *poly = (POLY_FT3 *)new_primitive(context, ot_z, sizeof(POLY_FT3));

    setPolyFT3(poly);
    setRGB0(poly, 128, 128, 128);

    setXY3(
        poly,
        a->x, a->y,
        b->x, b->y,
        c->x, c->y
    );

    setUV3(
        poly,
        u0, v0,
        u1, v1,
        u2, v2
    );

    setTPage(poly, 2, 0, TEXTURE_ATLAS_X, TEXTURE_ATLAS_Y);
    setClut(poly, 0, 0);
}

static void draw_camera_textured_log_quad(
    RenderContext *context,
    const Vec3i *v0,
    const Vec3i *v1,
    const Vec3i *v2,
    const Vec3i *v3,
    int face_type
) {
    ProjectedVertex p0;
    ProjectedVertex p1;
    ProjectedVertex p2;
    ProjectedVertex p3;

    const int tex_u0 = log_face_texture_u(face_type);
    const int tex_v0 = LOG_TILE_V;
    const int tex_u1 = tex_u0 + LOG_TILE_SIZE - 1;
    const int tex_v1 = tex_v0 + LOG_TILE_SIZE - 1;

    int depth_a;
    int depth_b;
    int ot_a;
    int ot_b;

    if (!face_is_safe_for_textured_quad(v0, v1, v2, v3)) {
        return;
    }

    project_camera_vertex(v0, &p0);
    project_camera_vertex(v1, &p1);
    project_camera_vertex(v2, &p2);
    project_camera_vertex(v3, &p3);

    /*
     * Do not render the whole face as one large textured quad primitive.
     * A single large quad is sorted by one average depth, which caused nearby
     * world triangles to overwrite parts of the log face.
     *
     * Two textured triangle primitives are still very cheap on PS1 and sort
     * much better.
     */
    depth_a = (v0->z + v1->z + v2->z) / 3;
    depth_b = (v0->z + v2->z + v3->z) / 3;

    ot_a = depth_to_ot(depth_a);
    ot_b = depth_to_ot(depth_b);

    draw_projected_textured_triangle(
        context,
        &p0,
        &p1,
        &p2,
        tex_u0, tex_v0,
        tex_u1, tex_v0,
        tex_u1, tex_v1,
        ot_a
    );

    draw_projected_textured_triangle(
        context,
        &p0,
        &p2,
        &p3,
        tex_u0, tex_v0,
        tex_u1, tex_v1,
        tex_u0, tex_v1,
        ot_b
    );
}

static void draw_mesh(RenderContext *context) {
    for (int i = 0; i < mesh_face_count; i++) {
        const MeshFace *face = &(mesh_faces[i]);

        if (face_center_z(face) <= NEAR_PLANE_Z) {
            continue;
        }

        if (face_is_outside_frustum(face)) {
            continue;
        }

        const Vec3i *v0 = &(camera_vertices[face->v[0]]);
        const Vec3i *v1 = &(camera_vertices[face->v[1]]);
        const Vec3i *v2 = &(camera_vertices[face->v[2]]);
        const Vec3i *v3 = &(camera_vertices[face->v[3]]);

        if (
            face->block_type == BLOCK_LOG &&
            face_is_safe_for_textured_quad(v0, v1, v2, v3)
        ) {
            draw_camera_textured_log_quad(
                context,
                v0,
                v1,
                v2,
                v3,
                face->face_type
            );
        } else {
            draw_camera_triangle_clipped(context, v0, v1, v2, face->r, face->g, face->b);
            draw_camera_triangle_clipped(context, v0, v2, v3, face->r, face->g, face->b);
        }
    }
}

static void draw_texture_speckles(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int z,
    int seed,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    for (int i = 0; i < 5; i++) {
        const int px = x + 2 + ((seed * 11 + i * 17) % (w - 4));
        const int py = y + 2 + ((seed * 7 + i * 13) % (h - 4));
        const int size = 2 + ((seed + i) & 1);

        draw_filled_rect(context, px, py, size, size, z, r, g, b);
    }
}

static void draw_minecraft_texture_block(
    RenderContext *context,
    int x,
    int y,
    int size,
    int type,
    int z,
    int seed
) {
    const int band = size / 4;

    switch (type) {
        case 0:
            draw_filled_rect(context, x, y, size, size, z + 1, 115, 72, 38);
            draw_filled_rect(context, x, y, size, band, z, 80, 175, 64);
            draw_filled_rect(context, x, y + band, size, 2, z, 70, 135, 48);
            draw_texture_speckles(context, x, y + band + 1, size, size - band - 1, z, seed, 82, 50, 28);
            draw_texture_speckles(context, x, y, size, band + 2, z, seed + 9, 118, 210, 82);
            break;

        case 1:
            draw_filled_rect(context, x, y, size, size, z + 1, 116, 72, 38);
            draw_texture_speckles(context, x, y, size, size, z, seed, 82, 50, 28);
            draw_texture_speckles(context, x, y, size, size, z, seed + 3, 145, 92, 48);
            break;

        case 2:
            draw_filled_rect(context, x, y, size, size, z + 1, 104, 106, 104);
            draw_texture_speckles(context, x, y, size, size, z, seed, 72, 74, 74);
            draw_texture_speckles(context, x, y, size, size, z, seed + 5, 138, 140, 138);
            break;

        case 3:
            draw_filled_rect(context, x, y, size, size, z + 1, 48, 126, 44);
            draw_texture_speckles(context, x, y, size, size, z, seed, 32, 92, 34);
            draw_texture_speckles(context, x, y, size, size, z, seed + 8, 82, 166, 64);
            break;

        case 4:
            draw_filled_rect(context, x, y, size, size, z + 1, 100, 68, 36);
            draw_filled_rect(context, x + 3, y, 3, size, z, 70, 44, 24);
            draw_filled_rect(context, x + size - 5, y, 2, size, z, 130, 88, 48);
            draw_texture_speckles(context, x, y, size, size, z, seed, 74, 46, 24);
            break;

        case 5:
            draw_filled_rect(context, x, y, size, size, z + 1, 42, 112, 190);
            draw_filled_rect(context, x, y + 3, size, 2, z, 70, 152, 220);
            draw_filled_rect(context, x, y + 9, size, 2, z, 34, 88, 160);
            break;

        case 6:
            draw_filled_rect(context, x, y, size, size, z + 1, 202, 184, 106);
            draw_texture_speckles(context, x, y, size, size, z, seed, 164, 142, 78);
            draw_texture_speckles(context, x, y, size, size, z, seed + 4, 232, 216, 136);
            break;

        case 7:
            draw_filled_rect(context, x, y, size, size, z + 1, 166, 108, 52);
            draw_filled_rect(context, x, y + (size / 3), size, 1, z, 104, 64, 32);
            draw_filled_rect(context, x, y + ((size * 2) / 3), size, 1, z, 104, 64, 32);
            draw_filled_rect(context, x + (size / 2), y, 1, size, z, 196, 142, 76);
            draw_texture_speckles(context, x, y, size, size, z, seed, 118, 76, 38);
            break;

        case 8:
        default:
            draw_filled_rect(context, x, y, size, size, z + 1, 132, 78, 36);
            draw_filled_rect(context, x + 2, y + 2, size - 4, size - 4, z, 168, 112, 54);
            draw_filled_rect(context, x + 4, y + 4, size - 8, size - 8, z, 96, 58, 28);
            draw_filled_rect(context, x + 5, y + 5, size - 10, size - 10, z, 188, 136, 68);
            draw_line(context, x + 3, y + (size / 2), x + size - 4, y + (size / 2), z, 78, 48, 24);
            draw_line(context, x + (size / 2), y + 3, x + (size / 2), y + size - 4, z, 78, 48, 24);
            break;
    }

    draw_line(context, x, y, x + size - 1, y, z, 230, 230, 230);
    draw_line(context, x, y, x, y + size - 1, z, 218, 218, 218);
    draw_line(context, x + size - 1, y, x + size - 1, y + size - 1, z, 34, 34, 34);
    draw_line(context, x, y + size - 1, x + size - 1, y + size - 1, z, 34, 34, 34);
}

static int project_world_position(
    int world_x,
    int world_y,
    int world_z,
    ProjectedVertex *projected
) {
    const int sin_y = isin(-camera_yaw);
    const int cos_y = icos(-camera_yaw);

    const int sin_x = isin(camera_pitch);
    const int cos_x = icos(camera_pitch);

    const int rel_x = world_x - camera_pos_x;
    const int rel_y = world_y - camera_pos_y;
    const int rel_z = world_z - camera_pos_z;

    const int x1 = ((rel_x * cos_y) + (rel_z * sin_y)) / FIXED_ONE;
    const int z1 = ((-rel_x * sin_y) + (rel_z * cos_y)) / FIXED_ONE;

    const int y2 = ((rel_y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
    const int z2 = ((rel_y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

    Vec3i camera_point;

    if (z2 < NEAR_PLANE_Z || z2 > FAR_PLANE_Z) {
        return 0;
    }

    camera_point.x = x1;
    camera_point.y = y2;
    camera_point.z = z2;

    project_camera_vertex(&camera_point, projected);

    if (
        projected->x < -12 ||
        projected->x > SCREEN_W + 12 ||
        projected->y < -12 ||
        projected->y > SCREEN_H + 12
    ) {
        return 0;
    }

    return 1;
}

static int dropped_item_texture_type(uint8_t block_type) {
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

static void draw_dropped_item_icon(
    RenderContext *context,
    int x,
    int y,
    int size,
    int ot_z,
    uint8_t block_type,
    int seed
) {
    const int texture_type = dropped_item_texture_type(block_type);

    if (texture_type < 0) {
        return;
    }

    /*
     * Use the item's real OT depth, not HUD depth.
     * This lets world polygons in front of the item cover it.
     */
    draw_filled_rect(context, x - 1, y - 1, size + 2, size + 2, ot_z + 1, 18, 18, 18);
    draw_minecraft_texture_block(context, x, y, size, texture_type, ot_z, seed);

    /*
     * Extra tiny type markers make 8-12px drops more recognizable on PS1.
     */
    if (block_type == BLOCK_STONE) {
        draw_line(context, x + 2, y + 3, x + size - 3, y + 3, ot_z, 190, 190, 190);
        draw_line(context, x + 3, y + size - 4, x + size - 4, y + size - 4, ot_z, 70, 70, 70);
    } else if (block_type == BLOCK_SAND) {
        draw_filled_rect(context, x + 2, y + 2, 3, 3, ot_z, 240, 226, 142);
        draw_filled_rect(context, x + size - 5, y + size - 5, 3, 3, ot_z, 156, 134, 72);
    } else if (block_type == BLOCK_DIRT) {
        draw_filled_rect(context, x + 2, y + size - 5, size - 4, 2, ot_z, 74, 44, 24);
    } else if (block_type == BLOCK_GRASS) {
        draw_filled_rect(context, x + 2, y + 2, size - 4, 2, ot_z, 96, 214, 72);
    } else if (block_type == BLOCK_LOG) {
        draw_filled_rect(context, x + 2, y + 1, 3, size - 2, ot_z, 70, 44, 24);
        draw_filled_rect(context, x + size - 4, y + 1, 2, size - 2, ot_z, 130, 88, 48);
    } else if (block_type == BLOCK_PLANKS) {
        draw_line(context, x + 1, y + (size / 3), x + size - 2, y + (size / 3), ot_z, 96, 58, 30);
        draw_line(context, x + 1, y + ((size * 2) / 3), x + size - 2, y + ((size * 2) / 3), ot_z, 96, 58, 30);
    } else if (block_type == BLOCK_WORKBENCH) {
        draw_line(context, x + 2, y + 2, x + size - 3, y + size - 3, ot_z, 64, 38, 18);
        draw_line(context, x + size - 3, y + 2, x + 2, y + size - 3, ot_z, 64, 38, 18);
    }
}

static void draw_dropped_items(RenderContext *context) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            continue;
        }

        ProjectedVertex projected;
        const int bob = ((dropped_items[i].bob_frame >> 3) & 1) ? 3 : 0;
        int ot_z;
        int size;

        if (!project_world_position(
            dropped_items[i].x,
            dropped_items[i].y + bob,
            dropped_items[i].z,
            &projected
        )) {
            continue;
        }

        ot_z = depth_to_ot(projected.z);

        /*
         * Approximate perspective scaling for readability.
         * Close items are a little larger; far items remain small.
         */
        size = 14 - (projected.z / 90);

        if (size < 7) {
            size = 7;
        }

        if (size > 12) {
            size = 12;
        }

        draw_dropped_item_icon(
            context,
            projected.x - (size / 2),
            projected.y - (size / 2),
            size,
            ot_z,
            dropped_items[i].type,
            900 + i
        );
    }
}

static void draw_menu_cloud(RenderContext *context, int x, int y, int z) {
    draw_filled_rect(context, x, y + 8, 54, 10, z, 244, 248, 255);
    draw_filled_rect(context, x + 12, y, 18, 20, z, 244, 248, 255);
    draw_filled_rect(context, x + 28, y + 4, 24, 16, z, 244, 248, 255);
    draw_filled_rect(context, x + 50, y + 10, 18, 8, z, 244, 248, 255);
}

static void draw_menu_tree(RenderContext *context, int x, int y, int block, int z) {
    draw_minecraft_texture_block(context, x + block, y + block, block, 4, z, x + y + 1);
    draw_minecraft_texture_block(context, x + block, y + block * 2, block, 4, z, x + y + 2);

    draw_minecraft_texture_block(context, x, y, block, 3, z, x + y + 3);
    draw_minecraft_texture_block(context, x + block, y, block, 3, z, x + y + 4);
    draw_minecraft_texture_block(context, x + block * 2, y, block, 3, z, x + y + 5);
    draw_minecraft_texture_block(context, x, y + block, block, 3, z, x + y + 6);
    draw_minecraft_texture_block(context, x + block, y + block, block, 3, z, x + y + 7);
    draw_minecraft_texture_block(context, x + block * 2, y + block, block, 3, z, x + y + 8);
}

static void draw_minecraft_button(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int selected
) {
    if (selected) {
        draw_filled_rect(context, x + 2, y + 2, w, h, 4, 18, 18, 18);
        draw_filled_rect(context, x, y, w, h, 3, 160, 164, 160);
        draw_line(context, x, y, x + w - 1, y, 2, 252, 252, 252);
        draw_line(context, x, y, x, y + h - 1, 2, 252, 252, 252);
        draw_line(context, x + w - 1, y, x + w - 1, y + h - 1, 2, 64, 64, 64);
        draw_line(context, x, y + h - 1, x + w - 1, y + h - 1, 2, 64, 64, 64);
        draw_filled_rect(context, x + 4, y + 4, w - 8, h - 8, 2, 126, 132, 126);
    } else {
        draw_filled_rect(context, x + 2, y + 2, w, h, 4, 14, 14, 14);
        draw_filled_rect(context, x, y, w, h, 3, 106, 108, 106);
        draw_line(context, x, y, x + w - 1, y, 2, 206, 206, 206);
        draw_line(context, x, y, x, y + h - 1, 2, 206, 206, 206);
        draw_line(context, x + w - 1, y, x + w - 1, y + h - 1, 2, 42, 42, 42);
        draw_line(context, x, y + h - 1, x + w - 1, y + h - 1, 2, 42, 42, 42);
        draw_filled_rect(context, x + 4, y + 4, w - 8, h - 8, 2, 86, 88, 86);
    }
}

static void draw_menu_background(RenderContext *context) {
    const int block = 16;

    draw_filled_rect(context, 0, 0, SCREEN_W, 58, 9, 86, 152, 224);
    draw_filled_rect(context, 0, 58, SCREEN_W, 46, 9, 108, 176, 236);
    draw_filled_rect(context, 0, 104, SCREEN_W, 32, 9, 130, 194, 242);

    draw_filled_rect(context, 248, 18, 32, 32, 8, 248, 226, 112);
    draw_filled_rect(context, 252, 22, 24, 24, 7, 255, 242, 160);

    draw_menu_cloud(context, 18, 52, 7);
    draw_menu_cloud(context, 202, 62, 7);

    for (int col = 0; col < 20; col++) {
        const int x = col * block;
        int top = 132;

        if (col == 3 || col == 4 || col == 5) {
            top = 116;
        } else if (col == 6 || col == 7 || col == 8 || col == 9) {
            top = 124;
        } else if (col == 14 || col == 15 || col == 16) {
            top = 148;
        } else if (col >= 17) {
            top = 140;
        }

        if (col >= 11 && col <= 13) {
            draw_minecraft_texture_block(context, x, 148, block, 6, 6, col + 1);
            draw_minecraft_texture_block(context, x, 164, block, 5, 6, col + 2);
            draw_minecraft_texture_block(context, x, 180, block, 5, 6, col + 3);
            draw_minecraft_texture_block(context, x, 196, block, 1, 6, col + 4);
            draw_minecraft_texture_block(context, x, 212, block, 1, 6, col + 5);
            continue;
        }

        draw_minecraft_texture_block(context, x, top, block, 0, 6, col + 3);

        for (int y = top + block; y < SCREEN_H; y += block) {
            if (y >= top + block * 3 && (col == 1 || col == 2 || col == 18)) {
                draw_minecraft_texture_block(context, x, y, block, 2, 6, col + y);
            } else {
                draw_minecraft_texture_block(context, x, y, block, 1, 6, col + y);
            }
        }
    }

    draw_menu_tree(context, 38, 84, block, 5);
    draw_menu_tree(context, 226, 100, block, 5);

    draw_minecraft_texture_block(context, 152, 112, block, 2, 5, 31);
    draw_minecraft_texture_block(context, 168, 112, block, 2, 5, 32);
    draw_minecraft_texture_block(context, 152, 128, block, 2, 5, 33);
    draw_minecraft_texture_block(context, 168, 128, block, 2, 5, 34);
}
static void draw_menu_option(
    RenderContext *context,
    int x,
    int y,
    int w,
    const char *label,
    int selected
) {
    draw_minecraft_button(context, x, y, w, 24, selected);

    if (selected) {
        draw_text(context, x + 18, y + 7, 0, "> ");
        draw_text(context, x + 36, y + 7, 0, label);
    } else {
        draw_text(context, x + 36, y + 7, 0, label);
    }
}
static void draw_pause_option(
    RenderContext *context,
    int x,
    int y,
    int w,
    const char *label,
    int selected
) {
    draw_minecraft_button(context, x, y, w, 19, selected);

    if (selected) {
        draw_text(context, x + 14, y + 5, 0, "> ");
        draw_text(context, x + 30, y + 5, 0, label);
    } else {
        draw_text(context, x + 30, y + 5, 0, label);
    }
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
    const ItemStack *stack = &(hotbar_slot_blocks[slot_index]);
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
        draw_hotbar_slot(context, i, i == selected_hotbar_slot);
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
            i < player_health_hearts
        );
    }
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

static void move_inventory_cursor(int dx, int dy) {
    if (inventory_cursor_slot < INVENTORY_STORAGE_SLOT_COUNT) {
        int row = inventory_cursor_slot / INVENTORY_STORAGE_COLS;
        int col = inventory_cursor_slot % INVENTORY_STORAGE_COLS;

        if (dy < 0 && row == 0) {
            if (col >= 6 && col <= 7) {
                inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + (1 * 2) + (col - 6);
                return;
            }

            if (col >= 8) {
                inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_OUTPUT;
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
            inventory_cursor_slot = (row * INVENTORY_STORAGE_COLS) + col;
        } else {
            inventory_cursor_slot = INVENTORY_CURSOR_HOTBAR_START + col;
        }

        return;
    }

    if (inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_START) {
        int col = inventory_cursor_slot - INVENTORY_CURSOR_HOTBAR_START;

        col += dx;

        if (col < 0) {
            col = INVENTORY_STORAGE_COLS - 1;
        }

        if (col >= INVENTORY_STORAGE_COLS) {
            col = 0;
        }

        if (dy < 0) {
            inventory_cursor_slot = ((INVENTORY_STORAGE_ROWS - 1) * INVENTORY_STORAGE_COLS) + col;
        } else if (dy > 0) {
            inventory_cursor_slot = col;
        } else {
            inventory_cursor_slot = INVENTORY_CURSOR_HOTBAR_START + col;
        }

        return;
    }

    if (inventory_cursor_slot < INVENTORY_CURSOR_CRAFT_OUTPUT) {
        int local = inventory_cursor_slot - INVENTORY_CURSOR_CRAFT_START;
        int row = local / 2;
        int col = local % 2;

        if (dx > 0 && col == 1) {
            inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_OUTPUT;
            return;
        }

        if (dy > 0 && row == 1) {
            inventory_cursor_slot = col + 6;
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

        inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + (row * 2) + col;
        return;
    }

    /*
     * Crafting output slot.
     */
    if (dx < 0) {
        inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + 3;
        return;
    }

    if (dy > 0) {
        inventory_cursor_slot = 8;
        return;
    }

    if (dy < 0) {
        inventory_cursor_slot = INVENTORY_CURSOR_CRAFT_START + 1;
        return;
    }
}

static void merge_or_swap_inventory_held_with_slot(ItemStack *slot) {
    if (stack_is_empty(&inventory_held_stack)) {
        inventory_held_stack = *slot;
        slot->type = BLOCK_AIR;
        slot->count = 0;

        normalize_stack(&inventory_held_stack);
        normalize_stack(slot);
        set_system_status(stack_is_empty(&inventory_held_stack) ? "HAND EMPTY" : "ITEM HELD", 45);
        return;
    }

    if (stack_is_empty(slot)) {
        *slot = inventory_held_stack;
        inventory_held_stack.type = BLOCK_AIR;
        inventory_held_stack.count = 0;

        normalize_stack(slot);
        normalize_stack(&inventory_held_stack);
        set_system_status("ITEM PLACED", 45);
        return;
    }

    if (slot->type == inventory_held_stack.type && slot->count < STACK_MAX_COUNT) {
        const int space = STACK_MAX_COUNT - slot->count;
        const int move_count = inventory_held_stack.count < space ? inventory_held_stack.count : space;

        slot->count = (uint8_t)(slot->count + move_count);
        inventory_held_stack.count = (uint8_t)(inventory_held_stack.count - move_count);

        normalize_stack(slot);
        normalize_stack(&inventory_held_stack);
        set_system_status(stack_is_empty(&inventory_held_stack) ? "STACK MERGED" : "STACK PARTIAL", 45);
        return;
    }

    {
        const ItemStack previous = *slot;

        *slot = inventory_held_stack;
        inventory_held_stack = previous;

        normalize_stack(slot);
        normalize_stack(&inventory_held_stack);
        set_system_status("ITEM SWAPPED", 45);
    }
}

static void swap_inventory_held_with_cursor(void) {
    ItemStack *slot;

    if (inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
        take_crafting_output();
        return;
    }

    slot = get_inventory_cursor_stack_ptr();
    merge_or_swap_inventory_held_with_slot(slot);
}

static void quick_move_inventory_slot_to_hotbar(void) {
    ItemStack *slot = get_inventory_cursor_stack_ptr();
    ItemStack *target = &(hotbar_slot_blocks[selected_hotbar_slot]);
    const ItemStack previous = *target;

    if (inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
        return;
    }

    *target = *slot;
    *slot = previous;

    normalize_stack(slot);
    normalize_stack(target);

    set_system_status("MOVED TO HOTBAR", 55);
}

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

static int can_add_stack_to_hand(const ItemStack *stack) {
    if (stack_is_empty(stack)) {
        return 0;
    }

    if (stack_is_empty(&inventory_held_stack)) {
        return 1;
    }

    if (inventory_held_stack.type != stack->type) {
        return 0;
    }

    return (inventory_held_stack.count + stack->count) <= STACK_MAX_COUNT;
}

static void add_stack_to_hand(const ItemStack *stack) {
    if (stack_is_empty(&inventory_held_stack)) {
        inventory_held_stack = *stack;
        normalize_stack(&inventory_held_stack);
        return;
    }

    if (inventory_held_stack.type == stack->type) {
        inventory_held_stack.count = (uint8_t)(inventory_held_stack.count + stack->count);
        normalize_stack(&inventory_held_stack);
    }
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
            inventory_storage_blocks[i].type,
            inventory_storage_blocks[i].count,
            inventory_cursor_slot == i,
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
            hotbar_slot_blocks[i].type,
            hotbar_slot_blocks[i].count,
            inventory_cursor_slot == cursor_index,
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
        crafting_slots[0].type,
        crafting_slots[0].count,
        inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START,
        0,
        600
    );
    draw_inventory_slot(
        context,
        210,
        48,
        crafting_slots[1].type,
        crafting_slots[1].count,
        inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 1,
        0,
        601
    );
    draw_inventory_slot(
        context,
        190,
        68,
        crafting_slots[2].type,
        crafting_slots[2].count,
        inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 2,
        0,
        602
    );
    draw_inventory_slot(
        context,
        210,
        68,
        crafting_slots[3].type,
        crafting_slots[3].count,
        inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_START + 3,
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
        inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT,
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

    if (!stack_is_empty(&inventory_held_stack)) {
        const int texture_type = block_type_to_icon_texture(inventory_held_stack.type);

        draw_text(context, 214, 182, 0, "HAND");
        draw_inventory_slot(
            context,
            256,
            178,
            inventory_held_stack.type,
            inventory_held_stack.count,
            1,
            0,
            800 + texture_type
        );
    }

    if (system_status_timer > 0) {
        draw_minecraft_button(context, 104, 224, 112, 16, 0);
        draw_text(context, 122, 226, 0, system_status_text);
    }
}

static ItemStack *get_workbench_cursor_stack_ptr(void) {
    if (workbench_cursor_slot < WORKBENCH_CURSOR_OUTPUT) {
        return &(workbench_crafting_slots[workbench_cursor_slot]);
    }

    if (workbench_cursor_slot < WORKBENCH_CURSOR_HOTBAR_START) {
        return &(inventory_storage_blocks[workbench_cursor_slot - WORKBENCH_CURSOR_STORAGE_START]);
    }

    return &(hotbar_slot_blocks[workbench_cursor_slot - WORKBENCH_CURSOR_HOTBAR_START]);
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

    if (hud_visible) {
        draw_panel(context, 6, 8, 148, 64, 2, 32, 36, 42, 174, 174, 174);
        draw_text(context, 16, 18, 0, "MINECRAFT PS1");
        draw_text(context, 16, 34, 0, fly_mode_enabled ? "MODE: FLY" : "MODE: WALK");
        draw_text(context, 16, 50, 0, autojump_enabled ? "SQUARE HOLD BREAK" : "AUTOJUMP OFF");
    }

    if (system_status_timer > 0) {
        draw_minecraft_button(context, 8, hud_visible ? 80 : 8, 112, 18, 0);
        draw_text(context, 18, hud_visible ? 84 : 12, 0, system_status_text);
    }
}
static void reset_inventory_items(void) {
    for (int i = 0; i < HOTBAR_SLOT_COUNT; i++) {
        hotbar_slot_blocks[i].type = BLOCK_AIR;
        hotbar_slot_blocks[i].count = 0;
    }

    hotbar_slot_blocks[0].type = BLOCK_DIRT;
    hotbar_slot_blocks[0].count = STACK_MAX_COUNT;

    hotbar_slot_blocks[1].type = BLOCK_STONE;
    hotbar_slot_blocks[1].count = STACK_MAX_COUNT;

    hotbar_slot_blocks[2].type = BLOCK_SAND;
    hotbar_slot_blocks[2].count = STACK_MAX_COUNT;

    hotbar_slot_blocks[3].type = ITEM_WOOD_PICKAXE;
    hotbar_slot_blocks[3].count = 1;

    for (int i = 0; i < INVENTORY_STORAGE_SLOT_COUNT; i++) {
        inventory_storage_blocks[i].type = BLOCK_AIR;
        inventory_storage_blocks[i].count = 0;
    }

    inventory_storage_blocks[0].type = BLOCK_LOG;
    inventory_storage_blocks[0].count = 16;

    for (int i = 0; i < CRAFT_SLOT_COUNT; i++) {
        crafting_slots[i].type = BLOCK_AIR;
        crafting_slots[i].count = 0;
    }

    for (int i = 0; i < WORKBENCH_CRAFT_SLOT_COUNT; i++) {
        workbench_crafting_slots[i].type = BLOCK_AIR;
        workbench_crafting_slots[i].count = 0;
    }

    inventory_held_stack.type = BLOCK_AIR;
    inventory_held_stack.count = 0;
    inventory_cursor_slot = INVENTORY_CURSOR_STORAGE_START;
    selected_hotbar_slot = 0;
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

static void update_menu_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if (pressed_this_frame & PAD_UP) {
        menu_selected_option--;

        if (menu_selected_option < 0) {
            menu_selected_option = MENU_OPTION_COUNT - 1;
        }
    }

    if (pressed_this_frame & PAD_DOWN) {
        menu_selected_option++;

        if (menu_selected_option >= MENU_OPTION_COUNT) {
            menu_selected_option = 0;
        }
    }

    if (pressed_this_frame & PAD_LEFT) {
        menu_selected_option = MENU_OPTION_NEW_GAME;
    }

    if (pressed_this_frame & PAD_RIGHT) {
        menu_selected_option = MENU_OPTION_LOAD_GAME;
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE) ||
        (pressed_this_frame & PAD_START)
    ) {
        if (menu_selected_option == MENU_OPTION_NEW_GAME) {
            start_new_game();
        } else {
            start_loaded_game();
        }
    }

    pad_previous_buttons = buttons;
}

static void update_pause_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        app_state = APP_STATE_PLAY;
        pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_UP) {
        pause_selected_option--;

        if (pause_selected_option < 0) {
            pause_selected_option = PAUSE_OPTION_COUNT - 1;
        }
    }

    if (pressed_this_frame & PAD_DOWN) {
        pause_selected_option++;

        if (pause_selected_option >= PAUSE_OPTION_COUNT) {
            pause_selected_option = 0;
        }
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE)
    ) {
        switch (pause_selected_option) {
            case PAUSE_OPTION_RESUME:
                app_state = APP_STATE_PLAY;
                break;

            case PAUSE_OPTION_INVENTORY:
                app_state = APP_STATE_INVENTORY;
                inventory_cursor_slot = INVENTORY_CURSOR_STORAGE_START;
                set_system_status("INVENTORY", 45);
                break;

            case PAUSE_OPTION_TOGGLE_FLY:
                fly_mode_enabled = !fly_mode_enabled;

                if (!fly_mode_enabled) {
                    snap_camera_to_ground();
                    set_system_status("MODE WALK", 90);
                } else {
                    set_system_status("MODE FLY", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_HUD:
                hud_visible = !hud_visible;

                if (hud_visible) {
                    set_system_status("HELP HUD ON", 90);
                } else {
                    set_system_status("HELP HUD OFF", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_FOG:
                fog_enabled = !fog_enabled;
                update_clear_color_for_game();

                if (fog_enabled) {
                    set_system_status("FOG ON", 90);
                } else {
                    set_system_status("FOG OFF", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_AUTOJUMP:
                autojump_enabled = !autojump_enabled;

                if (autojump_enabled) {
                    set_system_status("AUTOJUMP ON", 90);
                } else {
                    set_system_status("AUTOJUMP OFF", 90);
                }
                break;

            case PAUSE_OPTION_SAVE_GAME:
                if (save_game_to_memory_card()) {
                    set_system_status("SAVE OK", 90);
                } else {
                    set_system_status("SAVE FAILED", 120);
                }
                break;

            case PAUSE_OPTION_LOAD_GAME:
                if (load_game_from_memory_card()) {
                    reset_inventory_items();
                    reset_dropped_items();
                    reset_block_breaking();
                    app_state = APP_STATE_PLAY;
                    set_system_status("LOAD OK", 90);
                } else {
                    set_system_status("LOAD FAILED", 120);
                }
                break;

            case PAUSE_OPTION_RETURN_MENU:
            default:
                app_state = APP_STATE_MENU;
                menu_selected_option = MENU_OPTION_NEW_GAME;
                pause_selected_option = PAUSE_OPTION_RESUME;
                set_system_status("MAIN MENU", 90);
                break;
        }
    }

    pad_previous_buttons = buttons;
}
static void update_inventory_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        app_state = APP_STATE_PLAY;
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
        if ((buttons & PAD_R2) && inventory_cursor_slot == INVENTORY_CURSOR_CRAFT_OUTPUT) {
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

static void draw_menu(RenderContext *context) {
    draw_menu_background(context);

    draw_text(context, 93, 22, 1, "MINECRAFT PS1");
    draw_text(context, 91, 20, 0, "MINECRAFT PS1");
    draw_text(context, 72, 38, 0, "PLAYSTATION STYLE DEMAKE");

    draw_menu_option(
        context,
        90,
        82,
        140,
        "NEW GAME",
        menu_selected_option == MENU_OPTION_NEW_GAME
    );
    draw_menu_option(
        context,
        90,
        112,
        140,
        "LOAD GAME",
        menu_selected_option == MENU_OPTION_LOAD_GAME
    );

    draw_minecraft_button(context, 32, 206, 256, 20, 0);
    draw_text(context, 50, 211, 0, "UP/DOWN CHOOSE  CROSS/CIRCLE/START OK");

    if (system_status_timer > 0) {
        draw_minecraft_button(context, 102, 178, 116, 18, 0);
        draw_text(context, 120, 182, 0, system_status_text);
    }
}
static void draw_pause_menu(RenderContext *context) {
    draw_filled_rect(context, 0, 0, SCREEN_W, SCREEN_H, 7, 18, 18, 22);

    draw_minecraft_texture_block(context, 24, 20, 16, 2, 6, 100);
    draw_minecraft_texture_block(context, 40, 20, 16, 2, 6, 101);
    draw_minecraft_texture_block(context, 56, 20, 16, 2, 6, 102);
    draw_minecraft_texture_block(context, 248, 20, 16, 1, 6, 103);
    draw_minecraft_texture_block(context, 264, 20, 16, 0, 6, 104);
    draw_minecraft_texture_block(context, 280, 20, 16, 0, 6, 105);

    draw_text(context, 122, 20, 0, "GAME MENU");

    draw_pause_option(
        context,
        74,
        32,
        172,
        "BACK TO GAME",
        pause_selected_option == PAUSE_OPTION_RESUME
    );
    draw_pause_option(
        context,
        74,
        52,
        172,
        "INVENTORY",
        pause_selected_option == PAUSE_OPTION_INVENTORY
    );
    draw_pause_option(
        context,
        74,
        72,
        172,
        fly_mode_enabled ? "SWITCH TO WALK" : "SWITCH TO FLY",
        pause_selected_option == PAUSE_OPTION_TOGGLE_FLY
    );
    draw_pause_option(
        context,
        74,
        92,
        172,
        hud_visible ? "HELP HUD: ON" : "HELP HUD: OFF",
        pause_selected_option == PAUSE_OPTION_TOGGLE_HUD
    );
    draw_pause_option(
        context,
        74,
        112,
        172,
        fog_enabled ? "FOG: ON" : "FOG: OFF",
        pause_selected_option == PAUSE_OPTION_TOGGLE_FOG
    );
    draw_pause_option(
        context,
        74,
        132,
        172,
        autojump_enabled ? "AUTOJUMP: ON" : "AUTOJUMP: OFF",
        pause_selected_option == PAUSE_OPTION_TOGGLE_AUTOJUMP
    );
    draw_pause_option(
        context,
        74,
        152,
        172,
        "SAVE GAME",
        pause_selected_option == PAUSE_OPTION_SAVE_GAME
    );
    draw_pause_option(
        context,
        74,
        172,
        172,
        "LOAD GAME",
        pause_selected_option == PAUSE_OPTION_LOAD_GAME
    );
    draw_pause_option(
        context,
        74,
        192,
        172,
        "MAIN MENU",
        pause_selected_option == PAUSE_OPTION_RETURN_MENU
    );

    draw_text(context, 70, 216, 0, "UP/DOWN CHOOSE  CROSS/CIRCLE OK");
    draw_text(context, 86, 228, 0, "START/TRIANGLE BACK");

    if (system_status_timer > 0) {
        draw_minecraft_button(context, 104, 4, 112, 18, 0);
        draw_text(context, 122, 8, 0, system_status_text);
    }
}
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    ResetGraph(0);
    FntLoad(960, 0);

    /*
     * Sky-ish background.
     */
    setup_context(&ctx, SCREEN_W, SCREEN_H, SKY_R, SKY_G, SKY_B);
    upload_texture_assets();
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
