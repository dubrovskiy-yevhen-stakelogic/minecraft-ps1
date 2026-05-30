#ifndef MCPSX_ENGINE_PS1_TEXTURE_H
#define MCPSX_ENGINE_PS1_TEXTURE_H

#include <stdint.h>

void ps1_upload_texture_16bpp(
    int x,
    int y,
    int w,
    int h,
    const uint16_t *pixels
);

#endif
