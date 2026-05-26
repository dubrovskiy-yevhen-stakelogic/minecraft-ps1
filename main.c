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
#define GRID_VERTICES_PER_SIDE (VIEW_SIZE + 1)

#define MAX_COLUMN_HEIGHT 6
#define GRID_Y_LINES (MAX_COLUMN_HEIGHT + 1)

#define FOCAL_LENGTH 220
#define NEAR_PLANE_Z 24
#define FAR_PLANE_Z 900
#define FRUSTUM_MARGIN_X 96
#define FRUSTUM_MARGIN_Y 72

#define MAX_MESH_VERTICES (GRID_VERTICES_PER_SIDE * GRID_VERTICES_PER_SIDE * GRID_Y_LINES)
#define MAX_MESH_FACES 1536

#define MAX_COLUMN_EDITS 128
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
    int tile_x;
    int tile_z;
    int height;
    uint8_t used;
} ColumnEdit;

typedef struct {
    int found;
    int tile_x;
    int tile_z;
    int level;
} RaycastHit;

static RenderContext ctx;

static Vec3i mesh_vertices[MAX_MESH_VERTICES];
static Vec3i camera_vertices[MAX_MESH_VERTICES];

static MeshFace mesh_faces[MAX_MESH_FACES];

static int mesh_vertex_count = 0;
static int mesh_face_count = 0;

static int vertex_lookup[GRID_Y_LINES][GRID_VERTICES_PER_SIDE][GRID_VERTICES_PER_SIDE];

static int mesh_center_tile_x = 999999;
static int mesh_center_tile_z = 999999;
static int mesh_dirty = 1;

static ColumnEdit column_edits[MAX_COLUMN_EDITS];
static int column_edit_count = 0;

static uint8_t pad_buffers[2][PAD_BUFFER_SIZE];

static int camera_pos_x = 0;
static int camera_pos_y = PLAYER_EYE_HEIGHT;
static int camera_pos_z = 0;

static int camera_yaw = 0;
static int camera_pitch = DEFAULT_CAMERA_PITCH;

/*
 * 0 = walk on infinite flat world.
 * 1 = free fly/debug camera.
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

static int y_line_to_world_y(int y_line) {
    return -BLOCK_SIZE + (y_line * BLOCK_SIZE);
}

static int get_floor_y_for_height(int height) {
    if (height <= 0) {
        return -100000;
    }

    return y_line_to_world_y(height);
}

static int grid_vertex_index(int x, int z) {
    return (z * GRID_VERTICES_PER_SIDE) + x;
}

static int get_column_edit_index(int tile_x, int tile_z) {
    /*
     * Important performance fix:
     *
     * Old version scanned all MAX_COLUMN_EDITS slots even when there were
     * zero edits. Mesh rebuild calls get_column_height() many times per tile,
     * so this caused a huge CPU spike when walking into a new tile.
     *
     * Now we scan only active edits.
     */
    for (int i = 0; i < column_edit_count; i++) {
        if (
            column_edits[i].tile_x == tile_x &&
            column_edits[i].tile_z == tile_z
        ) {
            return i;
        }
    }

    return -1;
}

static int get_column_height(int tile_x, int tile_z) {
    const int edit_index = get_column_edit_index(tile_x, tile_z);

    if (edit_index >= 0) {
        return column_edits[edit_index].height;
    }

    /*
     * Infinite default flat world.
     */
    return 1;
}

static void remove_column_edit_at_index(int index) {
    if (index < 0 || index >= column_edit_count) {
        return;
    }

    /*
     * Compact array by moving last edit into removed slot.
     * Order is irrelevant and this keeps lookup O(active_edits).
     */
    column_edit_count--;

    if (index != column_edit_count) {
        column_edits[index] = column_edits[column_edit_count];
    }

    column_edits[column_edit_count].used = 0;
}

static void set_column_height(int tile_x, int tile_z, int height) {
    height = clamp_int(height, 0, MAX_COLUMN_HEIGHT);

    const int edit_index = get_column_edit_index(tile_x, tile_z);

    /*
     * Default height is 1. If user returns column to default, remove edit.
     */
    if (height == 1) {
        if (edit_index >= 0) {
            remove_column_edit_at_index(edit_index);
        }

        mesh_dirty = 1;
        return;
    }

    if (edit_index >= 0) {
        column_edits[edit_index].height = height;
        mesh_dirty = 1;
        return;
    }

    if (column_edit_count >= MAX_COLUMN_EDITS) {
        return;
    }

    column_edits[column_edit_count].used = 1;
    column_edits[column_edit_count].tile_x = tile_x;
    column_edits[column_edit_count].tile_z = tile_z;
    column_edits[column_edit_count].height = height;
    column_edit_count++;

    mesh_dirty = 1;
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

static void snap_camera_to_ground(void) {
    const int tile_x = world_to_tile(camera_pos_x);
    const int tile_z = world_to_tile(camera_pos_z);
    const int height = get_column_height(tile_x, tile_z);

    if (height <= 0) {
        return;
    }

    camera_pos_y = get_floor_y_for_height(height) + PLAYER_EYE_HEIGHT;
}

static void reset_camera(void) {
    fly_mode_enabled = 0;

    camera_pos_x = 0;
    camera_pos_y = PLAYER_EYE_HEIGHT;
    camera_pos_z = 0;

    camera_yaw = 0;
    camera_pitch = DEFAULT_CAMERA_PITCH;

    mesh_center_tile_x = 999999;
    mesh_center_tile_z = 999999;
    mesh_dirty = 1;
}

static int is_walk_target_valid(int next_x, int next_z) {
    const int current_height = get_column_height(
        world_to_tile(camera_pos_x),
        world_to_tile(camera_pos_z)
    );

    const int next_height = get_column_height(
        world_to_tile(next_x),
        world_to_tile(next_z)
    );

    if (next_height <= 0) {
        return 0;
    }

    /*
     * Simple Minecraft-like step rule:
     * can walk up/down by one block, but not through tall walls.
     */
    if (iabs(next_height - current_height) > 1) {
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

static int y_to_block_level(int y) {
    return floor_div(y + BLOCK_SIZE, BLOCK_SIZE);
}

static RaycastHit raycast_block(void) {
    RaycastHit hit;
    Vec3i dir;

    hit.found = 0;
    hit.tile_x = 0;
    hit.tile_z = 0;
    hit.level = 0;

    get_camera_forward_direction(&dir);

    for (int distance = 8; distance <= RAYCAST_MAX_DISTANCE; distance += RAYCAST_STEP) {
        const int sample_x = camera_pos_x + ((dir.x * distance) / FIXED_ONE);
        const int sample_y = camera_pos_y + ((dir.y * distance) / FIXED_ONE);
        const int sample_z = camera_pos_z + ((dir.z * distance) / FIXED_ONE);

        const int tile_x = world_to_tile(sample_x);
        const int tile_z = world_to_tile(sample_z);
        const int height = get_column_height(tile_x, tile_z);
        const int level = y_to_block_level(sample_y);

        if (height <= 0) {
            continue;
        }

        if (level >= 0 && level < height) {
            hit.found = 1;
            hit.tile_x = tile_x;
            hit.tile_z = tile_z;
            hit.level = level;
            return hit;
        }
    }

    return hit;
}

static void remove_target_block(void) {
    const RaycastHit hit = raycast_block();

    if (!hit.found) {
        return;
    }

    const int height = get_column_height(hit.tile_x, hit.tile_z);

    if (height <= 0) {
        return;
    }

    /*
     * Height-column edit for now: remove top block from selected column.
     */
    set_column_height(hit.tile_x, hit.tile_z, height - 1);

    if (!fly_mode_enabled) {
        const int current_tile_x = world_to_tile(camera_pos_x);
        const int current_tile_z = world_to_tile(camera_pos_z);

        if (current_tile_x == hit.tile_x && current_tile_z == hit.tile_z) {
            if (get_column_height(hit.tile_x, hit.tile_z) <= 0) {
                set_column_height(hit.tile_x, hit.tile_z, 1);
            }

            snap_camera_to_ground();
        }
    }
}

static void add_target_block(void) {
    const RaycastHit hit = raycast_block();

    if (!hit.found) {
        return;
    }

    const int height = get_column_height(hit.tile_x, hit.tile_z);

    if (height >= MAX_COLUMN_HEIGHT) {
        return;
    }

    /*
     * Height-column edit for now: add block on top of selected column.
     */
    set_column_height(hit.tile_x, hit.tile_z, height + 1);
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

static int add_grid_vertex(
    int local_x_line,
    int local_z_line,
    int y_line,
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
    vertex->y = y_line_to_world_y(y_line);
    vertex->z = start_world_z + (local_z_line * BLOCK_SIZE);

    *lookup_entry = mesh_vertex_count;
    mesh_vertex_count++;

    return *lookup_entry;
}

static void push_face_by_grid_vertices(
    int x0,
    int z0,
    int y0,
    int x1,
    int z1,
    int y1,
    int x2,
    int z2,
    int y2,
    int x3,
    int z3,
    int y3,
    int start_world_x,
    int start_world_z,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    if (mesh_face_count >= MAX_MESH_FACES) {
        return;
    }

    MeshFace *face = &(mesh_faces[mesh_face_count]);

    face->v[0] = (uint16_t)add_grid_vertex(x0, z0, y0, start_world_x, start_world_z);
    face->v[1] = (uint16_t)add_grid_vertex(x1, z1, y1, start_world_x, start_world_z);
    face->v[2] = (uint16_t)add_grid_vertex(x2, z2, y2, start_world_x, start_world_z);
    face->v[3] = (uint16_t)add_grid_vertex(x3, z3, y3, start_world_x, start_world_z);

    face->r = r;
    face->g = g;
    face->b = b;

    mesh_face_count++;
}

static void push_column_faces(
    int local_x,
    int local_z,
    int world_tile_x,
    int world_tile_z,
    int start_world_x,
    int start_world_z
) {
    const int height = get_column_height(world_tile_x, world_tile_z);

    if (height <= 0) {
        return;
    }

    const int x0 = local_x;
    const int x1 = local_x + 1;
    const int z0 = local_z;
    const int z1 = local_z + 1;

    /*
     * Top face.
     */
    if (((world_tile_x ^ world_tile_z) & 1) == 0) {
        push_face_by_grid_vertices(
            x0, z0, height,
            x1, z0, height,
            x1, z1, height,
            x0, z1, height,
            start_world_x,
            start_world_z,
            64,
            175,
            64
        );
    } else {
        push_face_by_grid_vertices(
            x0, z0, height,
            x1, z0, height,
            x1, z1, height,
            x0, z1, height,
            start_world_x,
            start_world_z,
            54,
            155,
            54
        );
    }

    /*
     * Side faces where neighbor is lower.
     * Sides are drawn as one vertical quad from neighbor height to column top.
     */
    const int neighbor_neg_z = get_column_height(world_tile_x, world_tile_z - 1);
    const int neighbor_pos_z = get_column_height(world_tile_x, world_tile_z + 1);
    const int neighbor_neg_x = get_column_height(world_tile_x - 1, world_tile_z);
    const int neighbor_pos_x = get_column_height(world_tile_x + 1, world_tile_z);

    if (neighbor_neg_z < height) {
        push_face_by_grid_vertices(
            x0, z0, neighbor_neg_z,
            x0, z0, height,
            x1, z0, height,
            x1, z0, neighbor_neg_z,
            start_world_x,
            start_world_z,
            118,
            76,
            38
        );
    }

    if (neighbor_pos_z < height) {
        push_face_by_grid_vertices(
            x1, z1, neighbor_pos_z,
            x1, z1, height,
            x0, z1, height,
            x0, z1, neighbor_pos_z,
            start_world_x,
            start_world_z,
            95,
            60,
            32
        );
    }

    if (neighbor_neg_x < height) {
        push_face_by_grid_vertices(
            x0, z1, neighbor_neg_x,
            x0, z1, height,
            x0, z0, height,
            x0, z0, neighbor_neg_x,
            start_world_x,
            start_world_z,
            105,
            68,
            35
        );
    }

    if (neighbor_pos_x < height) {
        push_face_by_grid_vertices(
            x1, z0, neighbor_pos_x,
            x1, z0, height,
            x1, z1, height,
            x1, z1, neighbor_pos_x,
            start_world_x,
            start_world_z,
            130,
            84,
            42
        );
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

    for (int z = 0; z < VIEW_SIZE; z++) {
        for (int x = 0; x < VIEW_SIZE; x++) {
            push_column_faces(
                x,
                z,
                start_tile_x + x,
                start_tile_z + z,
                start_world_x,
                start_world_z
            );
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

        /*
         * Screen projection:
         * screen_x = center + x * FOCAL_LENGTH / z
         *
         * So a rough camera-space visible range is:
         * abs(x) <= (half_screen + margin) * z / FOCAL_LENGTH
         *
         * This is intentionally generous to avoid popping.
         */
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

        /*
         * Important optimization:
         * Do cheap face-level visibility tests before near clipping.
         *
         * This fixes two problems:
         * 1. FPS loss: we no longer clip/project hundreds of invisible faces.
         * 2. Behind-camera artifacts: faces behind the player no longer get
         *    clipped onto the near plane and rendered as garbage.
         */
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
        draw_text(&ctx, 8, 32, 0, "SQUARE REMOVE  CIRCLE ADD");
        draw_text(&ctx, 8, 48, 0, "VISIBILITY CULLING");
        draw_text(&ctx, 8, 64, 0, "FAST SPARSE EDITS");

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
