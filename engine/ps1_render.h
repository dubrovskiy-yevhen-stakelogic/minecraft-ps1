#ifndef MCPSX_ENGINE_PS1_RENDER_H
#define MCPSX_ENGINE_PS1_RENDER_H

#include <stddef.h>
#include <stdint.h>

/*
 * RenderContext is defined in main.c before this header is included.
 * In the next refactor step it should move to an engine/common header.
 */
void setup_context(RenderContext *context, int w, int h, int r, int g, int b);
void flip_buffers(RenderContext *context);
void *new_primitive(RenderContext *context, int z, size_t size);

void draw_text(RenderContext *context, int x, int y, int z, const char *text);
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
);
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
);
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
);

#endif
