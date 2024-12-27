#ifndef LAMP_H_
#define LAMP_H_

// PWM period is 4096, so having the output compare 
// value be 4096 makes the duty cycle be 0%
extern const uint16_t LAMP_MIN_BRIGHTNESS;
extern const uint16_t LAMP_MAX_BRIGHTNESS;

enum LAMP_EVENT{
	LAMP_EVENT_NONE,
	LAMP_EVENT_BRIGHTNESS_INC,
	LAMP_EVENT_BRIGHTNESS_DEC,
	LAMP_EVENT_BRIGHTNESS_MAX,
	LAMP_EVENT_BRIGHTNESS_MIN
};

extern uint32_t alarms[7];

// Time tracking
extern const uint32_t DAY_LENGTH;

void StartFading(uint32_t fade_length, uint16_t fade_start_brightness, uint16_t fade_end_brightness);

#endif
