#include "utility.h"
#include <stdlib.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t len){
	char *d = dst;
	const char *s = src;

	for(size_t i = 0; i < len; i++){
		d[i] = s[i];
	}

	return dst;
}

void *memset(void *dst, int value, size_t len){
	char *d = dst;
	for(size_t i = 0; i < len; i++){
		d[i] = value;
	}
	return dst;
}

int16_t custom_sin(uint8_t x){
	static const int16_t sin_lut[64] = {
		0, 804, 1607, 2410, 3211, 4011, 4807, 5601, 
		6392, 7179, 7961, 8739, 9511, 10278, 11038, 11792, 
		12539, 13278, 14009, 14732, 15446, 16150, 16845, 17530, 
		18204, 18867, 19519, 20159, 20787, 21402, 22004, 22594, 
		23169, 23731, 24278, 24811, 25329, 25831, 26318, 26789, 
		27244, 27683, 28105, 28510, 28897, 29268, 29621, 29955, 
		30272, 30571, 30851, 31113, 31356, 31580, 31785, 31970, 
		32137, 32284, 32412, 32520, 32609, 32678, 32727, 32767
	};

    int16_t value = sin_lut[x % 64];
    if(x >= 192){
        value = -sin_lut[63 - x % 64];
    }else if(x >= 128){
        value = -sin_lut[x % 64];
    }else if(x >= 64){
        value = sin_lut[63 - x % 64];
    }
    return value;
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}