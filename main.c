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

#define MAX_MESH_VERTICES 512
#define MAX_MESH_FACES 256

#define ANGLE_FULL 4096
#define ANGLE_MASK (ANGLE_FULL - 1)
#define ANGLE_FRAC_BITS 6
#define ANGLE_QUARTER 1024

#define CAMERA_TILT_X (-448)
#define ROTATION_SPEED 8

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
    uint8_t indices[4];

    uint8_t r;
    uint8_t g;
    uint8_t b;
} BlockFace;

static RenderContext ctx;

static Vec3i mesh_vertices[MAX_MESH_VERTICES];
static Vec3i camera_vertices[MAX_MESH_VERTICES];
static ProjectedVertex projected_vertices[MAX_MESH_VERTICES];

static MeshFace mesh_faces[MAX_MESH_FACES];

static int mesh_vertex_count = 0;
static int mesh_face_count = 0;

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
    { { 0, 3, 2, 1 }, 118, 76, 38 },
    { { 4, 5, 6, 7 }, 95, 60, 32 },
    { { 0, 4, 7, 3 }, 105, 68, 35 },
    { { 1, 2, 6, 5 }, 130, 84, 42 },
    { { 0, 1, 5, 4 }, 55, 38, 24 },
    { { 3, 7, 6, 2 }, 70, 185, 70 }
};

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

static int add_mesh_vertex(Vec3i vertex) {
    for (int i = 0; i < mesh_vertex_count; i++) {
        if (
            mesh_vertices[i].x == vertex.x &&
            mesh_vertices[i].y == vertex.y &&
            mesh_vertices[i].z == vertex.z
        ) {
            return i;
        }
    }

    if (mesh_vertex_count >= MAX_MESH_VERTICES) {
        return 0;
    }

    mesh_vertices[mesh_vertex_count] = vertex;
    mesh_vertex_count++;

    return mesh_vertex_count - 1;
}

static void push_mesh_face(int block_x, int block_y, int block_z, int face_index) {
    if (mesh_face_count >= MAX_MESH_FACES) {
        return;
    }

    const BlockFace *block_face = &(block_faces[face_index]);
    MeshFace *mesh_face = &(mesh_faces[mesh_face_count]);

    for (int i = 0; i < 4; i++) {
        const Vec3i vertex = get_block_vertex(
            block_x,
            block_y,
            block_z,
            block_face->indices[i]
        );

        mesh_face->v[i] = (uint16_t)add_mesh_vertex(vertex);
    }

    mesh_face->r = block_face->r;
    mesh_face->g = block_face->g;
    mesh_face->b = block_face->b;

    mesh_face_count++;
}

static void build_mesh(void) {
    mesh_vertex_count = 0;
    mesh_face_count = 0;

    for (int z = 0; z < CHUNK_D; z++) {
        for (int x = 0; x < CHUNK_W; x++) {
            const int height = get_height(x, z);

            if (height <= 0) {
                continue;
            }

            for (int y = 0; y < height; y++) {
                if (y == height - 1) {
                    push_mesh_face(x, y, z, FACE_POS_Y);
                }

                if (y == 0) {
                    push_mesh_face(x, y, z, FACE_NEG_Y);
                }

                if (get_height(x, z - 1) <= y) {
                    push_mesh_face(x, y, z, FACE_NEG_Z);
                }

                if (get_height(x, z + 1) <= y) {
                    push_mesh_face(x, y, z, FACE_POS_Z);
                }

                if (get_height(x - 1, z) <= y) {
                    push_mesh_face(x, y, z, FACE_NEG_X);
                }

                if (get_height(x + 1, z) <= y) {
                    push_mesh_face(x, y, z, FACE_POS_X);
                }
            }
        }
    }
}

static void transform_all_vertices(int angle_y, int angle_x) {
    const int sin_y = isin(angle_y);
    const int cos_y = icos(angle_y);

    const int sin_x = isin(angle_x);
    const int cos_x = icos(angle_x);

    for (int i = 0; i < mesh_vertex_count; i++) {
        const Vec3i *world = &(mesh_vertices[i]);

        const int x1 = ((world->x * cos_y) + (world->z * sin_y)) / FIXED_ONE;
        const int z1 = ((-world->x * sin_y) + (world->z * cos_y)) / FIXED_ONE;

        const int y2 = ((world->y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
        const int z2 = ((world->y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

        const int camera_z = z2 + CAMERA_Z;

        camera_vertices[i].x = x1;
        camera_vertices[i].y = y2;
        camera_vertices[i].z = camera_z;

        projected_vertices[i].x = (SCREEN_W / 2) + ((x1 * FOCAL_LENGTH) / camera_z);
        projected_vertices[i].y = (SCREEN_H / 2) - ((y2 * FOCAL_LENGTH) / camera_z);
        projected_vertices[i].z = camera_z;
    }
}

static int face_normal_z(const MeshFace *face) {
    const Vec3i *a = &(camera_vertices[face->v[0]]);
    const Vec3i *b = &(camera_vertices[face->v[1]]);
    const Vec3i *c = &(camera_vertices[face->v[2]]);

    const int ux = b->x - a->x;
    const int uy = b->y - a->y;

    const int vx = c->x - a->x;
    const int vy = c->y - a->y;

    return (ux * vy) - (uy * vx);
}

static int face_depth(const MeshFace *face) {
    return (
        camera_vertices[face->v[0]].z +
        camera_vertices[face->v[1]].z +
        camera_vertices[face->v[2]].z +
        camera_vertices[face->v[3]].z
    ) / 4;
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

static void draw_triangle(
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

static void draw_mesh(RenderContext *context) {
    for (int i = 0; i < mesh_face_count; i++) {
        const MeshFace *face = &(mesh_faces[i]);

        if (
            camera_vertices[face->v[0]].z <= 32 ||
            camera_vertices[face->v[1]].z <= 32 ||
            camera_vertices[face->v[2]].z <= 32 ||
            camera_vertices[face->v[3]].z <= 32
        ) {
            continue;
        }

        /*
         * Camera looks toward +Z.
         * Visible faces have normals pointing toward -Z.
         */
        if (face_normal_z(face) >= 0) {
            continue;
        }

        const int ot_z = depth_to_ot(face_depth(face));

        const ProjectedVertex *v0 = &(projected_vertices[face->v[0]]);
        const ProjectedVertex *v1 = &(projected_vertices[face->v[1]]);
        const ProjectedVertex *v2 = &(projected_vertices[face->v[2]]);
        const ProjectedVertex *v3 = &(projected_vertices[face->v[3]]);

        draw_triangle(context, v0, v1, v2, face->r, face->g, face->b, ot_z);
        draw_triangle(context, v0, v2, v3, face->r, face->g, face->b, ot_z);
    }
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    ResetGraph(0);
    FntLoad(960, 0);

    setup_context(&ctx, SCREEN_W, SCREEN_H, 8, 12, 18);

    build_mesh();

    int angle_y = 0;

    for (;;) {
        transform_all_vertices(angle_y, CAMERA_TILT_X);
        draw_mesh(&ctx);

        draw_text(&ctx, 8, 16, 0, "MINECRAFT PS1");
        draw_text(&ctx, 8, 32, 0, "smooth mesh renderer");

        angle_y = (angle_y + ROTATION_SPEED) & ANGLE_MASK;

        flip_buffers(&ctx);
    }

    return 0;
}