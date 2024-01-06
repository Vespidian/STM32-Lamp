#ifndef UTILITY_H_
#define UTILITY_H_

#include <stdlib.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);

/**
 * @brief Integer implementation of mathematical sine
 * @param x Integer between 0 and 256
 * @return Signed 16-bit value between 32767 and -32767
*/
int16_t custom_sin(uint8_t x);

long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif