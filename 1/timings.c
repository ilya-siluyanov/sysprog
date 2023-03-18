//
// Created by Илья Силуянов on 18.03.2023.
//
#include <time.h>
#include "types.h"

ld get_microsec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    return (ld) ts.tv_sec * 1000000 + (ld) ts.tv_nsec * 1.0 / 1000;
}
