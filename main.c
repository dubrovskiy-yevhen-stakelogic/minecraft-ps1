#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <psxgpu.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define OT_LENGTH 64
#define BUFFER_LENGTH 16384

#define FIXED_ONE 1024
#define CUBE_SIZE 72
#define CAMERA_Z 360
#define FOCAL_LENGTH 180

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
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;

    uint8_t r;
    uint8_t g;
    uint8_t b_col;
} CubeFace;

typedef struct {
    const CubeFace *face;
    int depth;
} FaceToDraw;

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

static const Vec3i cube_vertices[8] = {
    { -CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE }, /* 0 */
    {  CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE }, /* 1 */
    {  CUBE_SIZE,  CUBE_SIZE, -CUBE_SIZE }, /* 2 */
    { -CUBE_SIZE,  CUBE_SIZE, -CUBE_SIZE }, /* 3 */

    { -CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE }, /* 4 */
    {  CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE }, /* 5 */
    {  CUBE_SIZE,  CUBE_SIZE,  CUBE_SIZE }, /* 6 */
    { -CUBE_SIZE,  CUBE_SIZE,  CUBE_SIZE }  /* 7 */
};

/*
 * Important:
 * Vertex order is consistent and outward-facing.
 *
 * Camera is at Z=0 and looks toward +Z.
 * A face is visible when its normal points toward -Z.
 */
static const CubeFace cube_faces[6] = {
    /* Back / nearest at zero rotation, normal -Z */
    { 0, 3, 2, 1,  55, 190,  75 },

    /* Front / farthest at zero rotation, normal +Z */
    { 4, 5, 6, 7,  35, 120,  55 },

    /* Left, normal -X */
    { 0, 4, 7, 3,  45, 150,  65 },

    /* Right, normal +X */
    { 1, 2, 6, 5,  80, 220, 100 },

    /* Bottom, normal -Y */
    { 0, 1, 5, 4,  35, 100,  50 },

    /* Top, normal +Y */
    { 3, 7, 6, 2, 100, 240, 120 }
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

static void transform_cube(
    Vec3i camera_vertices[8],
    ProjectedVertex projected_vertices[8],
    int angle
) {
    const int sin_y = isin(angle);
    const int cos_y = icos(angle);

    const int sin_x = isin(angle / 2);
    const int cos_x = icos(angle / 2);

    for (int i = 0; i < 8; i++) {
        const int x = cube_vertices[i].x;
        const int y = cube_vertices[i].y;
        const int z = cube_vertices[i].z;

        /*
         * Rotate around Y.
         */
        const int x1 = ((x * cos_y) + (z * sin_y)) / FIXED_ONE;
        const int z1 = ((-x * sin_y) + (z * cos_y)) / FIXED_ONE;

        /*
         * Rotate around X.
         */
        const int y2 = ((y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
        const int z2 = ((y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

        const int camera_z = z2 + CAMERA_Z;

        camera_vertices[i].x = x1;
        camera_vertices[i].y = y2;
        camera_vertices[i].z = camera_z;

        projected_vertices[i].x = (SCREEN_W / 2) + ((x1 * FOCAL_LENGTH) / camera_z);
        projected_vertices[i].y = (SCREEN_H / 2) - ((y2 * FOCAL_LENGTH) / camera_z);
        projected_vertices[i].z = camera_z;
    }
}

static int face_normal_z(const Vec3i vertices[8], const CubeFace *face) {
    const Vec3i *a = &(vertices[face->a]);
    const Vec3i *b = &(vertices[face->b]);
    const Vec3i *c = &(vertices[face->c]);

    const int ux = b->x - a->x;
    const int uy = b->y - a->y;

    const int vx = c->x - a->x;
    const int vy = c->y - a->y;

    /*
     * Z component of cross product:
     * normal.z = ux * vy - uy * vx
     */
    return (ux * vy) - (uy * vx);
}

static int face_depth(const Vec3i vertices[8], const CubeFace *face) {
    return (
        vertices[face->a].z +
        vertices[face->b].z +
        vertices[face->c].z +
        vertices[face->d].z
    ) / 4;
}

static int collect_visible_faces(
    const Vec3i camera_vertices[8],
    FaceToDraw faces_to_draw[6]
) {
    int count = 0;

    for (int i = 0; i < 6; i++) {
        const CubeFace *face = &(cube_faces[i]);

        /*
         * Camera looks toward +Z.
         * Visible faces have normals pointing toward -Z.
         */
        if (face_normal_z(camera_vertices, face) >= 0) {
            continue;
        }

        faces_to_draw[count].face = face;
        faces_to_draw[count].depth = face_depth(camera_vertices, face);
        count++;
    }

    return count;
}

static void sort_faces_far_to_near(FaceToDraw faces[6], int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            /*
             * Larger Z = farther away from camera.
             */
            if (faces[j].depth > faces[i].depth) {
                FaceToDraw tmp = faces[i];
                faces[i] = faces[j];
                faces[j] = tmp;
            }
        }
    }
}

static void draw_triangle(
    RenderContext *ctx,
    const ProjectedVertex vertices[8],
    uint8_t a,
    uint8_t b,
    uint8_t c,
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
        vertices[a].x, vertices[a].y,
        vertices[b].x, vertices[b].y,
        vertices[c].x, vertices[c].y
    );
}

static void draw_face(
    RenderContext *ctx,
    const ProjectedVertex vertices[8],
    const CubeFace *face,
    int ot_z
) {
    /*
     * Draw each quad face as two triangles.
     *
     * This is more reliable than POLY_F4 and is closer to what we will need
     * for a voxel renderer anyway.
     */
    draw_triangle(
        ctx,
        vertices,
        face->a,
        face->b,
        face->c,
        face->r,
        face->g,
        face->b_col,
        ot_z
    );

    draw_triangle(
        ctx,
        vertices,
        face->a,
        face->c,
        face->d,
        face->r,
        face->g,
        face->b_col,
        ot_z
    );
}

static void draw_cube(
    RenderContext *ctx,
    const Vec3i camera_vertices[8],
    const ProjectedVertex projected_vertices[8]
) {
    FaceToDraw faces_to_draw[6];

    const int face_count = collect_visible_faces(camera_vertices, faces_to_draw);
    sort_faces_far_to_near(faces_to_draw, face_count);

    for (int i = 0; i < face_count; i++) {
        /*
         * Higher OT index draws earlier.
         * So farthest face gets the highest slot.
         * Text stays at z=0 and draws on top.
         */
        const int ot_z = 8 + (face_count - 1 - i);

        draw_face(
            ctx,
            projected_vertices,
            faces_to_draw[i].face,
            ot_z
        );
    }
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    ResetGraph(0);
    FntLoad(960, 0);

    RenderContext ctx;
    setup_context(&ctx, SCREEN_W, SCREEN_H, 8, 12, 18);

    int angle = 0;

    for (;;) {
        Vec3i camera_vertices[8];
        ProjectedVertex projected_vertices[8];

        transform_cube(camera_vertices, projected_vertices, angle);
        draw_cube(&ctx, camera_vertices, projected_vertices);

        draw_text(&ctx, 8, 16, 0, "MINECRAFT PS1");
        draw_text(&ctx, 8, 32, 0, "solid cube + backface culling");

        angle = (angle + 1) & 63;

        flip_buffers(&ctx);
    }

    return 0;
}