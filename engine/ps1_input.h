#ifndef MCPSX_ENGINE_PS1_INPUT_H
#define MCPSX_ENGINE_PS1_INPUT_H

#include <stdint.h>

typedef struct {
    int has_analog;

    /*
     * Axis values are centered around zero:
     * -128..127-ish after deadzone filtering.
     *
     * Y axis follows raw PS1 convention after centering:
     * negative = up, positive = down.
     */
    int left_x;
    int left_y;
    int right_x;
    int right_y;
} PadAnalogState;

void init_input(void);
uint16_t read_pad_buttons(void);
PadAnalogState read_pad_analog(void);

#endif
