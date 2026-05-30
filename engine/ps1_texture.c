/*
 * engine/ps1_texture.c
 *
 * Low-level PS1 VRAM texture upload helpers.
 * This file knows how to upload pixel data, but it does not know Minecraft
 * blocks, recipes, or terrain semantics.
 */

#include "ps1_texture.h"

#include <psxgpu.h>

void ps1_upload_texture_16bpp(
    int x,
    int y,
    int w,
    int h,
    const uint16_t *pixels
) {
    RECT rect;

    setRECT(&rect, x, y, w, h);

    LoadImage(&rect, (uint32_t *)pixels);
    DrawSync(0);
}
