/*
 * game/player.c
 *
 * Player/camera movement, autojump, block breaking and gameplay input.
 * Transitional stage: included directly from main.c.
 */

#include "player.h"

static int project_breaking_world_point(
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

    if (z2 < NEAR_PLANE_Z || z2 > FAR_PLANE_Z) {
        return 0;
    }

    projected->x = (SCREEN_W / 2) + ((x1 * FOCAL_LENGTH) / z2);
    projected->y = (SCREEN_H / 2) - ((y2 * FOCAL_LENGTH) / z2);
    projected->z = z2;

    return 1;
}


static ProjectedVertex interpolate_breaking_face_point(
    const ProjectedVertex *p0,
    const ProjectedVertex *p1,
    const ProjectedVertex *p3,
    int u,
    int v
) {
    ProjectedVertex result;

    result.x = p0->x + (((p1->x - p0->x) * u) / 16) + (((p3->x - p0->x) * v) / 16);
    result.y = p0->y + (((p1->y - p0->y) * u) / 16) + (((p3->y - p0->y) * v) / 16);
    result.z = p0->z + (((p1->z - p0->z) * u) / 16) + (((p3->z - p0->z) * v) / 16);

    return result;
}


static void draw_breaking_crack_line(
    RenderContext *context,
    const ProjectedVertex *p0,
    const ProjectedVertex *p1,
    const ProjectedVertex *p3,
    int u0,
    int v0,
    int u1,
    int v1
) {
    const ProjectedVertex a = interpolate_breaking_face_point(p0, p1, p3, u0, v0);
    const ProjectedVertex b = interpolate_breaking_face_point(p0, p1, p3, u1, v1);

    /*
     * Two very close dark lines make the crack readable on noisy block colors.
     */
    draw_line(context, a.x, a.y, b.x, b.y, 0, 8, 8, 8);
    draw_line(context, a.x + 1, a.y, b.x + 1, b.y, 0, 42, 42, 42);
}


static int get_breaking_face_vertices(ProjectedVertex out[4]) {
    const int x0 = tile_to_world_center(breaking_block_x) - BLOCK_HALF;
    const int x1 = tile_to_world_center(breaking_block_x) + BLOCK_HALF;
    const int y0 = block_y_to_world_min(breaking_block_y);
    const int y1 = block_y_to_world_top(breaking_block_y);
    const int z0 = tile_to_world_center(breaking_block_z) - BLOCK_HALF;
    const int z1 = tile_to_world_center(breaking_block_z) + BLOCK_HALF;

    int wx[4];
    int wy[4];
    int wz[4];

    switch (breaking_block_face) {
        case FACE_NEG_X:
            wx[0] = x0; wy[0] = y0; wz[0] = z1;
            wx[1] = x0; wy[1] = y1; wz[1] = z1;
            wx[2] = x0; wy[2] = y1; wz[2] = z0;
            wx[3] = x0; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_X:
            wx[0] = x1; wy[0] = y0; wz[0] = z0;
            wx[1] = x1; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z1;
            wx[3] = x1; wy[3] = y0; wz[3] = z1;
            break;

        case FACE_NEG_Z:
            wx[0] = x0; wy[0] = y0; wz[0] = z0;
            wx[1] = x0; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z0;
            wx[3] = x1; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_Z:
            wx[0] = x1; wy[0] = y0; wz[0] = z1;
            wx[1] = x1; wy[1] = y1; wz[1] = z1;
            wx[2] = x0; wy[2] = y1; wz[2] = z1;
            wx[3] = x0; wy[3] = y0; wz[3] = z1;
            break;

        case FACE_NEG_Y:
            wx[0] = x0; wy[0] = y0; wz[0] = z1;
            wx[1] = x1; wy[1] = y0; wz[1] = z1;
            wx[2] = x1; wy[2] = y0; wz[2] = z0;
            wx[3] = x0; wy[3] = y0; wz[3] = z0;
            break;

        case FACE_POS_Y:
        default:
            wx[0] = x0; wy[0] = y1; wz[0] = z0;
            wx[1] = x1; wy[1] = y1; wz[1] = z0;
            wx[2] = x1; wy[2] = y1; wz[2] = z1;
            wx[3] = x0; wy[3] = y1; wz[3] = z1;
            break;
    }

    for (int i = 0; i < 4; i++) {
        if (!project_breaking_world_point(wx[i], wy[i], wz[i], &(out[i]))) {
            return 0;
        }
    }

    return 1;
}


static void draw_breaking_overlay(RenderContext *context) {
    if (!breaking_active || breaking_required_frames <= 0) {
        return;
    }

    ProjectedVertex face[4];
    int stage = (breaking_progress * 7) / breaking_required_frames;

    if (stage < 1) {
        stage = 1;
    }

    if (stage > 7) {
        stage = 7;
    }

    if (!get_breaking_face_vertices(face)) {
        return;
    }

    /*
     * Minecraft-like cracks projected onto the actual targeted block face.
     * No progress bar and no cursor-space sticks.
     */
    draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 7, 2, 8, 7);

    if (stage >= 2) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 5, 10);
    }

    if (stage >= 3) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 12, 9);
    }

    if (stage >= 4) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 5, 10, 3, 14);
    }

    if (stage >= 5) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 12, 9, 14, 13);
    }

    if (stage >= 6) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 7, 2, 4, 4);
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 8, 7, 10, 4);
    }

    if (stage >= 7) {
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 5, 10, 8, 14);
        draw_breaking_crack_line(context, &(face[0]), &(face[1]), &(face[3]), 12, 9, 11, 15);
    }
}


static int get_surface_top_block_y_at_world_position(int world_x, int world_z) {
    return get_top_solid_block_y(
        world_to_tile(world_x),
        world_to_tile(world_z)
    );
}


static int get_player_center_top_block_y(void) {
    return get_surface_top_block_y_at_world_position(
        game_state.player.camera_pos_x,
        game_state.player.camera_pos_z
    );
}


static int is_player_footprint_clear_at(
    int world_x,
    int world_z,
    int target_top_block_y
) {
    int sample_x[9];
    int sample_z[9];

    /*
     * Square-ish collision footprint.
     *
     * This is intentionally not a renderer fix. The issue is that the camera
     * can get close enough to a side wall that the near plane clips into the
     * voxel. When that happens, blue background leaks through while turning.
     *
     * We keep the camera farther from block faces by testing a small player
     * body footprint before accepting movement.
     */
    sample_x[0] = world_x;
    sample_z[0] = world_z;

    sample_x[1] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[1] = world_z;

    sample_x[2] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[2] = world_z;

    sample_x[3] = world_x;
    sample_z[3] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[4] = world_x;
    sample_z[4] = world_z + PLAYER_COLLISION_RADIUS;

    sample_x[5] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[5] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[6] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[6] = world_z - PLAYER_COLLISION_RADIUS;

    sample_x[7] = world_x - PLAYER_COLLISION_RADIUS;
    sample_z[7] = world_z + PLAYER_COLLISION_RADIUS;

    sample_x[8] = world_x + PLAYER_COLLISION_RADIUS;
    sample_z[8] = world_z + PLAYER_COLLISION_RADIUS;

    for (int i = 0; i < 9; i++) {
        const int sample_top_block_y = get_surface_top_block_y_at_world_position(
            sample_x[i],
            sample_z[i]
        );

        if (sample_top_block_y < 0) {
            return 0;
        }

        /*
         * Footprint rule:
         *
         * Without autojump, higher blocks inside the player's collision radius
         * are walls.
         *
         * With autojump enabled, allow the footprint to touch a block that is
         * one level higher. Otherwise the player can never get close enough for
         * the center point to cross onto the higher tile.
         */
        if (sample_top_block_y > target_top_block_y) {
            if (!game_state.player.autojump_enabled) {
                return 0;
            }

            if ((sample_top_block_y - target_top_block_y) > 1) {
                return 0;
            }
        }
    }

    return 1;
}


static void snap_camera_to_ground(void) {
    const int top_block_y = get_player_center_top_block_y();

    if (top_block_y < 0) {
        return;
    }

    game_state.player.camera_pos_y = block_y_to_world_top(top_block_y) + PLAYER_EYE_HEIGHT;
}


static void reset_camera(void) {
    game_state.player.fly_mode_enabled = 0;

    game_state.player.camera_pos_x = 0;
    game_state.player.camera_pos_z = 0;
    game_state.player.camera_yaw = 0;
    game_state.player.camera_pitch = DEFAULT_CAMERA_PITCH;

    snap_camera_to_ground();

    mesh_center_tile_x = 999999;
    mesh_center_tile_z = 999999;
    mesh_dirty = 1;
}


static int is_walk_target_valid(int next_x, int next_z) {
    const int current_top_block_y = get_player_center_top_block_y();
    const int next_top_block_y = get_surface_top_block_y_at_world_position(
        next_x,
        next_z
    );

    if (current_top_block_y < 0 || next_top_block_y < 0) {
        return 0;
    }

    /*
     * Optional Minecraft-like autojump.
     * ON: allow stepping up exactly one block.
     * OFF: higher blocks behave like walls.
     */
    if (next_top_block_y > current_top_block_y) {
        if (!game_state.player.autojump_enabled) {
            return 0;
        }

        if ((next_top_block_y - current_top_block_y) > 1) {
            return 0;
        }
    }

    /*
     * Allow a small drop, but do not walk off large cliffs/holes yet.
     */
    if ((current_top_block_y - next_top_block_y) > MAX_AUTO_DROP_BLOCKS) {
        return 0;
    }

    if (!is_player_footprint_clear_at(
        next_x,
        next_z,
        next_top_block_y
    )) {
        return 0;
    }

    return 1;
}


static void try_walk_move(int delta_x, int delta_z) {
    const int next_x = game_state.player.camera_pos_x + delta_x;
    const int next_z = game_state.player.camera_pos_z + delta_z;

    if (!is_walk_target_valid(next_x, next_z)) {
        return;
    }

    game_state.player.camera_pos_x = next_x;
    game_state.player.camera_pos_z = next_z;

    snap_camera_to_ground();
}


static int get_block_hardness_frames(int block_type) {
    if (block_type == BLOCK_SAND) {
        return BLOCK_BREAK_SAND_FRAMES;
    }

    if (block_type == BLOCK_STONE) {
        return BLOCK_BREAK_STONE_FRAMES;
    }

    if (block_type == BLOCK_GRASS) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_LOG) {
        return BLOCK_BREAK_STONE_FRAMES;
    }

    if (block_type == BLOCK_PLANKS) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_WORKBENCH) {
        return BLOCK_BREAK_GRASS_FRAMES;
    }

    if (block_type == BLOCK_DIRT) {
        return BLOCK_BREAK_DIRT_FRAMES;
    }

    return BLOCK_BREAK_MIN_FRAMES;
}


static void reset_block_breaking(void) {
    breaking_active = 0;
    breaking_block_x = 0;
    breaking_block_y = 0;
    breaking_block_z = 0;
    breaking_block_face = FACE_POS_Y;
    breaking_block_type = BLOCK_AIR;
    breaking_progress = 0;
    breaking_required_frames = BLOCK_BREAK_MIN_FRAMES;
}


static void update_block_breaking(int is_break_button_down) {
    const RaycastHit hit = raycast_block();

    if (!is_break_button_down || !hit.found) {
        reset_block_breaking();
        return;
    }

    const int block_type = get_block_type(hit.hit_x, hit.hit_y, hit.hit_z);

    if (block_type == BLOCK_AIR) {
        reset_block_breaking();
        return;
    }

    if (
        !breaking_active ||
        breaking_block_x != hit.hit_x ||
        breaking_block_y != hit.hit_y ||
        breaking_block_z != hit.hit_z ||
        breaking_block_face != hit.hit_face ||
        breaking_block_type != block_type
    ) {
        breaking_active = 1;
        breaking_block_x = hit.hit_x;
        breaking_block_y = hit.hit_y;
        breaking_block_z = hit.hit_z;
        breaking_block_face = hit.hit_face;
        breaking_block_type = block_type;
        breaking_progress = 0;
        breaking_required_frames = get_block_hardness_frames(block_type);
        set_system_status("BREAKING...", 30);
    }

    /*
     * Keep this intentionally slow and visible.
     * Earlier values felt identical to instant block deletion.
     */
    if (breaking_progress < breaking_required_frames) {
        breaking_progress++;
    }

    if (breaking_progress >= breaking_required_frames) {
        set_block_type(hit.hit_x, hit.hit_y, hit.hit_z, BLOCK_AIR);
        spawn_dropped_item(block_type, hit.hit_x, hit.hit_y, hit.hit_z);

        if (!game_state.player.fly_mode_enabled) {
            snap_camera_to_ground();
        }

        set_system_status("BLOCK BROKEN", 45);
        reset_block_breaking();
    }
}


static int block_intersects_player_footprint(int block_x, int block_y, int block_z) {
    const int player_top_block_y = get_player_center_top_block_y();

    /*
     * Only care about blocks around player's body height. This prevents placing
     * a block into your own collision space.
     */
    if (player_top_block_y >= 0 && block_y <= player_top_block_y) {
        return 0;
    }

    const int block_min_x = tile_to_world_center(block_x) - BLOCK_HALF;
    const int block_max_x = tile_to_world_center(block_x) + BLOCK_HALF;
    const int block_min_z = tile_to_world_center(block_z) - BLOCK_HALF;
    const int block_max_z = tile_to_world_center(block_z) + BLOCK_HALF;

    const int player_min_x = game_state.player.camera_pos_x - PLAYER_COLLISION_RADIUS;
    const int player_max_x = game_state.player.camera_pos_x + PLAYER_COLLISION_RADIUS;
    const int player_min_z = game_state.player.camera_pos_z - PLAYER_COLLISION_RADIUS;
    const int player_max_z = game_state.player.camera_pos_z + PLAYER_COLLISION_RADIUS;

    if (player_max_x <= block_min_x || player_min_x >= block_max_x) {
        return 0;
    }

    if (player_max_z <= block_min_z || player_min_z >= block_max_z) {
        return 0;
    }

    return 1;
}


static void add_target_block(void) {
    const RaycastHit hit = raycast_block();
    const int selected_block_type = get_selected_hotbar_block_type();

    if (selected_block_type == BLOCK_AIR) {
        set_system_status("EMPTY SLOT", 45);
        return;
    }

    if (!is_placeable_block_type(selected_block_type)) {
        set_system_status("NOT A BLOCK", 45);
        return;
    }

    if (!hit.found) {
        return;
    }

    if (!is_valid_block_position(hit.place_x, hit.place_y, hit.place_z)) {
        return;
    }

    if (get_block_type(hit.place_x, hit.place_y, hit.place_z) != BLOCK_AIR) {
        return;
    }

    if (block_intersects_player_footprint(
        hit.place_x,
        hit.place_y,
        hit.place_z
    )) {
        return;
    }

    set_block_type(hit.place_x, hit.place_y, hit.place_z, selected_block_type);
    consume_selected_hotbar_block();
}


static void update_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    const int forward_x = isin(game_state.player.camera_yaw);
    const int forward_z = icos(game_state.player.camera_yaw);

    const int right_x = icos(game_state.player.camera_yaw);
    const int right_z = -isin(game_state.player.camera_yaw);

    if (pressed_this_frame & PAD_START) {
        reset_block_breaking();
        game_state.app.app_state = APP_STATE_PAUSE;
        game_state.app.pause_selected_option = PAUSE_OPTION_RESUME;
        pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_SELECT) {
        if (save_game_to_memory_card()) {
            set_system_status("SAVE OK", 90);
        } else {
            set_system_status("SAVE FAILED", 120);
        }
    }

    update_dropped_items();
    update_block_breaking((buttons & PAD_SQUARE) != 0);

    if (pressed_this_frame & PAD_CIRCLE) {
        reset_block_breaking();
        add_target_block();
    }

    if (pressed_this_frame & PAD_CROSS) {
        const RaycastHit use_hit = raycast_block();

        if (
            use_hit.found &&
            get_block_type(use_hit.hit_x, use_hit.hit_y, use_hit.hit_z) == BLOCK_WORKBENCH
        ) {
            reset_block_breaking();
            game_state.app.app_state = APP_STATE_WORKBENCH;
            workbench_cursor_slot = WORKBENCH_CURSOR_CRAFT_START;
            set_system_status("WORKBENCH", 45);
            pad_previous_buttons = buttons;
            return;
        }
    }

    if (buttons & PAD_LEFT) {
        game_state.player.camera_yaw = (game_state.player.camera_yaw - CAMERA_YAW_SPEED) & ANGLE_MASK;
    }

    if (buttons & PAD_RIGHT) {
        game_state.player.camera_yaw = (game_state.player.camera_yaw + CAMERA_YAW_SPEED) & ANGLE_MASK;
    }

    if (buttons & PAD_TRIANGLE) {
        game_state.player.camera_pitch -= CAMERA_PITCH_SPEED;
    }

    if (buttons & PAD_CROSS) {
        game_state.player.camera_pitch += CAMERA_PITCH_SPEED;
    }

    if (!game_state.player.fly_mode_enabled) {
        if (pressed_this_frame & PAD_L2) {
            select_previous_hotbar_slot();
        }

        if (pressed_this_frame & PAD_R2) {
            select_next_hotbar_slot();
        }
    }

    game_state.player.camera_pitch = clamp_int(
        game_state.player.camera_pitch,
        CAMERA_PITCH_MIN,
        CAMERA_PITCH_MAX
    );

    if (game_state.player.fly_mode_enabled) {
        if (buttons & PAD_UP) {
            game_state.player.camera_pos_x += (forward_x * FLY_MOVE_SPEED) / FIXED_ONE;
            game_state.player.camera_pos_z += (forward_z * FLY_MOVE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_DOWN) {
            game_state.player.camera_pos_x -= (forward_x * FLY_MOVE_SPEED) / FIXED_ONE;
            game_state.player.camera_pos_z -= (forward_z * FLY_MOVE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_L1) {
            game_state.player.camera_pos_x -= (right_x * FLY_STRAFE_SPEED) / FIXED_ONE;
            game_state.player.camera_pos_z -= (right_z * FLY_STRAFE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_R1) {
            game_state.player.camera_pos_x += (right_x * FLY_STRAFE_SPEED) / FIXED_ONE;
            game_state.player.camera_pos_z += (right_z * FLY_STRAFE_SPEED) / FIXED_ONE;
        }

        if (buttons & PAD_L2) {
            game_state.player.camera_pos_y += FLY_VERTICAL_SPEED;
        }

        if (buttons & PAD_R2) {
            game_state.player.camera_pos_y -= FLY_VERTICAL_SPEED;
        }
    } else {
        if (buttons & PAD_UP) {
            try_walk_move(
                (forward_x * WALK_MOVE_SPEED) / FIXED_ONE,
                (forward_z * WALK_MOVE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_DOWN) {
            try_walk_move(
                -(forward_x * WALK_MOVE_SPEED) / FIXED_ONE,
                -(forward_z * WALK_MOVE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_L1) {
            try_walk_move(
                -(right_x * WALK_STRAFE_SPEED) / FIXED_ONE,
                -(right_z * WALK_STRAFE_SPEED) / FIXED_ONE
            );
        }

        if (buttons & PAD_R1) {
            try_walk_move(
                (right_x * WALK_STRAFE_SPEED) / FIXED_ONE,
                (right_z * WALK_STRAFE_SPEED) / FIXED_ONE
            );
        }

        snap_camera_to_ground();
    }

    pad_previous_buttons = buttons;
}
