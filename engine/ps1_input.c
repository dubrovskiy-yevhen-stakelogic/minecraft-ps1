/*
 * engine/ps1_input.c
 *
 * Low-level gamepad input.
 * Uses the existing game_state.input.pad_buffers storage from main.c in this transitional stage.
 */

#include "ps1_input.h"

void init_input(void) {
    InitPAD(
        game_state.input.pad_buffers[0],
        PAD_BUFFER_SIZE,
        game_state.input.pad_buffers[1],
        PAD_BUFFER_SIZE
    );

    StartPAD();
    ChangeClearPAD(1);
}

uint16_t read_pad_buttons(void) {
    PADTYPE *pad = (PADTYPE *)game_state.input.pad_buffers[0];

    if (pad->stat != 0) {
        return 0;
    }

    if (
        pad->type != PAD_ID_DIGITAL &&
        pad->type != PAD_ID_ANALOG_STICK &&
        pad->type != PAD_ID_ANALOG
    ) {
        return 0;
    }

    /*
     * PS1 pad buttons are active-low:
     * bit = 0 means pressed.
     * Invert it so pressed buttons become 1.
     */
    return (uint16_t)(~pad->btn);
}
