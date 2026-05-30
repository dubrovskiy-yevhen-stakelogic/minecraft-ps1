/*
 * game/world.c
 *
 * Voxel world data, block storage, mesh building and world mesh rendering.
 * Transitional stage: included directly from main.c.
 */

#include "world.h"

static int world_to_tile(int value) {
    return floor_div(value + BLOCK_HALF, BLOCK_SIZE);
}

static int tile_to_world_center(int tile) {
    return tile * BLOCK_SIZE;
}

static int world_to_block_y(int value) {
    return floor_div(value + BLOCK_SIZE, BLOCK_SIZE);
}

static int block_y_to_world_min(int block_y) {
    return -BLOCK_SIZE + (block_y * BLOCK_SIZE);
}

static int block_y_to_world_top(int block_y) {
    return block_y_to_world_min(block_y) + BLOCK_SIZE;
}

static int get_generated_block_type(int x, int y, int z) {
    (void)x;
    (void)z;

    if (y < 0 || y >= WORLD_HEIGHT) {
        return BLOCK_AIR;
    }

    if (y == 0) {
        return BLOCK_DIRT;
    }

    if (y == 1) {
        return BLOCK_GRASS;
    }

    return BLOCK_AIR;
}

static int get_block_edit_index(int x, int y, int z) {
    for (int i = 0; i < game_state.world.block_edit_count; i++) {
        if (
            game_state.world.block_edits[i].x == x &&
            game_state.world.block_edits[i].y == y &&
            game_state.world.block_edits[i].z == z
        ) {
            return i;
        }
    }

    return -1;
}

static int get_block_type(int x, int y, int z) {
    const int edit_index = get_block_edit_index(x, y, z);

    if (edit_index >= 0) {
        return game_state.world.block_edits[edit_index].type;
    }

    return get_generated_block_type(x, y, z);
}

static void remove_block_edit_at_index(int index) {
    if (index < 0 || index >= game_state.world.block_edit_count) {
        return;
    }

    game_state.world.block_edit_count--;

    if (index != game_state.world.block_edit_count) {
        game_state.world.block_edits[index] = game_state.world.block_edits[game_state.world.block_edit_count];
    }
}

static void set_block_type(int x, int y, int z, int type) {
    if (y < 0 || y >= WORLD_HEIGHT) {
        return;
    }

    const int generated_type = get_generated_block_type(x, y, z);
    const int edit_index = get_block_edit_index(x, y, z);

    if (type == generated_type) {
        if (edit_index >= 0) {
            remove_block_edit_at_index(edit_index);
        }

        game_state.world.mesh_dirty = 1;
        return;
    }

    if (edit_index >= 0) {
        game_state.world.block_edits[edit_index].type = (uint8_t)type;
        game_state.world.mesh_dirty = 1;
        return;
    }

    if (game_state.world.block_edit_count >= MAX_BLOCK_EDITS) {
        return;
    }

    game_state.world.block_edits[game_state.world.block_edit_count].x = x;
    game_state.world.block_edits[game_state.world.block_edit_count].y = y;
    game_state.world.block_edits[game_state.world.block_edit_count].z = z;
    game_state.world.block_edits[game_state.world.block_edit_count].type = (uint8_t)type;
    game_state.world.block_edit_count++;

    game_state.world.mesh_dirty = 1;
}

static int get_top_solid_block_y(int tile_x, int tile_z) {
    for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
        if (get_block_type(tile_x, y, tile_z) != BLOCK_AIR) {
            return y;
        }
    }

    return -1;
}

static void clear_vertex_lookup(void) {
    for (int y = 0; y < GRID_Y_LINES; y++) {
        for (int z = 0; z < GRID_VERTICES_PER_SIDE; z++) {
            for (int x = 0; x < GRID_VERTICES_PER_SIDE; x++) {
                game_state.world.vertex_lookup[y][z][x] = -1;
            }
        }
    }
}

static void build_local_block_cache(int center_tile_x, int center_tile_z) {
    const int start_tile_x = center_tile_x - VIEW_RADIUS;
    const int start_tile_z = center_tile_z - VIEW_RADIUS;

    /*
     * game_state.world.local_blocks has a 1-block border around the visible 17x17 area.
     * That lets us test neighbor blocks in O(1) without calling get_block_type()
     * thousands of times during mesh generation.
     */
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int z = 0; z < PADDED_VIEW_SIZE; z++) {
            for (int x = 0; x < PADDED_VIEW_SIZE; x++) {
                const int world_x = start_tile_x + x - 1;
                const int world_z = start_tile_z + z - 1;

                game_state.world.local_blocks[y][z][x] = (uint8_t)get_generated_block_type(
                    world_x,
                    y,
                    world_z
                );
            }
        }
    }

    /*
     * Apply sparse edits only once per rebuild.
     * Previous version scanned edits for every block lookup; this is what made
     * each placed block progressively slow the game down.
     */
    for (int i = 0; i < game_state.world.block_edit_count; i++) {
        const int local_x = game_state.world.block_edits[i].x - start_tile_x + 1;
        const int local_z = game_state.world.block_edits[i].z - start_tile_z + 1;
        const int y = game_state.world.block_edits[i].y;

        if (
            local_x < 0 ||
            local_x >= PADDED_VIEW_SIZE ||
            local_z < 0 ||
            local_z >= PADDED_VIEW_SIZE ||
            y < 0 ||
            y >= WORLD_HEIGHT
        ) {
            continue;
        }

        game_state.world.local_blocks[y][local_z][local_x] = game_state.world.block_edits[i].type;
    }
}

static int get_local_block_type(int visible_x, int y, int visible_z) {
    /*
     * visible_x/visible_z are usually 0..16.
     * -1 and VIEW_SIZE are allowed for neighbor checks because game_state.world.local_blocks
     * has a 1-block border.
     */
    const int local_x = visible_x + 1;
    const int local_z = visible_z + 1;

    if (
        local_x < 0 ||
        local_x >= PADDED_VIEW_SIZE ||
        local_z < 0 ||
        local_z >= PADDED_VIEW_SIZE ||
        y < 0 ||
        y >= WORLD_HEIGHT
    ) {
        return BLOCK_AIR;
    }

    return game_state.world.local_blocks[y][local_z][local_x];
}

static int add_grid_vertex(
    int local_x_line,
    int y_line,
    int local_z_line,
    int start_world_x,
    int start_world_z
) {
    int *lookup_entry = &(game_state.world.vertex_lookup[y_line][local_z_line][local_x_line]);

    if (*lookup_entry >= 0) {
        return *lookup_entry;
    }

    if (game_state.world.mesh_vertex_count >= MAX_MESH_VERTICES) {
        return 0;
    }

    Vec3i *vertex = &(game_state.world.mesh_vertices[game_state.world.mesh_vertex_count]);

    vertex->x = start_world_x + (local_x_line * BLOCK_SIZE);
    vertex->y = block_y_to_world_min(y_line);
    vertex->z = start_world_z + (local_z_line * BLOCK_SIZE);

    *lookup_entry = game_state.world.mesh_vertex_count;
    game_state.world.mesh_vertex_count++;

    return *lookup_entry;
}

static void push_face_by_grid_vertices(
    int x0,
    int y0,
    int z0,
    int x1,
    int y1,
    int z1,
    int x2,
    int y2,
    int z2,
    int x3,
    int y3,
    int z3,
    int start_world_x,
    int start_world_z,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t block_type,
    uint8_t face_type
) {
    if (game_state.world.mesh_face_count >= MAX_MESH_FACES) {
        return;
    }

    if (
        x0 < 0 || x0 >= GRID_VERTICES_PER_SIDE ||
        x1 < 0 || x1 >= GRID_VERTICES_PER_SIDE ||
        x2 < 0 || x2 >= GRID_VERTICES_PER_SIDE ||
        x3 < 0 || x3 >= GRID_VERTICES_PER_SIDE ||
        z0 < 0 || z0 >= GRID_VERTICES_PER_SIDE ||
        z1 < 0 || z1 >= GRID_VERTICES_PER_SIDE ||
        z2 < 0 || z2 >= GRID_VERTICES_PER_SIDE ||
        z3 < 0 || z3 >= GRID_VERTICES_PER_SIDE ||
        y0 < 0 || y0 >= GRID_Y_LINES ||
        y1 < 0 || y1 >= GRID_Y_LINES ||
        y2 < 0 || y2 >= GRID_Y_LINES ||
        y3 < 0 || y3 >= GRID_Y_LINES
    ) {
        return;
    }

    MeshFace *face = &(game_state.world.mesh_faces[game_state.world.mesh_face_count]);

    face->v[0] = (uint16_t)add_grid_vertex(x0, y0, z0, start_world_x, start_world_z);
    face->v[1] = (uint16_t)add_grid_vertex(x1, y1, z1, start_world_x, start_world_z);
    face->v[2] = (uint16_t)add_grid_vertex(x2, y2, z2, start_world_x, start_world_z);
    face->v[3] = (uint16_t)add_grid_vertex(x3, y3, z3, start_world_x, start_world_z);

    face->r = r;
    face->g = g;
    face->b = b;
    face->block_type = block_type;
    face->face_type = face_type;

    game_state.world.mesh_face_count++;
}

static void get_face_color(int block_type, int face, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (block_type == BLOCK_GRASS) {
        if (face == FACE_POS_Y) {
            *r = 64;
            *g = 175;
            *b = 64;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 45;
            *g = 30;
            *b = 18;
            return;
        }

        *r = 118;
        *g = 76;
        *b = 38;
        return;
    }

    if (block_type == BLOCK_DIRT) {
        if (face == FACE_POS_Y) {
            *r = 125;
            *g = 82;
            *b = 44;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 45;
            *g = 30;
            *b = 18;
            return;
        }

        *r = 105;
        *g = 68;
        *b = 35;
        return;
    }

    if (block_type == BLOCK_STONE) {
        if (face == FACE_POS_Y) {
            *r = 128;
            *g = 130;
            *b = 128;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 58;
            *g = 60;
            *b = 58;
            return;
        }

        *r = 100;
        *g = 102;
        *b = 100;
        return;
    }

    if (block_type == BLOCK_SAND) {
        if (face == FACE_POS_Y) {
            *r = 218;
            *g = 202;
            *b = 118;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 118;
            *g = 102;
            *b = 58;
            return;
        }

        *r = 184;
        *g = 166;
        *b = 92;
        return;
    }

    if (block_type == BLOCK_LOG) {
        if (face == FACE_POS_Y || face == FACE_NEG_Y) {
            *r = 148;
            *g = 104;
            *b = 58;
            return;
        }

        *r = 100;
        *g = 68;
        *b = 36;
        return;
    }

    if (block_type == BLOCK_PLANKS) {
        if (face == FACE_POS_Y) {
            *r = 184;
            *g = 132;
            *b = 70;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 92;
            *g = 58;
            *b = 30;
            return;
        }

        *r = 148;
        *g = 96;
        *b = 48;
        return;
    }

    if (block_type == BLOCK_WORKBENCH) {
        if (face == FACE_POS_Y) {
            *r = 172;
            *g = 116;
            *b = 56;
            return;
        }

        if (face == FACE_NEG_Y) {
            *r = 76;
            *g = 48;
            *b = 24;
            return;
        }

        *r = 126;
        *g = 78;
        *b = 38;
        return;
    }

    *r = 180;
    *g = 180;
    *b = 180;
}

static void push_block_face(
    int local_x,
    int block_y,
    int local_z,
    int face,
    int block_type,
    int start_world_x,
    int start_world_z
) {
    const int x0 = local_x;
    const int x1 = local_x + 1;
    const int y0 = block_y;
    const int y1 = block_y + 1;
    const int z0 = local_z;
    const int z1 = local_z + 1;

    uint8_t r;
    uint8_t g;
    uint8_t b_col;

    get_face_color(block_type, face, &r, &g, &b_col);

    switch (face) {
        case FACE_NEG_Z:
            push_face_by_grid_vertices(
                x0, y0, z0,
                x0, y1, z0,
                x1, y1, z0,
                x1, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_Z:
            push_face_by_grid_vertices(
                x1, y0, z1,
                x1, y1, z1,
                x0, y1, z1,
                x0, y0, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_NEG_X:
            push_face_by_grid_vertices(
                x0, y0, z1,
                x0, y1, z1,
                x0, y1, z0,
                x0, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_X:
            push_face_by_grid_vertices(
                x1, y0, z0,
                x1, y1, z0,
                x1, y1, z1,
                x1, y0, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_NEG_Y:
            push_face_by_grid_vertices(
                x0, y0, z1,
                x1, y0, z1,
                x1, y0, z0,
                x0, y0, z0,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;

        case FACE_POS_Y:
        default:
            push_face_by_grid_vertices(
                x0, y1, z0,
                x1, y1, z0,
                x1, y1, z1,
                x0, y1, z1,
                start_world_x,
                start_world_z,
                r,
                g,
                b_col,
                (uint8_t)block_type,
                (uint8_t)face
            );
            break;
    }
}

static void build_world_mesh(int center_tile_x, int center_tile_z) {
    const int start_tile_x = center_tile_x - VIEW_RADIUS;
    const int start_tile_z = center_tile_z - VIEW_RADIUS;

    const int start_world_x = tile_to_world_center(start_tile_x) - BLOCK_HALF;
    const int start_world_z = tile_to_world_center(start_tile_z) - BLOCK_HALF;

    game_state.world.mesh_vertex_count = 0;
    game_state.world.mesh_face_count = 0;

    clear_vertex_lookup();
    build_local_block_cache(center_tile_x, center_tile_z);

    for (int z = 0; z < VIEW_SIZE; z++) {
        for (int x = 0; x < VIEW_SIZE; x++) {
            for (int y = 0; y < WORLD_HEIGHT; y++) {
                const int block_type = get_local_block_type(x, y, z);

                if (block_type == BLOCK_AIR) {
                    continue;
                }

                if (get_local_block_type(x, y + 1, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_Y,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                /*
                 * Skip bottom face at world floor to save some performance.
                 * Floating blocks still get bottom faces.
                 */
                if (y > 0 && get_local_block_type(x, y - 1, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_Y,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x, y, z - 1) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_Z,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x, y, z + 1) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_Z,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x - 1, y, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_NEG_X,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }

                if (get_local_block_type(x + 1, y, z) == BLOCK_AIR) {
                    push_block_face(
                        x,
                        y,
                        z,
                        FACE_POS_X,
                        block_type,
                        start_world_x,
                        start_world_z
                    );
                }
            }
        }
    }

    game_state.world.mesh_center_tile_x = center_tile_x;
    game_state.world.mesh_center_tile_z = center_tile_z;
    game_state.world.mesh_dirty = 0;
}

static void rebuild_mesh_if_needed(void) {
    const int center_tile_x = world_to_tile(game_state.player.camera_pos_x);
    const int center_tile_z = world_to_tile(game_state.player.camera_pos_z);

    if (
        game_state.world.mesh_dirty ||
        center_tile_x != game_state.world.mesh_center_tile_x ||
        center_tile_z != game_state.world.mesh_center_tile_z
    ) {
        build_world_mesh(center_tile_x, center_tile_z);
    }
}

static void transform_all_vertices(void) {
    const int sin_y = isin(-game_state.player.camera_yaw);
    const int cos_y = icos(-game_state.player.camera_yaw);

    const int sin_x = isin(game_state.player.camera_pitch);
    const int cos_x = icos(game_state.player.camera_pitch);

    for (int i = 0; i < game_state.world.mesh_vertex_count; i++) {
        const Vec3i *world = &(game_state.world.mesh_vertices[i]);

        const int rel_x = world->x - game_state.player.camera_pos_x;
        const int rel_y = world->y - game_state.player.camera_pos_y;
        const int rel_z = world->z - game_state.player.camera_pos_z;

        const int x1 = ((rel_x * cos_y) + (rel_z * sin_y)) / FIXED_ONE;
        const int z1 = ((-rel_x * sin_y) + (rel_z * cos_y)) / FIXED_ONE;

        const int y2 = ((rel_y * cos_x) - (z1 * sin_x)) / FIXED_ONE;
        const int z2 = ((rel_y * sin_x) + (z1 * cos_x)) / FIXED_ONE;

        game_state.world.camera_vertices[i].x = x1;
        game_state.world.camera_vertices[i].y = y2;
        game_state.world.camera_vertices[i].z = z2;
    }
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

static uint8_t fog_blend_channel(uint8_t color, uint8_t fog_color, int fog_amount) {
    const int color_part = ((int)color * (256 - fog_amount));
    const int fog_part = ((int)fog_color * fog_amount);

    return (uint8_t)((color_part + fog_part) >> 8);
}

static void apply_distance_fog(
    int depth,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b
) {
    int fog_amount;

    if (!game_state.app.fog_enabled) {
        return;
    }

    if (depth <= FOG_START_Z) {
        return;
    }

    if (depth >= FOG_FULL_Z) {
        fog_amount = 256;
    } else {
        fog_amount = ((depth - FOG_START_Z) * 256) / (FOG_FULL_Z - FOG_START_Z);
    }

    /*
     * Classic PS1 fog: distant polygons are simply recolored toward the
     * background. No screen-space stripes, no alpha, no texture.
     */
    *r = fog_blend_channel(*r, FOG_SKY_R, fog_amount);
    *g = fog_blend_channel(*g, FOG_SKY_G, fog_amount);
    *b = fog_blend_channel(*b, FOG_SKY_B, fog_amount);
}

static Vec3i intersect_near_plane(const Vec3i *a, const Vec3i *b) {
    Vec3i result;

    const int dz = b->z - a->z;
    const int t_num = NEAR_PLANE_Z - a->z;

    if (dz == 0) {
        result = *a;
        result.z = NEAR_PLANE_Z;
        return result;
    }

    result.x = a->x + (((b->x - a->x) * t_num) / dz);
    result.y = a->y + (((b->y - a->y) * t_num) / dz);
    result.z = NEAR_PLANE_Z;

    return result;
}

static int clip_triangle_to_near_plane(const Vec3i input[3], Vec3i output[4]) {
    int output_count = 0;

    Vec3i previous = input[2];
    int previous_inside = previous.z >= NEAR_PLANE_Z;

    for (int i = 0; i < 3; i++) {
        const Vec3i current = input[i];
        const int current_inside = current.z >= NEAR_PLANE_Z;

        if (current_inside) {
            if (!previous_inside) {
                output[output_count++] = intersect_near_plane(&previous, &current);
            }

            output[output_count++] = current;
        } else if (previous_inside) {
            output[output_count++] = intersect_near_plane(&previous, &current);
        }

        previous = current;
        previous_inside = current_inside;
    }

    return output_count;
}

static void project_camera_vertex(const Vec3i *camera, ProjectedVertex *projected) {
    int z = camera->z;

    if (z < NEAR_PLANE_Z) {
        z = NEAR_PLANE_Z;
    }

    projected->x = (SCREEN_W / 2) + ((camera->x * FOCAL_LENGTH) / z);
    projected->y = (SCREEN_H / 2) - ((camera->y * FOCAL_LENGTH) / z);
    projected->z = camera->z;
}

static int triangle_depth(const Vec3i *a, const Vec3i *b, const Vec3i *c) {
    return (a->z + b->z + c->z) / 3;
}

static void draw_projected_triangle(
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

static void draw_camera_triangle_clipped(
    RenderContext *context,
    const Vec3i *a,
    const Vec3i *b,
    const Vec3i *c,
    uint8_t r,
    uint8_t g,
    uint8_t b_col
) {
    Vec3i input[3];
    Vec3i clipped[4];

    input[0] = *a;
    input[1] = *b;
    input[2] = *c;

    const int count = clip_triangle_to_near_plane(input, clipped);

    if (count < 3) {
        return;
    }

    ProjectedVertex p0;
    ProjectedVertex p1;
    ProjectedVertex p2;
    ProjectedVertex p3;

    project_camera_vertex(&(clipped[0]), &p0);
    project_camera_vertex(&(clipped[1]), &p1);
    project_camera_vertex(&(clipped[2]), &p2);

    int depth = triangle_depth(&(clipped[0]), &(clipped[1]), &(clipped[2]));
    int ot_z = depth_to_ot(depth);

    uint8_t fogged_r = r;
    uint8_t fogged_g = g;
    uint8_t fogged_b = b_col;

    apply_distance_fog(depth, &fogged_r, &fogged_g, &fogged_b);

    draw_projected_triangle(
        context,
        &p0,
        &p1,
        &p2,
        fogged_r,
        fogged_g,
        fogged_b,
        ot_z
    );

    if (count == 4) {
        project_camera_vertex(&(clipped[3]), &p3);

        depth = triangle_depth(&(clipped[0]), &(clipped[2]), &(clipped[3]));
        ot_z = depth_to_ot(depth);

        fogged_r = r;
        fogged_g = g;
        fogged_b = b_col;

        apply_distance_fog(depth, &fogged_r, &fogged_g, &fogged_b);

        draw_projected_triangle(
            context,
            &p0,
            &p2,
            &p3,
            fogged_r,
            fogged_g,
            fogged_b,
            ot_z
        );
    }
}

static int face_center_z(const MeshFace *face) {
    return (
        game_state.world.camera_vertices[face->v[0]].z +
        game_state.world.camera_vertices[face->v[1]].z +
        game_state.world.camera_vertices[face->v[2]].z +
        game_state.world.camera_vertices[face->v[3]].z
    ) / 4;
}

static int face_is_outside_frustum(const MeshFace *face) {
    int all_behind = 1;
    int all_too_far = 1;

    int outside_left = 1;
    int outside_right = 1;
    int outside_top = 1;
    int outside_bottom = 1;

    for (int i = 0; i < 4; i++) {
        const Vec3i *v = &(game_state.world.camera_vertices[face->v[i]]);
        int z = v->z;

        if (z >= NEAR_PLANE_Z) {
            all_behind = 0;
        }

        if (z <= FAR_PLANE_Z) {
            all_too_far = 0;
        }

        if (z < NEAR_PLANE_Z) {
            z = NEAR_PLANE_Z;
        }

        const int max_x = (((SCREEN_W / 2) + FRUSTUM_MARGIN_X) * z) / FOCAL_LENGTH;
        const int max_y = (((SCREEN_H / 2) + FRUSTUM_MARGIN_Y) * z) / FOCAL_LENGTH;

        if (v->x >= -max_x) {
            outside_left = 0;
        }

        if (v->x <= max_x) {
            outside_right = 0;
        }

        if (v->y >= -max_y) {
            outside_top = 0;
        }

        if (v->y <= max_y) {
            outside_bottom = 0;
        }
    }

    return (
        all_behind ||
        all_too_far ||
        outside_left ||
        outside_right ||
        outside_top ||
        outside_bottom
    );
}

static int face_is_safe_for_textured_quad(
    const Vec3i *v0,
    const Vec3i *v1,
    const Vec3i *v2,
    const Vec3i *v3
) {
    /*
     * Keep log texture visible when the player stands right next to the block.
     * project_camera_vertex() already clamps projection to NEAR_PLANE_Z, so we
     * only need to reject faces that are almost on/behind the camera.
     */
    return (
        v0->z > TEXTURED_LOG_MIN_Z &&
        v1->z > TEXTURED_LOG_MIN_Z &&
        v2->z > TEXTURED_LOG_MIN_Z &&
        v3->z > TEXTURED_LOG_MIN_Z
    );
}

static int block_face_texture_u(int block_type, int face_type) {
    if (block_type == BLOCK_LOG) {
        if (face_type == FACE_POS_Y || face_type == FACE_NEG_Y) {
            return LOG_TOP_U;
        }

        return LOG_SIDE_U;
    }

    if (block_type == BLOCK_PLANKS) {
        return PLANKS_U;
    }

    return LOG_SIDE_U;
}

static void draw_projected_textured_triangle(
    RenderContext *context,
    const ProjectedVertex *a,
    const ProjectedVertex *b,
    const ProjectedVertex *c,
    int u0,
    int v0,
    int u1,
    int v1,
    int u2,
    int v2,
    int ot_z
) {
    POLY_FT3 *poly = (POLY_FT3 *)new_primitive(context, ot_z, sizeof(POLY_FT3));

    setPolyFT3(poly);
    setRGB0(poly, 128, 128, 128);

    setXY3(
        poly,
        a->x, a->y,
        b->x, b->y,
        c->x, c->y
    );

    setUV3(
        poly,
        u0, v0,
        u1, v1,
        u2, v2
    );

    setTPage(poly, 2, 0, TEXTURE_ATLAS_X, TEXTURE_ATLAS_Y);
    setClut(poly, 0, 0);
}

static void draw_camera_textured_block_quad(
    RenderContext *context,
    const Vec3i *v0,
    const Vec3i *v1,
    const Vec3i *v2,
    const Vec3i *v3,
    int block_type,
    int face_type
) {
    ProjectedVertex p0;
    ProjectedVertex p1;
    ProjectedVertex p2;
    ProjectedVertex p3;

    const int tex_u0 = block_face_texture_u(block_type, face_type);
    const int tex_v0 = LOG_TILE_V;
    const int tex_u1 = tex_u0 + LOG_TILE_SIZE - 1;
    const int tex_v1 = tex_v0 + LOG_TILE_SIZE - 1;

    int depth_a;
    int depth_b;
    int ot_a;
    int ot_b;

    if (!face_is_safe_for_textured_quad(v0, v1, v2, v3)) {
        return;
    }

    project_camera_vertex(v0, &p0);
    project_camera_vertex(v1, &p1);
    project_camera_vertex(v2, &p2);
    project_camera_vertex(v3, &p3);

    /*
     * Do not render the whole face as one large textured quad primitive.
     * A single large quad is sorted by one average depth, which caused nearby
     * world triangles to overwrite parts of the log face.
     *
     * Two textured triangle primitives are still very cheap on PS1 and sort
     * much better.
     */
    depth_a = (v0->z + v1->z + v2->z) / 3;
    depth_b = (v0->z + v2->z + v3->z) / 3;

    ot_a = depth_to_ot(depth_a);
    ot_b = depth_to_ot(depth_b);

    draw_projected_textured_triangle(
        context,
        &p0,
        &p1,
        &p2,
        tex_u0, tex_v0,
        tex_u1, tex_v0,
        tex_u1, tex_v1,
        ot_a
    );

    draw_projected_textured_triangle(
        context,
        &p0,
        &p2,
        &p3,
        tex_u0, tex_v0,
        tex_u1, tex_v1,
        tex_u0, tex_v1,
        ot_b
    );
}

static void draw_mesh(RenderContext *context) {
    for (int i = 0; i < game_state.world.mesh_face_count; i++) {
        const MeshFace *face = &(game_state.world.mesh_faces[i]);

        if (face_center_z(face) <= NEAR_PLANE_Z) {
            continue;
        }

        if (face_is_outside_frustum(face)) {
            continue;
        }

        const Vec3i *v0 = &(game_state.world.camera_vertices[face->v[0]]);
        const Vec3i *v1 = &(game_state.world.camera_vertices[face->v[1]]);
        const Vec3i *v2 = &(game_state.world.camera_vertices[face->v[2]]);
        const Vec3i *v3 = &(game_state.world.camera_vertices[face->v[3]]);

        if (
            (face->block_type == BLOCK_LOG || face->block_type == BLOCK_PLANKS) &&
            face_is_safe_for_textured_quad(v0, v1, v2, v3)
        ) {
            draw_camera_textured_block_quad(
                context,
                v0,
                v1,
                v2,
                v3,
                face->block_type,
                face->face_type
            );
        } else {
            draw_camera_triangle_clipped(context, v0, v1, v2, face->r, face->g, face->b);
            draw_camera_triangle_clipped(context, v0, v2, v3, face->r, face->g, face->b);
        }
    }
}

static void draw_texture_speckles(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int z,
    int seed,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    for (int i = 0; i < 5; i++) {
        const int px = x + 2 + ((seed * 11 + i * 17) % (w - 4));
        const int py = y + 2 + ((seed * 7 + i * 13) % (h - 4));
        const int size = 2 + ((seed + i) & 1);

        draw_filled_rect(context, px, py, size, size, z, r, g, b);
    }
}

static void draw_minecraft_texture_block(
    RenderContext *context,
    int x,
    int y,
    int size,
    int type,
    int z,
    int seed
) {
    const int band = size / 4;

    switch (type) {
        case 0:
            draw_filled_rect(context, x, y, size, size, z + 1, 115, 72, 38);
            draw_filled_rect(context, x, y, size, band, z, 80, 175, 64);
            draw_filled_rect(context, x, y + band, size, 2, z, 70, 135, 48);
            draw_texture_speckles(context, x, y + band + 1, size, size - band - 1, z, seed, 82, 50, 28);
            draw_texture_speckles(context, x, y, size, band + 2, z, seed + 9, 118, 210, 82);
            break;

        case 1:
            draw_filled_rect(context, x, y, size, size, z + 1, 116, 72, 38);
            draw_texture_speckles(context, x, y, size, size, z, seed, 82, 50, 28);
            draw_texture_speckles(context, x, y, size, size, z, seed + 3, 145, 92, 48);
            break;

        case 2:
            draw_filled_rect(context, x, y, size, size, z + 1, 104, 106, 104);
            draw_texture_speckles(context, x, y, size, size, z, seed, 72, 74, 74);
            draw_texture_speckles(context, x, y, size, size, z, seed + 5, 138, 140, 138);
            break;

        case 3:
            draw_filled_rect(context, x, y, size, size, z + 1, 48, 126, 44);
            draw_texture_speckles(context, x, y, size, size, z, seed, 32, 92, 34);
            draw_texture_speckles(context, x, y, size, size, z, seed + 8, 82, 166, 64);
            break;

        case 4:
            draw_filled_rect(context, x, y, size, size, z + 1, 100, 68, 36);
            draw_filled_rect(context, x + 3, y, 3, size, z, 70, 44, 24);
            draw_filled_rect(context, x + size - 5, y, 2, size, z, 130, 88, 48);
            draw_texture_speckles(context, x, y, size, size, z, seed, 74, 46, 24);
            break;

        case 5:
            draw_filled_rect(context, x, y, size, size, z + 1, 42, 112, 190);
            draw_filled_rect(context, x, y + 3, size, 2, z, 70, 152, 220);
            draw_filled_rect(context, x, y + 9, size, 2, z, 34, 88, 160);
            break;

        case 6:
            draw_filled_rect(context, x, y, size, size, z + 1, 202, 184, 106);
            draw_texture_speckles(context, x, y, size, size, z, seed, 164, 142, 78);
            draw_texture_speckles(context, x, y, size, size, z, seed + 4, 232, 216, 136);
            break;

        case 7:
            draw_filled_rect(context, x, y, size, size, z + 1, 166, 108, 52);
            draw_filled_rect(context, x, y + (size / 3), size, 1, z, 104, 64, 32);
            draw_filled_rect(context, x, y + ((size * 2) / 3), size, 1, z, 104, 64, 32);
            draw_filled_rect(context, x + (size / 2), y, 1, size, z, 196, 142, 76);
            draw_texture_speckles(context, x, y, size, size, z, seed, 118, 76, 38);
            break;

        case 8:
        default:
            draw_filled_rect(context, x, y, size, size, z + 1, 132, 78, 36);
            draw_filled_rect(context, x + 2, y + 2, size - 4, size - 4, z, 168, 112, 54);
            draw_filled_rect(context, x + 4, y + 4, size - 8, size - 8, z, 96, 58, 28);
            draw_filled_rect(context, x + 5, y + 5, size - 10, size - 10, z, 188, 136, 68);
            draw_line(context, x + 3, y + (size / 2), x + size - 4, y + (size / 2), z, 78, 48, 24);
            draw_line(context, x + (size / 2), y + 3, x + (size / 2), y + size - 4, z, 78, 48, 24);
            break;
    }

    draw_line(context, x, y, x + size - 1, y, z, 230, 230, 230);
    draw_line(context, x, y, x, y + size - 1, z, 218, 218, 218);
    draw_line(context, x + size - 1, y, x + size - 1, y + size - 1, z, 34, 34, 34);
    draw_line(context, x, y + size - 1, x + size - 1, y + size - 1, z, 34, 34, 34);
}
