/*
 * ui/menu.c
 *
 * Main menu and pause menu UI/input.
 * Transitional stage: included directly from main.c, so CMake does not need changes yet.
 */

#include "menu.h"

static void draw_menu_cloud(RenderContext *context, int x, int y, int z) {
    draw_filled_rect(context, x, y + 8, 54, 10, z, 244, 248, 255);
    draw_filled_rect(context, x + 12, y, 18, 20, z, 244, 248, 255);
    draw_filled_rect(context, x + 28, y + 4, 24, 16, z, 244, 248, 255);
    draw_filled_rect(context, x + 50, y + 10, 18, 8, z, 244, 248, 255);
}


static void draw_menu_tree(RenderContext *context, int x, int y, int block, int z) {
    draw_minecraft_texture_block(context, x + block, y + block, block, 4, z, x + y + 1);
    draw_minecraft_texture_block(context, x + block, y + block * 2, block, 4, z, x + y + 2);

    draw_minecraft_texture_block(context, x, y, block, 3, z, x + y + 3);
    draw_minecraft_texture_block(context, x + block, y, block, 3, z, x + y + 4);
    draw_minecraft_texture_block(context, x + block * 2, y, block, 3, z, x + y + 5);
    draw_minecraft_texture_block(context, x, y + block, block, 3, z, x + y + 6);
    draw_minecraft_texture_block(context, x + block, y + block, block, 3, z, x + y + 7);
    draw_minecraft_texture_block(context, x + block * 2, y + block, block, 3, z, x + y + 8);
}


static void draw_minecraft_button(
    RenderContext *context,
    int x,
    int y,
    int w,
    int h,
    int selected
) {
    if (selected) {
        draw_filled_rect(context, x + 2, y + 2, w, h, 4, 18, 18, 18);
        draw_filled_rect(context, x, y, w, h, 3, 160, 164, 160);
        draw_line(context, x, y, x + w - 1, y, 2, 252, 252, 252);
        draw_line(context, x, y, x, y + h - 1, 2, 252, 252, 252);
        draw_line(context, x + w - 1, y, x + w - 1, y + h - 1, 2, 64, 64, 64);
        draw_line(context, x, y + h - 1, x + w - 1, y + h - 1, 2, 64, 64, 64);
        draw_filled_rect(context, x + 4, y + 4, w - 8, h - 8, 2, 126, 132, 126);
    } else {
        draw_filled_rect(context, x + 2, y + 2, w, h, 4, 14, 14, 14);
        draw_filled_rect(context, x, y, w, h, 3, 106, 108, 106);
        draw_line(context, x, y, x + w - 1, y, 2, 206, 206, 206);
        draw_line(context, x, y, x, y + h - 1, 2, 206, 206, 206);
        draw_line(context, x + w - 1, y, x + w - 1, y + h - 1, 2, 42, 42, 42);
        draw_line(context, x, y + h - 1, x + w - 1, y + h - 1, 2, 42, 42, 42);
        draw_filled_rect(context, x + 4, y + 4, w - 8, h - 8, 2, 86, 88, 86);
    }
}


static void draw_menu_background(RenderContext *context) {
    const int block = 16;

    draw_filled_rect(context, 0, 0, SCREEN_W, 58, 9, 86, 152, 224);
    draw_filled_rect(context, 0, 58, SCREEN_W, 46, 9, 108, 176, 236);
    draw_filled_rect(context, 0, 104, SCREEN_W, 32, 9, 130, 194, 242);

    draw_filled_rect(context, 248, 18, 32, 32, 8, 248, 226, 112);
    draw_filled_rect(context, 252, 22, 24, 24, 7, 255, 242, 160);

    draw_menu_cloud(context, 18, 52, 7);
    draw_menu_cloud(context, 202, 62, 7);

    for (int col = 0; col < 20; col++) {
        const int x = col * block;
        int top = 132;

        if (col == 3 || col == 4 || col == 5) {
            top = 116;
        } else if (col == 6 || col == 7 || col == 8 || col == 9) {
            top = 124;
        } else if (col == 14 || col == 15 || col == 16) {
            top = 148;
        } else if (col >= 17) {
            top = 140;
        }

        if (col >= 11 && col <= 13) {
            draw_minecraft_texture_block(context, x, 148, block, 6, 6, col + 1);
            draw_minecraft_texture_block(context, x, 164, block, 5, 6, col + 2);
            draw_minecraft_texture_block(context, x, 180, block, 5, 6, col + 3);
            draw_minecraft_texture_block(context, x, 196, block, 1, 6, col + 4);
            draw_minecraft_texture_block(context, x, 212, block, 1, 6, col + 5);
            continue;
        }

        draw_minecraft_texture_block(context, x, top, block, 0, 6, col + 3);

        for (int y = top + block; y < SCREEN_H; y += block) {
            if (y >= top + block * 3 && (col == 1 || col == 2 || col == 18)) {
                draw_minecraft_texture_block(context, x, y, block, 2, 6, col + y);
            } else {
                draw_minecraft_texture_block(context, x, y, block, 1, 6, col + y);
            }
        }
    }

    draw_menu_tree(context, 38, 84, block, 5);
    draw_menu_tree(context, 226, 100, block, 5);

    draw_minecraft_texture_block(context, 152, 112, block, 2, 5, 31);
    draw_minecraft_texture_block(context, 168, 112, block, 2, 5, 32);
    draw_minecraft_texture_block(context, 152, 128, block, 2, 5, 33);
    draw_minecraft_texture_block(context, 168, 128, block, 2, 5, 34);
}


static void draw_menu_option(
    RenderContext *context,
    int x,
    int y,
    int w,
    const char *label,
    int selected
) {
    draw_minecraft_button(context, x, y, w, 24, selected);

    if (selected) {
        draw_text(context, x + 18, y + 7, 0, "> ");
        draw_text(context, x + 36, y + 7, 0, label);
    } else {
        draw_text(context, x + 36, y + 7, 0, label);
    }
}


static void draw_pause_option(
    RenderContext *context,
    int x,
    int y,
    int w,
    const char *label,
    int selected
) {
    draw_minecraft_button(context, x, y, w, 19, selected);

    if (selected) {
        draw_text(context, x + 14, y + 5, 0, "> ");
        draw_text(context, x + 30, y + 5, 0, label);
    } else {
        draw_text(context, x + 30, y + 5, 0, label);
    }
}


static void update_menu_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if (pressed_this_frame & PAD_UP) {
        game_state.app.menu_selected_option--;

        if (game_state.app.menu_selected_option < 0) {
            game_state.app.menu_selected_option = MENU_OPTION_COUNT - 1;
        }
    }

    if (pressed_this_frame & PAD_DOWN) {
        game_state.app.menu_selected_option++;

        if (game_state.app.menu_selected_option >= MENU_OPTION_COUNT) {
            game_state.app.menu_selected_option = 0;
        }
    }

    if (pressed_this_frame & PAD_LEFT) {
        game_state.app.menu_selected_option = MENU_OPTION_NEW_GAME;
    }

    if (pressed_this_frame & PAD_RIGHT) {
        game_state.app.menu_selected_option = MENU_OPTION_LOAD_GAME;
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE) ||
        (pressed_this_frame & PAD_START)
    ) {
        if (game_state.app.menu_selected_option == MENU_OPTION_NEW_GAME) {
            start_new_game();
        } else {
            start_loaded_game();
        }
    }

    pad_previous_buttons = buttons;
}


static void update_pause_input(void) {
    const uint16_t buttons = read_pad_buttons();
    const uint16_t pressed_this_frame = buttons & ~pad_previous_buttons;

    if ((pressed_this_frame & PAD_START) || (pressed_this_frame & PAD_TRIANGLE)) {
        game_state.app.app_state = APP_STATE_PLAY;
        pad_previous_buttons = buttons;
        return;
    }

    if (pressed_this_frame & PAD_UP) {
        game_state.app.pause_selected_option--;

        if (game_state.app.pause_selected_option < 0) {
            game_state.app.pause_selected_option = PAUSE_OPTION_COUNT - 1;
        }
    }

    if (pressed_this_frame & PAD_DOWN) {
        game_state.app.pause_selected_option++;

        if (game_state.app.pause_selected_option >= PAUSE_OPTION_COUNT) {
            game_state.app.pause_selected_option = 0;
        }
    }

    if (
        (pressed_this_frame & PAD_CROSS) ||
        (pressed_this_frame & PAD_CIRCLE)
    ) {
        switch (game_state.app.pause_selected_option) {
            case PAUSE_OPTION_RESUME:
                game_state.app.app_state = APP_STATE_PLAY;
                break;

            case PAUSE_OPTION_INVENTORY:
                game_state.app.app_state = APP_STATE_INVENTORY;
                inventory_cursor_slot = INVENTORY_CURSOR_STORAGE_START;
                set_system_status("INVENTORY", 45);
                break;

            case PAUSE_OPTION_TOGGLE_FLY:
                game_state.player.fly_mode_enabled = !game_state.player.fly_mode_enabled;

                if (!game_state.player.fly_mode_enabled) {
                    snap_camera_to_ground();
                    set_system_status("MODE WALK", 90);
                } else {
                    set_system_status("MODE FLY", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_HUD:
                game_state.app.hud_visible = !game_state.app.hud_visible;

                if (game_state.app.hud_visible) {
                    set_system_status("HELP HUD ON", 90);
                } else {
                    set_system_status("HELP HUD OFF", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_FOG:
                game_state.app.fog_enabled = !game_state.app.fog_enabled;
                update_clear_color_for_game();

                if (game_state.app.fog_enabled) {
                    set_system_status("FOG ON", 90);
                } else {
                    set_system_status("FOG OFF", 90);
                }
                break;

            case PAUSE_OPTION_TOGGLE_AUTOJUMP:
                game_state.player.autojump_enabled = !game_state.player.autojump_enabled;

                if (game_state.player.autojump_enabled) {
                    set_system_status("AUTOJUMP ON", 90);
                } else {
                    set_system_status("AUTOJUMP OFF", 90);
                }
                break;

            case PAUSE_OPTION_SAVE_GAME:
                if (save_game_to_memory_card()) {
                    set_system_status("SAVE OK", 90);
                } else {
                    set_system_status("SAVE FAILED", 120);
                }
                break;

            case PAUSE_OPTION_LOAD_GAME:
                if (load_game_from_memory_card()) {
                    reset_inventory_items();
                    reset_dropped_items();
                    reset_block_breaking();
                    game_state.app.app_state = APP_STATE_PLAY;
                    set_system_status("LOAD OK", 90);
                } else {
                    set_system_status("LOAD FAILED", 120);
                }
                break;

            case PAUSE_OPTION_RETURN_MENU:
            default:
                game_state.app.app_state = APP_STATE_MENU;
                game_state.app.menu_selected_option = MENU_OPTION_NEW_GAME;
                game_state.app.pause_selected_option = PAUSE_OPTION_RESUME;
                set_system_status("MAIN MENU", 90);
                break;
        }
    }

    pad_previous_buttons = buttons;
}


static void draw_menu(RenderContext *context) {
    draw_menu_background(context);

    draw_text(context, 93, 22, 1, "MINECRAFT PS1");
    draw_text(context, 91, 20, 0, "MINECRAFT PS1");
    draw_text(context, 72, 38, 0, "PLAYSTATION STYLE DEMAKE");

    draw_menu_option(
        context,
        90,
        82,
        140,
        "NEW GAME",
        game_state.app.menu_selected_option == MENU_OPTION_NEW_GAME
    );
    draw_menu_option(
        context,
        90,
        112,
        140,
        "LOAD GAME",
        game_state.app.menu_selected_option == MENU_OPTION_LOAD_GAME
    );

    draw_minecraft_button(context, 32, 206, 256, 20, 0);
    draw_text(context, 50, 211, 0, "UP/DOWN CHOOSE  CROSS/CIRCLE/START OK");

    if (game_state.app.system_status_timer > 0) {
        draw_minecraft_button(context, 102, 178, 116, 18, 0);
        draw_text(context, 120, 182, 0, game_state.app.system_status_text);
    }
}


static void draw_pause_menu(RenderContext *context) {
    draw_filled_rect(context, 0, 0, SCREEN_W, SCREEN_H, 7, 18, 18, 22);

    draw_minecraft_texture_block(context, 24, 20, 16, 2, 6, 100);
    draw_minecraft_texture_block(context, 40, 20, 16, 2, 6, 101);
    draw_minecraft_texture_block(context, 56, 20, 16, 2, 6, 102);
    draw_minecraft_texture_block(context, 248, 20, 16, 1, 6, 103);
    draw_minecraft_texture_block(context, 264, 20, 16, 0, 6, 104);
    draw_minecraft_texture_block(context, 280, 20, 16, 0, 6, 105);

    draw_text(context, 122, 20, 0, "GAME MENU");

    draw_pause_option(
        context,
        74,
        32,
        172,
        "BACK TO GAME",
        game_state.app.pause_selected_option == PAUSE_OPTION_RESUME
    );
    draw_pause_option(
        context,
        74,
        52,
        172,
        "INVENTORY",
        game_state.app.pause_selected_option == PAUSE_OPTION_INVENTORY
    );
    draw_pause_option(
        context,
        74,
        72,
        172,
        game_state.player.fly_mode_enabled ? "SWITCH TO WALK" : "SWITCH TO FLY",
        game_state.app.pause_selected_option == PAUSE_OPTION_TOGGLE_FLY
    );
    draw_pause_option(
        context,
        74,
        92,
        172,
        game_state.app.hud_visible ? "HELP HUD: ON" : "HELP HUD: OFF",
        game_state.app.pause_selected_option == PAUSE_OPTION_TOGGLE_HUD
    );
    draw_pause_option(
        context,
        74,
        112,
        172,
        game_state.app.fog_enabled ? "FOG: ON" : "FOG: OFF",
        game_state.app.pause_selected_option == PAUSE_OPTION_TOGGLE_FOG
    );
    draw_pause_option(
        context,
        74,
        132,
        172,
        game_state.player.autojump_enabled ? "AUTOJUMP: ON" : "AUTOJUMP: OFF",
        game_state.app.pause_selected_option == PAUSE_OPTION_TOGGLE_AUTOJUMP
    );
    draw_pause_option(
        context,
        74,
        152,
        172,
        "SAVE GAME",
        game_state.app.pause_selected_option == PAUSE_OPTION_SAVE_GAME
    );
    draw_pause_option(
        context,
        74,
        172,
        172,
        "LOAD GAME",
        game_state.app.pause_selected_option == PAUSE_OPTION_LOAD_GAME
    );
    draw_pause_option(
        context,
        74,
        192,
        172,
        "MAIN MENU",
        game_state.app.pause_selected_option == PAUSE_OPTION_RETURN_MENU
    );

    draw_text(context, 70, 216, 0, "UP/DOWN CHOOSE  CROSS/CIRCLE OK");
    draw_text(context, 86, 228, 0, "START/TRIANGLE BACK");

    if (game_state.app.system_status_timer > 0) {
        draw_minecraft_button(context, 104, 4, 112, 18, 0);
        draw_text(context, 122, 8, 0, game_state.app.system_status_text);
    }
}
