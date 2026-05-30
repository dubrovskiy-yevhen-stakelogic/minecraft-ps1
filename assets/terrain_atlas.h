#ifndef MCPSX_ASSETS_TERRAIN_ATLAS_H
#define MCPSX_ASSETS_TERRAIN_ATLAS_H

#include <stdint.h>

#define TEXTURE_ATLAS_X 640
#define TEXTURE_ATLAS_Y 0
#define TEXTURE_ATLAS_W 128
#define TEXTURE_ATLAS_H 16

#define LOG_SIDE_U 0
#define LOG_TOP_U 16
#define LOG_TILE_V 0
#define LOG_TILE_SIZE 16

#define PLANKS_U 32
#define DIRT_U 48
#define GRASS_SIDE_U 64
#define GRASS_TOP_U 80
#define COBBLESTONE_U 96
#define SAND_U 112

extern const uint16_t terrain_log_atlas_16bpp[TEXTURE_ATLAS_H][TEXTURE_ATLAS_W];

void terrain_atlas_upload(void);

#endif
