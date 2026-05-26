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
#define FAR_PLANE_Z 900
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

#define PAD_BUFFER_SIZE 34

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
} MeshFace;

typedef struct {
    int x;
    int y;
    int z;
    uint8_t type;
} BlockEdit;

typedef struct {
    int found;
    int hit_x;
    int hit_y;
    int hit_z;
    int place_x;
    int place_y;
    int place_z;
} RaycastHit;

enum {
    BLOCK_AIR = 0,
    BLOCK_DIRT = 1,
    BLOCK_GRASS = 2
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

static void draw_crosshair(RenderContext *context) {
    const int cx = SCREEN_W / 2;
    const int cy = SCREEN_H / 2;

    draw_line(context, cx - 5, cy, cx - 2, cy, 0, 235, 235, 235);
    draw_line(context, cx + 2, cy, cx + 5, cy, 0, 235, 235, 235);
    draw_line(context, cx, cy - 5, cx, cy - 2, 0, 235, 235, 235);
    draw_line(context, cx, cy + 2, cx, cy + 5, 0, 235, 235, 235);
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
         * Key rule:
         *
         * Do not auto-step onto higher blocks. A block next to the player is
         * treated as a wall until we add a real jump/climb mechanic.
         *
         * This prevents the camera from sliding into side blocks and then
         * seeing through them when turning.
         */
        if (sample_top_block_y > target_top_block_y) {
            return 0;
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
     * No automatic climb for now.
     *
     * The old rule allowed +1 block auto-step. That felt convenient, but it
     * also meant a placed block could be treated like a step instead of a wall,
     * letting the camera get too close to its side faces.
     */
    if (next_top_block_y > current_top_block_y) {
        return 0;
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

static RaycastHit raycast_block(void) {
    RaycastHit hit;
    Vec3i dir;

    hit.found = 0;
    hit.hit_x = 0;
    hit.hit_y = 0;
    hit.hit_z = 0;
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
                    hit.place_x = last_empty_x;
                    hit.place_y = last_empty_y;
                    hit.place_z = last_empty_z;
                } else {
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

static void remove_target_block(void) {
    const RaycastHit hit = raycast_block();

    if (!hit.found) {
        return;
    }

    set_block_type(hit.hit_x, hit.hit_y, hit.hit_z, BLOCK_AIR);

    if (!fly_mode_enabled) {
        snap_camera_to_ground();
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

static void add_target_block(void) {
    const RaycastHit hit = raycast_block();

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

    /*
     * Placed block type for now.
     * Later we can switch this to current hotbar/material.
     */
    set_block_type(hit.place_x, hit.place_y, hit.place_z, BLOCK_DIRT);
}

static void update_input(void) {
    static uint16_t previous_buttons = 0;

    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~previous_buttons;

    const int forward_x = isin(camera_yaw);
    const int forward_z = icos(camera_yaw);

    const int right_x = icos(camera_yaw);
    const int right_z = -isin(camera_yaw);

    if (pressed_this_frame & PAD_SELECT) {
        reset_camera();
    }

    if (pressed_this_frame & PAD_START) {
        fly_mode_enabled = !fly_mode_enabled;

        if (!fly_mode_enabled) {
            snap_camera_to_ground();
        }
    }

    if (pressed_this_frame & PAD_SQUARE) {
        remove_target_block();
    }

    if (pressed_this_frame & PAD_CIRCLE) {
        add_target_block();
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

    previous_buttons = buttons;
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
    uint8_t b
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
                b_col
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
                b_col
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
                b_col
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
                b_col
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
                b_col
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
                b_col
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

    draw_projected_triangle(
        context,
        &p0,
        &p1,
        &p2,
        r,
        g,
        b_col,
        ot_z
    );

    if (count == 4) {
        project_camera_vertex(&(clipped[3]), &p3);

        depth = triangle_depth(&(clipped[0]), &(clipped[2]), &(clipped[3]));
        ot_z = depth_to_ot(depth);

        draw_projected_triangle(
            context,
            &p0,
            &p2,
            &p3,
            r,
            g,
            b_col,
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

        draw_camera_triangle_clipped(context, v0, v1, v2, face->r, face->g, face->b);
        draw_camera_triangle_clipped(context, v0, v2, v3, face->r, face->g, face->b);
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
    setup_context(&ctx, SCREEN_W, SCREEN_H, 48, 80, 130);
    init_input();

    reset_camera();

    for (;;) {
        update_input();

        rebuild_mesh_if_needed();
        transform_all_vertices();
        draw_mesh(&ctx);
        draw_crosshair(&ctx);

        draw_text(&ctx, 8, 16, 0, "MINECRAFT PS1");
        draw_text(&ctx, 8, 32, 0, "TRUE 3D VOXEL STORAGE");
        draw_text(&ctx, 8, 48, 0, "SQUARE REMOVE  CIRCLE ADD");
        draw_text(&ctx, 8, 64, 0, "BODY COLLISION FIX");

        if (fly_mode_enabled) {
            draw_text(&ctx, 8, 80, 0, "MODE: FLY  START WALK");
            draw_text(&ctx, 8, 96, 0, "DPAD MOVE/TURN L1/R1 STRAFE");
            draw_text(&ctx, 8, 112, 0, "TRI/CROSS PITCH L2/R2 HEIGHT");
        } else {
            draw_text(&ctx, 8, 80, 0, "MODE: WALK  START FLY");
            draw_text(&ctx, 8, 96, 0, "DPAD WALK/TURN L1/R1 STRAFE");
            draw_text(&ctx, 8, 112, 0, "TRI/CROSS PITCH SELECT RESET");
        }

        flip_buffers(&ctx);
    }

    return 0;
}
