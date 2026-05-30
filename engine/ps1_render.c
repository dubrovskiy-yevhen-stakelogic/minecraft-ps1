/*
 * engine/ps1_render.c
 *
 * Low-level PS1 rendering helpers extracted from main.c without logic changes.
 * Transitional stage: included from main.c so CMake does not need changes yet.
 */

#include "ps1_render.h"

void setup_context(RenderContext *context, int w, int h, int r, int g, int b) {
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

void flip_buffers(RenderContext *context) {
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

void *new_primitive(RenderContext *context, int z, size_t size) {
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

void draw_text(RenderContext *context, int x, int y, int z, const char *text) {
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

void draw_line(
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

void draw_filled_rect(
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

void draw_panel(
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
