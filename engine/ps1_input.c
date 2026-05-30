/*
 * engine/ps1_input.c
 *
 * Low-level gamepad input.
 * Uses the existing game_state.input.pad_buffers storage from main.c in this transitional stage.
 */

#include "ps1_input.h"

#include <hwregs_c.h>

#define PAD_SIO_TIMEOUT 4096

static void pad_sio_delay(int cycles) {
    for (int i = 0; i < cycles; i++) {
        __asm__ volatile("");
    }
}

static int pad_sio_wait_rx(void) {
    for (int i = 0; i < PAD_SIO_TIMEOUT; i++) {
        if (SIO_STAT(0) & 0x0002) {
            return 1;
        }
    }

    return 0;
}

static void pad_sio_setup(void) {
    SIO_CTRL(0) = 0x0040;
    SIO_MODE(0) = 0x000d;
    SIO_BAUD(0) = 0x0088;
}

static int pad_sio_transfer(
    const uint8_t *tx,
    uint8_t *rx,
    int length
) {
    if (length <= 0) {
        return 0;
    }

    pad_sio_setup();

    /*
     * Pull /CS high briefly, then low for controller port 1.
     * This mirrors the PSn00bSDK io/pads SPI example timing, but does it
     * synchronously only during startup/config instead of taking over the bus.
     */
    SIO_CTRL(0) = 0x0010;
    pad_sio_delay(1000);

    SIO_CTRL(0) = 0x1003;
    pad_sio_delay(2000);

    for (int i = 0; i < length; i++) {
        SIO_DATA(0) = tx[i];

        if (!pad_sio_wait_rx()) {
            SIO_CTRL(0) = 0x0010;
            return 0;
        }

        rx[i] = (uint8_t)SIO_DATA(0);

        /*
         * Clear /ACK and keep /CS low between bytes.
         */
        SIO_CTRL(0) = 0x1013;
        pad_sio_delay(120);
    }

    SIO_CTRL(0) = 0x0010;
    pad_sio_delay(1000);

    return 1;
}

static int send_pad_config_command(
    uint8_t command,
    uint8_t arg1,
    uint8_t arg2,
    int request_config_padding
) {
    uint8_t tx[9];
    uint8_t rx[9];

    tx[0] = 0x01;
    tx[1] = command;
    tx[2] = 0x00;
    tx[3] = arg1;
    tx[4] = arg2;

    for (int i = 5; i < 9; i++) {
        tx[i] = request_config_padding ? 0xff : 0x00;
    }

    for (int i = 0; i < 9; i++) {
        rx[i] = 0x00;
    }

    return pad_sio_transfer(tx, rx, 9);
}

static void force_dualshock_analog_mode(void) {
    /*
     * BIOS InitPAD/StartPAD can read analog values only after the controller
     * is already in analog mode. DualShock pads often boot as digital, so we
     * send the same core config sequence used by the PSn00bSDK io/pads example:
     *
     * 1) enter config mode
     * 2) set analog mode
     * 3) request full response format
     * 4) leave config mode
     *
     * This is done once during startup, then BIOS pad polling is restarted.
     */
    send_pad_config_command(PAD_CMD_CONFIG_MODE, 0x01, 0x00, 0);
    send_pad_config_command(PAD_CMD_SET_ANALOG, 0x01, 0x02, 0);
    send_pad_config_command(PAD_CMD_REQUEST_CONFIG, 0x00, 0x01, 1);
    send_pad_config_command(PAD_CMD_CONFIG_MODE, 0x00, 0x00, 0);
}

void init_input(void) {
    InitPAD(
        game_state.input.pad_buffers[0],
        PAD_BUFFER_SIZE,
        game_state.input.pad_buffers[1],
        PAD_BUFFER_SIZE
    );

    StartPAD();
    ChangeClearPAD(1);

    /*
     * Give BIOS polling a moment to initialize, then temporarily stop it while
     * we send DualShock config commands manually over SIO. After that, restart
     * BIOS polling so memory-card BIOS APIs remain usable.
     */
    VSync(0);
    VSync(0);

    StopPAD();
    force_dualshock_analog_mode();

    StartPAD();
    ChangeClearPAD(1);

    VSync(0);
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

static int read_analog_axis(uint8_t value) {
    const int dead_zone = 24;
    const int centered = ((int)value) - 128;

    if (centered > -dead_zone && centered < dead_zone) {
        return 0;
    }

    if (centered < -128) {
        return -128;
    }

    if (centered > 127) {
        return 127;
    }

    return centered;
}

static int analog_axis_byte_looks_valid(uint8_t value) {
    /*
     * A resting PS1 analog stick is around 0x80.
     * Unused digital-pad bytes are usually 0x00 or 0xff.
     */
    return value > 8 && value < 248;
}

static int analog_pair_looks_valid(uint8_t x, uint8_t y) {
    return analog_axis_byte_looks_valid(x) && analog_axis_byte_looks_valid(y);
}

PadAnalogState read_pad_analog(void) {
    PADTYPE *pad = (PADTYPE *)game_state.input.pad_buffers[0];
    PadAnalogState state;

    const int pad_type_reports_analog =
        pad->type == PAD_ID_ANALOG_STICK ||
        pad->type == PAD_ID_ANALOG;

    const int pad_len_reports_sticks = pad->len >= 3;

    /*
     * Use PADTYPE fields directly.
     * psxpad.h defines these fields for analog controllers:
     *   rs_x, rs_y = right stick
     *   ls_x, ls_y = left stick
     */
    const int right_pair_valid = analog_pair_looks_valid(pad->rs_x, pad->rs_y);
    const int left_pair_valid = analog_pair_looks_valid(pad->ls_x, pad->ls_y);

    state.has_analog = 0;
    state.left_x = 0;
    state.left_y = 0;
    state.right_x = 0;
    state.right_y = 0;

    if (pad->stat != 0) {
        return state;
    }

    if (
        !pad_type_reports_analog &&
        !pad_len_reports_sticks &&
        !right_pair_valid &&
        !left_pair_valid
    ) {
        return state;
    }

    state.has_analog = 1;

    if (right_pair_valid || pad_type_reports_analog || pad_len_reports_sticks) {
        state.right_x = read_analog_axis(pad->rs_x);
        state.right_y = read_analog_axis(pad->rs_y);
    }

    if (left_pair_valid || pad_type_reports_analog || pad_len_reports_sticks) {
        state.left_x = read_analog_axis(pad->ls_x);
        state.left_y = read_analog_axis(pad->ls_y);
    }

    return state;
}
