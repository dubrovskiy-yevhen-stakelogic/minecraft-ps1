/*
 * engine/fixed_math.c
 *
 * Extracted from main.c without logic changes.
 * Transitional stage: included from main.c so CMake does not need changes yet.
 */

#include "fixed_math.h"
#include <stdint.h>

static const int16_t sin_table[64] = {
    0, 100, 200, 297, 391, 483, 569, 650,
    724, 792, 851, 903, 946, 980, 1004, 1019,
    1024, 1019, 1004, 980, 946, 903, 851, 792,
    724, 650, 569, 483, 391, 297, 200, 100,
    0, -100, -200, -297, -391, -483, -569, -650,
    -724, -792, -851, -903, -946, -980, -1004, -1019,
    -1024, -1019, -1004, -980, -946, -903, -851, -792,
    -724, -650, -569, -483, -391, -297, -200, -100
};

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

int iabs(int value) {
    return value < 0 ? -value : value;
}

int isin(int angle) {
    angle &= ANGLE_MASK;

    const int index = (angle >> ANGLE_FRAC_BITS) & 63;
    const int frac = angle & ((1 << ANGLE_FRAC_BITS) - 1);

    const int a = sin_table[index];
    const int b = sin_table[(index + 1) & 63];

    return a + (((b - a) * frac) >> ANGLE_FRAC_BITS);
}

int icos(int angle) {
    return isin(angle + ANGLE_QUARTER);
}

int floor_div(int value, int divisor) {
    if (value >= 0) {
        return value / divisor;
    }

    return -(((-value) + divisor - 1) / divisor);
}
