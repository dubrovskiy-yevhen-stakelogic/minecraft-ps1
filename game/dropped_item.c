/*
 * game/dropped_item.c
 *
 * Dropped block item spawning, pickup and rendering.
 * Transitional stage: included directly from main.c.
 */

#include "dropped_item.h"

static int get_block_drop_type(int block_type) {
    if (block_type == BLOCK_GRASS) {
        return BLOCK_DIRT;
    }

    return block_type;
}


static void reset_dropped_items(void) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        dropped_items[i].active = 0;
        dropped_items[i].x = 0;
        dropped_items[i].y = 0;
        dropped_items[i].z = 0;
        dropped_items[i].type = BLOCK_AIR;
        dropped_items[i].count = 0;
        dropped_items[i].bob_frame = 0;
    }
}


static void spawn_dropped_item(int block_type, int block_x, int block_y, int block_z) {
    const int drop_type = get_block_drop_type(block_type);

    if (drop_type == BLOCK_AIR) {
        return;
    }

    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            dropped_items[i].active = 1;
            dropped_items[i].x = tile_to_world_center(block_x);
            dropped_items[i].y = block_y_to_world_min(block_y) + BLOCK_HALF;
            dropped_items[i].z = tile_to_world_center(block_z);
            dropped_items[i].type = (uint8_t)drop_type;
            dropped_items[i].count = 1;
            dropped_items[i].bob_frame = 0;
            return;
        }
    }

    /*
     * Fallback: if the world is full of drops, reward the item directly.
     */
    add_items_to_inventory((uint8_t)drop_type, 1);
}


static void update_dropped_items(void) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            continue;
        }

        dropped_items[i].bob_frame++;

        if (
            iabs(dropped_items[i].x - game_state.player.camera_pos_x) <= PICKUP_DISTANCE_XZ &&
            iabs(dropped_items[i].z - game_state.player.camera_pos_z) <= PICKUP_DISTANCE_XZ &&
            iabs(dropped_items[i].y - game_state.player.camera_pos_y) <= PICKUP_DISTANCE_Y
        ) {
            const int remaining = add_items_to_inventory(
                dropped_items[i].type,
                dropped_items[i].count
            );

            if (remaining <= 0) {
                dropped_items[i].active = 0;
                set_system_status("PICKED UP", 35);
            } else {
                dropped_items[i].count = (uint8_t)remaining;
            }
        }
    }
}


static int project_world_position(
    int world_x,
    int world_y,
    int world_z,
    ProjectedVertex *projected
) {
    const int sin_y = isin(-game_state.player.camera_yaw);
    const int cos_y = icos(-game_state.player.camera_yaw);

    const int sin_x = isin(game_state.player.camera_pitch);
    const int cos_x = icos(game_state.player.camera_pitch);

    const int rel_x = world_x - game_state.player.camera_pos_x;
    const int rel_y = world_y - game_state.player.camera_pos_y;
    const int rel_z = world_z - game_state.player.camera_pos_z;

    const int x1 = ((rel_x * cos_y) + (rel_z * sin_y)) / FIXED_ONE;
    const int z1 = ((-rel_x * sin_y) + (rel_z * cos_y)) / FIXED_ONE;

    const int y2 = ((rel_y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
    const int z2 = ((rel_y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

    Vec3i camera_point;

    if (z2 < NEAR_PLANE_Z || z2 > FAR_PLANE_Z) {
        return 0;
    }

    camera_point.x = x1;
    camera_point.y = y2;
    camera_point.z = z2;

    project_camera_vertex(&camera_point, projected);

    if (
        projected->x < -12 ||
        projected->x > SCREEN_W + 12 ||
        projected->y < -12 ||
        projected->y > SCREEN_H + 12
    ) {
        return 0;
    }

    return 1;
}


static int dropped_item_texture_type(uint8_t block_type) {
    if (block_type == BLOCK_DIRT) {
        return 1;
    }

    if (block_type == BLOCK_STONE) {
        return 2;
    }

    if (block_type == BLOCK_SAND) {
        return 6;
    }

    if (block_type == BLOCK_LOG) {
        return 4;
    }

    if (block_type == BLOCK_PLANKS) {
        return 7;
    }

    if (block_type == BLOCK_WORKBENCH) {
        return 8;
    }

    if (block_type == BLOCK_GRASS) {
        return 0;
    }

    return -1;
}


static void draw_dropped_item_icon(
    RenderContext *context,
    int x,
    int y,
    int size,
    int ot_z,
    uint8_t block_type,
    int seed
) {
    const int texture_type = dropped_item_texture_type(block_type);

    if (texture_type < 0) {
        return;
    }

    /*
     * Use the item's real OT depth, not HUD depth.
     * This lets world polygons in front of the item cover it.
     */
    draw_filled_rect(context, x - 1, y - 1, size + 2, size + 2, ot_z + 1, 18, 18, 18);
    draw_minecraft_texture_block(context, x, y, size, texture_type, ot_z, seed);

    /*
     * Extra tiny type markers make 8-12px drops more recognizable on PS1.
     */
    if (block_type == BLOCK_STONE) {
        draw_line(context, x + 2, y + 3, x + size - 3, y + 3, ot_z, 190, 190, 190);
        draw_line(context, x + 3, y + size - 4, x + size - 4, y + size - 4, ot_z, 70, 70, 70);
    } else if (block_type == BLOCK_SAND) {
        draw_filled_rect(context, x + 2, y + 2, 3, 3, ot_z, 240, 226, 142);
        draw_filled_rect(context, x + size - 5, y + size - 5, 3, 3, ot_z, 156, 134, 72);
    } else if (block_type == BLOCK_DIRT) {
        draw_filled_rect(context, x + 2, y + size - 5, size - 4, 2, ot_z, 74, 44, 24);
    } else if (block_type == BLOCK_GRASS) {
        draw_filled_rect(context, x + 2, y + 2, size - 4, 2, ot_z, 96, 214, 72);
    } else if (block_type == BLOCK_LOG) {
        draw_filled_rect(context, x + 2, y + 1, 3, size - 2, ot_z, 70, 44, 24);
        draw_filled_rect(context, x + size - 4, y + 1, 2, size - 2, ot_z, 130, 88, 48);
    } else if (block_type == BLOCK_PLANKS) {
        draw_line(context, x + 1, y + (size / 3), x + size - 2, y + (size / 3), ot_z, 96, 58, 30);
        draw_line(context, x + 1, y + ((size * 2) / 3), x + size - 2, y + ((size * 2) / 3), ot_z, 96, 58, 30);
    } else if (block_type == BLOCK_WORKBENCH) {
        draw_line(context, x + 2, y + 2, x + size - 3, y + size - 3, ot_z, 64, 38, 18);
        draw_line(context, x + size - 3, y + 2, x + 2, y + size - 3, ot_z, 64, 38, 18);
    }
}


static void draw_dropped_items(RenderContext *context) {
    for (int i = 0; i < MAX_DROPPED_ITEMS; i++) {
        if (!dropped_items[i].active) {
            continue;
        }

        ProjectedVertex projected;
        const int bob = ((dropped_items[i].bob_frame >> 3) & 1) ? 3 : 0;
        int ot_z;
        int size;

        if (!project_world_position(
            dropped_items[i].x,
            dropped_items[i].y + bob,
            dropped_items[i].z,
            &projected
        )) {
            continue;
        }

        ot_z = depth_to_ot(projected.z);

        /*
         * Approximate perspective scaling for readability.
         * Close items are a little larger; far items remain small.
         */
        size = 14 - (projected.z / 90);

        if (size < 7) {
            size = 7;
        }

        if (size > 12) {
            size = 12;
        }

        draw_dropped_item_icon(
            context,
            projected.x - (size / 2),
            projected.y - (size / 2),
            size,
            ot_z,
            dropped_items[i].type,
            900 + i
        );
    }
}
