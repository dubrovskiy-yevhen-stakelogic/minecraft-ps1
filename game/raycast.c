/*
 * game/raycast.c
 *
 * Camera raycast and hit-face helpers.
 * Transitional stage: included directly from main.c.
 */

#include "raycast.h"

static void get_camera_forward_direction(Vec3i *dir) {
    const int cos_pitch = icos(game_state.player.camera_pitch);

    dir->x = (isin(game_state.player.camera_yaw) * cos_pitch) / FIXED_ONE;
    dir->y = isin(game_state.player.camera_pitch);
    dir->z = (icos(game_state.player.camera_yaw) * cos_pitch) / FIXED_ONE;
}

static int is_valid_block_position(int x, int y, int z) {
    (void)x;
    (void)z;
    return (y >= 0 && y < WORLD_HEIGHT);
}

static int get_hit_face_from_empty_block(
    int empty_x,
    int empty_y,
    int empty_z,
    int block_x,
    int block_y,
    int block_z
) {
    if (empty_x < block_x) {
        return FACE_NEG_X;
    }

    if (empty_x > block_x) {
        return FACE_POS_X;
    }

    if (empty_z < block_z) {
        return FACE_NEG_Z;
    }

    if (empty_z > block_z) {
        return FACE_POS_Z;
    }

    if (empty_y < block_y) {
        return FACE_NEG_Y;
    }

    return FACE_POS_Y;
}

static RaycastHit raycast_block(void) {
    RaycastHit hit;
    Vec3i dir;

    hit.found = 0;
    hit.hit_x = 0;
    hit.hit_y = 0;
    hit.hit_z = 0;
    hit.hit_face = FACE_POS_Y;
    hit.place_x = 0;
    hit.place_y = 0;
    hit.place_z = 0;

    get_camera_forward_direction(&dir);

    {
        int last_empty_valid = 0;
        int last_empty_x = 0;
        int last_empty_y = 0;
        int last_empty_z = 0;

        int previous_x = 999999;
        int previous_y = 999999;
        int previous_z = 999999;

        for (int distance = 8; distance <= RAYCAST_MAX_DISTANCE; distance += RAYCAST_STEP) {
            const int sample_x = game_state.player.camera_pos_x + ((dir.x * distance) / FIXED_ONE);
            const int sample_y = game_state.player.camera_pos_y + ((dir.y * distance) / FIXED_ONE);
            const int sample_z = game_state.player.camera_pos_z + ((dir.z * distance) / FIXED_ONE);

            const int block_x = world_to_tile(sample_x);
            const int block_y = world_to_block_y(sample_y);
            const int block_z = world_to_tile(sample_z);

            if (
                block_x == previous_x &&
                block_y == previous_y &&
                block_z == previous_z
            ) {
                continue;
            }

            previous_x = block_x;
            previous_y = block_y;
            previous_z = block_z;

            if (!is_valid_block_position(block_x, block_y, block_z)) {
                continue;
            }

            if (get_block_type(block_x, block_y, block_z) != BLOCK_AIR) {
                hit.found = 1;
                hit.hit_x = block_x;
                hit.hit_y = block_y;
                hit.hit_z = block_z;

                if (last_empty_valid) {
                    hit.hit_face = get_hit_face_from_empty_block(
                        last_empty_x,
                        last_empty_y,
                        last_empty_z,
                        block_x,
                        block_y,
                        block_z
                    );
                    hit.place_x = last_empty_x;
                    hit.place_y = last_empty_y;
                    hit.place_z = last_empty_z;
                } else {
                    hit.hit_face = FACE_POS_Y;
                    hit.place_x = block_x;
                    hit.place_y = block_y + 1;
                    hit.place_z = block_z;
                }

                return hit;
            }

            last_empty_valid = 1;
            last_empty_x = block_x;
            last_empty_y = block_y;
            last_empty_z = block_z;
        }
    }

    return hit;
}
