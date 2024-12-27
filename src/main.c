#include <stdlib.h>
#include <libopencm3/stm32/f1/bkp.h>
#include "utility.h"


extern unsigned int _data;
extern unsigned int _edata;
extern unsigned int _data_loadaddr;

extern unsigned int _bss;
extern unsigned int _ebss;

int main(void);
void reset_handler(void){
	// Copy .data section to memory and zero out .bss section
	memcpy(&_data, &_data_loadaddr, sizeof(size_t) * (&_edata - &_data));
	memset(&_bss, 0, &_ebss - &_bss);

	// Entrypoint into the main function
	main();
}

/** --- CODE START HERE --- **/

#include "global.h"

#include <stdbool.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/pwr.h>

#include "usart.h"
#include "ir.h"
#include "ir_interface.h"
#include "terminal.h"
#include "lamp.h"


/**
 * ISSUES:
 * When turning on, sometimes it wont fade and just turn on to max after a delay
 * ^ Looks like this may be a problem with the LED driver chip itself ^
 * Lamp turns on at initial startup
*/

/** --- LAMP VARIABLES --- **/

// PWM period is 4096, so having the output compare 
// value be 4096 makes the duty cycle be 0%
const uint16_t LAMP_MIN_BRIGHTNESS = 4095;
// const uint16_t LAMP_MAX_BRIGHTNESS = 0;
const uint16_t LAMP_MAX_BRIGHTNESS = 2048;

// GPIOS
const uint32_t LAMP_GPIO_DIM_PORT = GPIOA;
const uint32_t LAMP_GPIO_DIM_PIN = GPIO8;
const uint32_t LAMP_GPIO_EN_PORT = GPIOA;
const uint32_t LAMP_GPIO_EN_PIN = GPIO11;

typedef enum LAMP_STATES{
	LAMP_ON,
	LAMP_OFF,
	LAMP_TURN_ON,
	LAMP_TURN_OFF,
	LAMP_FADING,
}LAMP_STATES;

typedef enum LAMP_DIM_STATES{
	LAMP_DIM_POTENTIOMETER,
	LAMP_DIM_REMOTE,
	LAMP_DIM_NODIM,
}LAMP_DIM_STATES;

// Lamp state
LAMP_STATES lamp_state = LAMP_OFF;
LAMP_DIM_STATES lamp_dim_state = LAMP_DIM_POTENTIOMETER;
bool lamp_on = false;
bool previous_button_state = false;
bool button_state = false;
uint16_t lamp_brightness = LAMP_MIN_BRIGHTNESS;

bool lamp_ev_ir_onbutton = false;
bool lamp_ev_alarm = false;
enum LAMP_EVENT lamp_ev_ir_brightness = LAMP_EVENT_NONE;

static const uint16_t pot_val_array_size = 16;
static uint16_t pot_val_array[16] = {0};
static uint16_t pot_val_index = 0;

// Fading
static const uint16_t fade_duration_default = 1000;
static uint16_t fade_start_val = 0;
static uint16_t fade_end_val = 0;
static uint16_t fade_current_val = 0;
static uint32_t fade_count = 0;
static uint32_t fade_duration = 2000;

// Time tracking
const uint32_t DAY_LENGTH = 86400;

// The RTC counter register is 32-bits, so conceivably 
// this could count days for 136 years continuously

/**
 * Alarmless day:
 * - Set alarm for RTC_CNT + DAY_LENGTH
 * 
 * Alarmed day:
 * - Set alarm for RTC_CNT + alarms[current_day]
 * - Set alarm for RTC_CNT + (DAY_LENGTH - alarms[current_day])
 * - Fade light on over 'sunrise_length'
 * 		- map time from (0, sunrise_length) to (0, 128) for fitting the sine curve
 * - Keep light on for 'sunrise_length'
 * - Fade light off over 'sunrise_length'
*/

/**
 * Alarm Times:
 * Zero means light will not auto turn on
 * Order: Mon Tue Wed Thr Fri Sat Sun
*/
uint32_t alarms[7] = {0, 0, 0, 0, 0, 0, 18000};
// uint32_t alarms[7] = {0};
uint16_t current_day = 0;
uint32_t sunrise_length = 3600000; // Fade in over the course of 1 hour
bool alarm_triggered = false;
bool alarm_set = false;

uint16_t loop_counter = 0;

bool tick = false;


void systick_setup(void){
	// 9999 means tick once every 10 ms
	// 8 MHz base clock divided by 8 means 1 MHz into systick
	// Count up to 1 MHz / 10k = 10 ms per tick
	systick_set_reload(9999);
	systick_clear();

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);

	systick_interrupt_enable();

	systick_counter_enable();
}

void sys_tick_handler(){
	tick = true;
}

static void adc_setup(void){
	rcc_periph_clock_enable(RCC_ADC1);

	// Make sure the ADC doesn't run during config
	adc_power_off(ADC1);
	adc_enable_eoc_interrupt(ADC1);

	// We configure everything for one single conversion
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_239DOT5CYC);

	adc_power_on(ADC1);

	// Wait for ADC starting up (10 ms)
	for (int i = 0; i < 80000; i++){
		__asm__("nop");
	}

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);

	uint8_t channel_array[16];
	channel_array[0] = 1; // pin PA1
	adc_set_regular_sequence(ADC1, 1, channel_array);
}

void adc_on(void){
	if (!(ADC_CR2(ADC1) & ADC_CR2_ADON)){
		adc_power_on(ADC1);

		// Wait for ADC starting up (1 ms)
		for (int i = 0; i < 8000; i++){
			__asm__("nop");
		}

		adc_reset_calibration(ADC1);
		adc_calibrate(ADC1);
	}
}

void pwm_setup(void){
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_GPIOA);
	
	rcc_periph_clock_enable(RCC_AFIO);
	gpio_set_mode(LAMP_GPIO_DIM_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, LAMP_GPIO_DIM_PIN);
	
	rcc_periph_clock_enable(RCC_TIM1);
	timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1, TIM_CR1_DIR_UP);
	timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_PWM2);
	timer_enable_oc_output(TIM1, TIM_OC1);
	timer_enable_break_main_output(TIM1);

	// Chose 4096 since the ADC is 12 bit and we can directly 
	// take a value from the ADC and place it into this OC register
	timer_set_oc_value(TIM1, TIM_OC1, 4095); // duty cycle
	timer_set_period(TIM1, 4096); // period
}

void rtc_setup(void){
	nvic_enable_irq(NVIC_RTC_IRQ);
	nvic_set_priority(NVIC_RTC_IRQ, 1);

	if(!rcc_rtc_clock_enabled_flag()){
		rtc_awake_from_off(RCC_LSE);
	}else{
		rtc_awake_from_standby();
	}

	pwr_disable_backup_domain_write_protect();
	// Read weekly alarm values from persistent BACKUP registers
	for(int i = 0; i < 7; i++){
		alarms[i] = *(uint16_t *)(0x40006c04 + (i * 0x04)) * 60; // Stored as minutes in backup register, so we must convert
	}

	// Recalculate the current day and alarm time from the RTC counter register
	uint32_t current_time = rtc_get_counter_val();
	current_day = current_time / DAY_LENGTH;

	// Calculate the alarm time by finding the start 
	// of the day and adding the alarm time to it
	uint32_t alarm_time = current_time - (current_time % DAY_LENGTH) + alarms[current_day % 7];
	
	// If we're already past the current day's 
	// alarm, we set the alarm for end of day
	alarm_triggered = false;
	alarm_set = false;
	if(alarm_time < current_time){
		rtc_set_alarm_time(current_time - (current_time % DAY_LENGTH) + DAY_LENGTH);
	}else{
		rtc_set_alarm_time(alarm_time);
		alarm_set = true;
	}

	// RTC crystal is 32,768 kHz and we want a 1 Hz isr
	rtc_set_prescale_val(32766);

	rtc_interrupt_enable(RTC_ALR);
}

void rtc_isr(void){
	// The interrupt flags aren't cleared by hardware, we have to do it
	rtc_clear_flag(RTC_ALR);

	alarm_triggered = true;

}

void StartFading(uint32_t fade_length, uint16_t fade_start_brightness, uint16_t fade_end_brightness){
	fade_start_val = fade_start_brightness;
	fade_end_val = fade_end_brightness;

	timer_set_oc_value(TIM1, TIM_OC1, fade_start_brightness);

	fade_count = 0;
	fade_duration = fade_length;
}

uint16_t GetPotSample(){
	uint16_t pot_val;

	// Turn the ADC on if its off
	adc_on();

	// Get a sample of the potentiometer to know the final brightness
	adc_start_conversion_direct(ADC1);
	while (!(ADC_SR(ADC1) & ADC_SR_EOC));

	//pot_val = ADC_DR(ADC1);
	pot_val_array[pot_val_index++] = ADC_DR(ADC1);
	if(pot_val_index >= pot_val_array_size){
		pot_val_index = 0;
	}
	for(int i = 0; i < pot_val_array_size; i++){
		pot_val += pot_val_array[i];
	}
	pot_val /= pot_val_array_size;

	return pot_val;
}

int16_t GetPotDelta(){
	int16_t prev_pot_val;

	GetPotSample();

	if(pot_val_index == 0){
		prev_pot_val = pot_val_array[pot_val_array_size - 1];
	}else{
		prev_pot_val = pot_val_array[pot_val_index - 1];
	}

	return (int16_t)(pot_val_array[pot_val_index]) - prev_pot_val;
}

uint16_t tmp(int8_t dir, uint16_t current_brightness){
	uint16_t scale[20] = {28,36,47,62,80,104,135,176,228,297,386,502,652,848,1102,1433,1863,2422,3148,4093}; // Precomputed lookup table for exponential brightness scale
	int index = 0;
	int smallest_dif = 4096;
	for(int i = 0; i < 20; i++){
		if(abs(current_brightness - scale[i]) < smallest_dif){
			smallest_dif = abs(current_brightness - scale[i]);
			index = i;
		}
	}

	uint16_t b = scale[index];

	if(dir < 1 && index != 0){
		b = scale[index - 1];
	}else if(index != 19){
		b = scale[index + 1];
	}

	return b;
}

void LampCheckRemote(){
	if(lamp_ev_ir_brightness != LAMP_EVENT_NONE){
		lamp_dim_state = LAMP_DIM_REMOTE;

		// TODO: Make scale logarithmic / exponential so we have fine control over small brightnesses
		// uint16_t scale[20] = {28,36,47,62,80,104,135,176,228,297,386,502,652,848,1102,1433,1863,2422,3148,4093}; // Precomputed lookup table for exponential brightness scale

				// OLD
				// uint16_t brightness_increment = 200;
				// if(lamp_ev_ir_brightness < 0){
				// 	brightness_increment = -brightness_increment;
				// }
				// lamp_brightness += brightness_increment;
		uint16_t prev_brightness = lamp_brightness;


				// NEW
				uint16_t brightness_increment = 200;
				switch(lamp_ev_ir_brightness){
					case LAMP_EVENT_BRIGHTNESS_INC:
						lamp_brightness += brightness_increment;
					break;

					case LAMP_EVENT_BRIGHTNESS_DEC:
						lamp_brightness -= brightness_increment;
					break;

					case LAMP_EVENT_BRIGHTNESS_MAX:
						lamp_brightness = LAMP_MAX_BRIGHTNESS;
					break;

					case LAMP_EVENT_BRIGHTNESS_MIN:
						lamp_brightness = LAMP_MIN_BRIGHTNESS - 50;
					break;
					default:
					break;
				}


		// tmp(lamp_ev_ir_brightness, lamp_brightness);
		StartFading(200, prev_brightness, lamp_brightness);
		lamp_state = LAMP_FADING;

		lamp_ev_ir_brightness = LAMP_EVENT_NONE;
	}
}

extern bool lamp_ev_ir_onbutton;

int main(void){
	rcc_periph_clock_enable(RCC_GPIOA);

	IRSetup();

	systick_setup();
	pwm_setup();
	adc_setup();
	USARTInit();

	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
	gpio_set(GPIOC, GPIO13);
	rtc_setup();

	/**
	 * PA1:		Potentiometer		Input	AF (ADC1)
	 * PA5:		On / Off Button		Input	GP
	 * PA8:		LED PWM				Output	AF (Timer 1)
	 * PA11:	LED Enable pin		Output	GP
	*/

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO1);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO5);
	gpio_set_mode(LAMP_GPIO_DIM_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, LAMP_GPIO_DIM_PIN);
	gpio_set_mode(LAMP_GPIO_EN_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LAMP_GPIO_EN_PIN);

	// Set PA5 to pullup
	GPIO_ODR(GPIOA) |= (1 << 5);
	button_state = gpio_get(GPIOA, GPIO5);
	previous_button_state = button_state;

	// Off by default
	lamp_on = false;
	timer_disable_counter(TIM1);
	gpio_clear(LAMP_GPIO_DIM_PORT, LAMP_GPIO_DIM_PIN); 
	gpio_clear(LAMP_GPIO_EN_PORT, LAMP_GPIO_EN_PIN);


	lamp_state = LAMP_OFF;
	lamp_dim_state = LAMP_DIM_POTENTIOMETER;

	while (1) {
		if(alarm_triggered){
			alarm_triggered = false;
			if(alarm_set){
				// ALARM (SUNRISE TIME)

				alarm_set = false;
				uint32_t current_time = rtc_get_counter_val();
				rtc_set_alarm_time(current_time - (current_time % DAY_LENGTH) + DAY_LENGTH);

				lamp_ev_alarm = true;

			}else{
				// END OF DAY

				current_day++;
				if(alarms[current_day % 7] == 0){
					
					// Next day has no alarm set (set the alarm to midnight)
					rtc_set_alarm_time(rtc_get_counter_val() + DAY_LENGTH);
				}else{
					
					// Next day HAS an alarm, initialize it
					rtc_set_alarm_time(rtc_get_counter_val() + alarms[current_day % 7]);
					alarm_set = true;
				}
			}
		}


		if(tick){
			tick = false;

			IRCheckCommands();
			Terminal();
			
			// Read the current button state
			button_state = gpio_get(GPIOA, GPIO5);
			bool button_pressed = (button_state == true && previous_button_state == false);

			switch(lamp_state){
				case LAMP_ON:

					// 
					switch(lamp_dim_state){
						case LAMP_DIM_POTENTIOMETER:
							lamp_brightness = GetPotSample();
						break;
						
						case LAMP_DIM_REMOTE:
							int8_t pot_threshold = 40;
							if(abs(GetPotDelta()) > pot_threshold){
								lamp_dim_state = LAMP_DIM_POTENTIOMETER;
							}
						break;

						case LAMP_DIM_NODIM:

						break;
					}

					timer_set_oc_value(TIM1, TIM_OC1, lamp_brightness);

					// Events
					if(button_pressed || lamp_ev_ir_onbutton){
						lamp_state = LAMP_TURN_OFF;
						StartFading(fade_duration_default, lamp_brightness, LAMP_MIN_BRIGHTNESS);

						lamp_ev_ir_onbutton = false;
					}

					LampCheckRemote();
				break;

				case LAMP_OFF:

					// Events
					if(button_pressed || lamp_ev_ir_onbutton){
						lamp_state = LAMP_TURN_ON;
						StartFading(fade_duration_default, LAMP_MIN_BRIGHTNESS, lamp_brightness);
						
						lamp_ev_ir_onbutton = false;
					}
					if(lamp_ev_alarm){
						lamp_state = LAMP_TURN_ON;

						lamp_dim_state = LAMP_DIM_REMOTE;
						lamp_brightness = LAMP_MAX_BRIGHTNESS;

						StartFading(sunrise_length, LAMP_MIN_BRIGHTNESS, LAMP_MAX_BRIGHTNESS);

						lamp_ev_alarm = false;
					}
				break;

				case LAMP_TURN_OFF:
					lamp_state = LAMP_FADING;
					lamp_on = false;
				break;

				case LAMP_TURN_ON:
					lamp_state = LAMP_FADING;
					lamp_on = true;

					// Give the timer control of the PWM pin
					gpio_set_mode(LAMP_GPIO_DIM_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, LAMP_GPIO_DIM_PIN);

					// Enable pwm timer
					timer_enable_counter(TIM1);

					gpio_set(LAMP_GPIO_EN_PORT, LAMP_GPIO_EN_PIN);

					if(lamp_dim_state == LAMP_DIM_POTENTIOMETER){
						lamp_brightness = GetPotSample();
					}
				break;

				case LAMP_FADING:
					// Set the PWM duty cycle from the sine of the fade value
					fade_current_val = map(custom_sin(map(fade_count, 0, fade_duration / 10, 0, 128) + 64), 32767, -32767, fade_start_val, fade_end_val);
					timer_set_oc_value(TIM1, TIM_OC1, fade_current_val);

					fade_count++;
					if(fade_count >= fade_duration / 10){
						fade_count = 0;
						if(lamp_on){
							lamp_state = LAMP_ON;
						}else{
							lamp_state = LAMP_OFF;

							// Disable pwm timer
							timer_disable_counter(TIM1);

							// Make sure the pwm pin is low
							gpio_set_mode(LAMP_GPIO_DIM_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LAMP_GPIO_DIM_PIN);
							gpio_clear(LAMP_GPIO_DIM_PORT, LAMP_GPIO_DIM_PIN);
							gpio_clear(LAMP_GPIO_EN_PORT, LAMP_GPIO_EN_PIN);
							
							// Set the brightness value to zero so we dont 
							// get blinded when turning it on
							timer_set_oc_value(TIM1, TIM_OC1, LAMP_MIN_BRIGHTNESS);


						}
					}

					// Events
					if(button_pressed || lamp_ev_ir_onbutton){
						lamp_ev_ir_onbutton = false;
						if(lamp_on){
							StartFading(fade_duration_default, fade_current_val, LAMP_MIN_BRIGHTNESS);
						}else{
							StartFading(fade_duration_default, fade_current_val, lamp_brightness);
						}
						lamp_on = !lamp_on;
					}

					LampCheckRemote();

				break;
				default:
				break;
			}

			// Store the button state for next loop
			previous_button_state = button_state;

			loop_counter++;
		}
	}

	return 0;
}
