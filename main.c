#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <psxgpu.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define OT_LENGTH 256
#define BUFFER_LENGTH 65536

#define FIXED_ONE 1024

#define BLOCK_HALF 24
#define BLOCK_SIZE (BLOCK_HALF * 2)

#define CHUNK_W 7
#define CHUNK_D 7

#define CAMERA_Z 680
#define FOCAL_LENGTH 220

#define MAX_WORLD_FACES 256
#define MAX_RENDER_FACES 256

#define CAMERA_TILT_X (-7)

/*
 * Bigger value = slower rotation and less CPU work.
 */
#define GEOMETRY_UPDATE_INTERVAL 6

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
    Vec3i vertices[4];

    uint8_t r;
    uint8_t g;
    uint8_t b;
} WorldFace;

typedef struct {
    ProjectedVertex vertices[4];
    int depth;

    uint8_t r;
    uint8_t g;
    uint8_t b;
} RenderFace;

typedef struct {
    uint8_t indices[4];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} BlockFace;

static WorldFace world_faces[MAX_WORLD_FACES];
static int world_face_count = 0;

static RenderFace cached_render_faces[MAX_RENDER_FACES];
static int cached_render_face_count = 0;

/*
 * 64-step sine table.
 * Scale: 1024 = 1.0
 *
 * cos(angle) = sin(angle + 16)
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

/*
 * height_map[z][x]
 */
static const uint8_t height_map[CHUNK_D][CHUNK_W] = {
    { 0, 0, 1, 1, 1, 0, 0 },
    { 0, 1, 1, 2, 1, 1, 0 },
    { 1, 1, 2, 3, 2, 1, 1 },
    { 1, 2, 3, 4, 3, 2, 1 },
    { 1, 1, 2, 3, 2, 1, 1 },
    { 0, 1, 1, 2, 1, 1, 0 },
    { 0, 0, 1, 1, 1, 0, 0 }
};

enum {
    FACE_NEG_Z = 0,
    FACE_POS_Z = 1,
    FACE_NEG_X = 2,
    FACE_POS_X = 3,
    FACE_NEG_Y = 4,
    FACE_POS_Y = 5
};

static const Vec3i block_local_vertices[8] = {
    { -BLOCK_HALF, -BLOCK_HALF, -BLOCK_HALF },
    {  BLOCK_HALF, -BLOCK_HALF, -BLOCK_HALF },
    {  BLOCK_HALF,  BLOCK_HALF, -BLOCK_HALF },
    { -BLOCK_HALF,  BLOCK_HALF, -BLOCK_HALF },

    { -BLOCK_HALF, -BLOCK_HALF,  BLOCK_HALF },
    {  BLOCK_HALF, -BLOCK_HALF,  BLOCK_HALF },
    {  BLOCK_HALF,  BLOCK_HALF,  BLOCK_HALF },
    { -BLOCK_HALF,  BLOCK_HALF,  BLOCK_HALF }
};

static const BlockFace block_faces[6] = {
    /* -Z side */
    { { 0, 3, 2, 1 }, 118, 76, 38 },

    /* +Z side */
    { { 4, 5, 6, 7 }, 95, 60, 32 },

    /* -X side */
    { { 0, 4, 7, 3 }, 105, 68, 35 },

    /* +X side */
    { { 1, 2, 6, 5 }, 130, 84, 42 },

    /* -Y bottom */
    { { 0, 1, 5, 4 }, 55, 38, 24 },

    /* +Y top */
    { { 3, 7, 6, 2 }, 70, 185, 70 }
};

static int isin(int angle) {
    return sin_table[angle & 63];
}

static int icos(int angle) {
    return sin_table[(angle + 16) & 63];
}

static void setup_context(RenderContext *ctx, int w, int h, int r, int g, int b) {
    SetDefDrawEnv(&(ctx->buffers[0].draw_env), 0, 0, w, h);
    SetDefDispEnv(&(ctx->buffers[0].disp_env), 0, 0, w, h);

    SetDefDrawEnv(&(ctx->buffers[1].draw_env), 0, h, w, h);
    SetDefDispEnv(&(ctx->buffers[1].disp_env), 0, h, w, h);

    setRGB0(&(ctx->buffers[0].draw_env), r, g, b);
    setRGB0(&(ctx->buffers[1].draw_env), r, g, b);

    ctx->buffers[0].draw_env.isbg = 1;
    ctx->buffers[1].draw_env.isbg = 1;

    ctx->active_buffer = 0;
    ctx->next_packet = ctx->buffers[0].buffer;

    ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);

    SetDispMask(1);
}

static void flip_buffers(RenderContext *ctx) {
    DrawSync(0);
    VSync(0);

    RenderBuffer *draw_buffer = &(ctx->buffers[ctx->active_buffer]);
    RenderBuffer *disp_buffer = &(ctx->buffers[ctx->active_buffer ^ 1]);

    PutDispEnv(&(disp_buffer->disp_env));
    DrawOTagEnv(&(draw_buffer->ot[OT_LENGTH - 1]), &(draw_buffer->draw_env));

    ctx->active_buffer ^= 1;
    ctx->next_packet = disp_buffer->buffer;

    ClearOTagR(disp_buffer->ot, OT_LENGTH);
}

static void *new_primitive(RenderContext *ctx, int z, size_t size) {
    if (z < 0) {
        z = 0;
    }

    if (z >= OT_LENGTH) {
        z = OT_LENGTH - 1;
    }

    RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);
    uint8_t *prim = ctx->next_packet;

    addPrim(&(buffer->ot[z]), prim);

    ctx->next_packet += size;
    assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));

    return (void *)prim;
}

static void draw_text(RenderContext *ctx, int x, int y, int z, const char *text) {
    RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);

    ctx->next_packet = (uint8_t *)FntSort(
        &(buffer->ot[z]),
        ctx->next_packet,
        x,
        y,
        text
    );

    assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

static int get_height(int x, int z) {
    if (x < 0 || x >= CHUNK_W || z < 0 || z >= CHUNK_D) {
        return 0;
    }

    return height_map[z][x];
}

static Vec3i get_block_vertex(int block_x, int block_y, int block_z, uint8_t vertex_index) {
    const int origin_x = -((CHUNK_W - 1) * BLOCK_SIZE) / 2;
    const int origin_z = -((CHUNK_D - 1) * BLOCK_SIZE) / 2;

    Vec3i result;

    result.x = origin_x + (block_x * BLOCK_SIZE) + block_local_vertices[vertex_index].x;
    result.y = -95 + (block_y * BLOCK_SIZE) + block_local_vertices[vertex_index].y;
    result.z = origin_z + (block_z * BLOCK_SIZE) + block_local_vertices[vertex_index].z;

    return result;
}

static void push_world_face(int block_x, int block_y, int block_z, int face_index) {
    if (world_face_count >= MAX_WORLD_FACES) {
        return;
    }

    const BlockFace *face = &(block_faces[face_index]);
    WorldFace *world_face = &(world_faces[world_face_count]);

    for (int i = 0; i < 4; i++) {
        world_face->vertices[i] = get_block_vertex(
            block_x,
            block_y,
            block_z,
            face->indices[i]
        );
    }

    world_face->r = face->r;
    world_face->g = face->g;
    world_face->b = face->b;

    world_face_count++;
}

/*
 * Build static island geometry once.
 * This removes the expensive height_map traversal from every frame.
 */
static void build_world_faces(void) {
    world_face_count = 0;

    for (int z = 0; z < CHUNK_D; z++) {
        for (int x = 0; x < CHUNK_W; x++) {
            const int height = get_height(x, z);

            if (height <= 0) {
                continue;
            }

            for (int y = 0; y < height; y++) {
                if (y == height - 1) {
                    push_world_face(x, y, z, FACE_POS_Y);
                }

                if (y == 0) {
                    push_world_face(x, y, z, FACE_NEG_Y);
                }

                if (get_height(x, z - 1) <= y) {
                    push_world_face(x, y, z, FACE_NEG_Z);
                }

                if (get_height(x, z + 1) <= y) {
                    push_world_face(x, y, z, FACE_POS_Z);
                }

                if (get_height(x - 1, z) <= y) {
                    push_world_face(x, y, z, FACE_NEG_X);
                }

                if (get_height(x + 1, z) <= y) {
                    push_world_face(x, y, z, FACE_POS_X);
                }
            }
        }
    }
}

static void transform_project_point(
    const Vec3i *world,
    Vec3i *camera,
    ProjectedVertex *projected,
    int angle_y,
    int angle_x
) {
    const int sin_y = isin(angle_y);
    const int cos_y = icos(angle_y);

    const int sin_x = isin(angle_x);
    const int cos_x = icos(angle_x);

    const int x1 = ((world->x * cos_y) + (world->z * sin_y)) / FIXED_ONE;
    const int z1 = ((-world->x * sin_y) + (world->z * cos_y)) / FIXED_ONE;

    const int y2 = ((world->y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
    const int z2 = ((world->y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

    const int camera_z = z2 + CAMERA_Z;

    camera->x = x1;
    camera->y = y2;
    camera->z = camera_z;

    projected->x = (SCREEN_W / 2) + ((x1 * FOCAL_LENGTH) / camera_z);
    projected->y = (SCREEN_H / 2) - ((y2 * FOCAL_LENGTH) / camera_z);
    projected->z = camera_z;
}

static int face_normal_z(const Vec3i camera_vertices[4]) {
    const Vec3i *a = &(camera_vertices[0]);
    const Vec3i *b = &(camera_vertices[1]);
    const Vec3i *c = &(camera_vertices[2]);

    const int ux = b->x - a->x;
    const int uy = b->y - a->y;

    const int vx = c->x - a->x;
    const int vy = c->y - a->y;

    return (ux * vy) - (uy * vx);
}

static int face_depth(const Vec3i camera_vertices[4]) {
    return (
        camera_vertices[0].z +
        camera_vertices[1].z +
        camera_vertices[2].z +
        camera_vertices[3].z
    ) / 4;
}

static void sort_faces_far_to_near(RenderFace faces[MAX_RENDER_FACES], int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (faces[j].depth > faces[i].depth) {
                RenderFace tmp = faces[i];
                faces[i] = faces[j];
                faces[j] = tmp;
            }
        }
    }
}

/*
 * Rebuild projected/sorted render list.
 * This is still expensive, so we do NOT do it every frame.
 */
static void rebuild_render_cache(int angle_y, int angle_x) {
    cached_render_face_count = 0;

    for (int face_index = 0; face_index < world_face_count; face_index++) {
        if (cached_render_face_count >= MAX_RENDER_FACES) {
            break;
        }

        const WorldFace *world_face = &(world_faces[face_index]);

        Vec3i camera_vertices[4];
        ProjectedVertex projected_vertices[4];

        int clipped = 0;

        for (int i = 0; i < 4; i++) {
            transform_project_point(
                &(world_face->vertices[i]),
                &(camera_vertices[i]),
                &(projected_vertices[i]),
                angle_y,
                angle_x
            );

            if (camera_vertices[i].z <= 32) {
                clipped = 1;
            }
        }

        if (clipped) {
            continue;
        }

        if (face_normal_z(camera_vertices) >= 0) {
            continue;
        }

        RenderFace *render_face = &(cached_render_faces[cached_render_face_count]);

        for (int i = 0; i < 4; i++) {
            render_face->vertices[i] = projected_vertices[i];
        }

        render_face->depth = face_depth(camera_vertices);
        render_face->r = world_face->r;
        render_face->g = world_face->g;
        render_face->b = world_face->b;

        cached_render_face_count++;
    }

    sort_faces_far_to_near(cached_render_faces, cached_render_face_count);
}

static void draw_triangle(
    RenderContext *ctx,
    const ProjectedVertex *a,
    const ProjectedVertex *b,
    const ProjectedVertex *c,
    uint8_t r,
    uint8_t g,
    uint8_t b_col,
    int ot_z
) {
    POLY_F3 *poly = (POLY_F3 *)new_primitive(ctx, ot_z, sizeof(POLY_F3));

    setPolyF3(poly);
    setRGB0(poly, r, g, b_col);

    setXY3(
        poly,
        a->x, a->y,
        b->x, b->y,
        c->x, c->y
    );
}

static void draw_render_face(RenderContext *ctx, const RenderFace *face, int ot_z) {
    draw_triangle(
        ctx,
        &(face->vertices[0]),
        &(face->vertices[1]),
        &(face->vertices[2]),
        face->r,
        face->g,
        face->b,
        ot_z
    );

    draw_triangle(
        ctx,
        &(face->vertices[0]),
        &(face->vertices[2]),
        &(face->vertices[3]),
        face->r,
        face->g,
        face->b,
        ot_z
    );
}

static void draw_cached_chunk(RenderContext *ctx) {
    for (int i = 0; i < cached_render_face_count; i++) {
        int ot_z = 1 + (cached_render_face_count - i);

        if (ot_z >= OT_LENGTH) {
            ot_z = OT_LENGTH - 1;
        }

        draw_render_face(ctx, &(cached_render_faces[i]), ot_z);
    }
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    ResetGraph(0);
    FntLoad(960, 0);

    RenderContext ctx;
    setup_context(&ctx, SCREEN_W, SCREEN_H, 8, 12, 18);

    build_world_faces();

    int angle_y = 0;
    int frame_counter = 0;

    rebuild_render_cache(angle_y, CAMERA_TILT_X);

    for (;;) {
        draw_cached_chunk(&ctx);

        draw_text(&ctx, 8, 16, 0, "MINECRAFT PS1");
        draw_text(&ctx, 8, 32, 0, "cached voxel island");

        frame_counter++;

        if (frame_counter >= GEOMETRY_UPDATE_INTERVAL) {
            frame_counter = 0;
            angle_y = (angle_y + 1) & 63;

            rebuild_render_cache(angle_y, CAMERA_TILT_X);
        }

        flip_buffers(&ctx);
    }

    return 0;
}