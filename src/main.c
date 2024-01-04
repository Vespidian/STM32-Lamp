#include <stdlib.h>
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
#include "terminal.h"


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

	// Wait for ADC starting up (100 ms)
	for (int i = 0; i < 800000; i++){
		__asm__("nop");
	}

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);

	uint8_t channel_array[16];
	channel_array[0] = 1; // pin PA1
	adc_set_regular_sequence(ADC1, 1, channel_array);
}

void adc_on(void){
	adc_power_on(ADC1);

	// Wait for ADC starting up (1 ms)
	for (int i = 0; i < 8000; i++){
		__asm__("nop");
	}

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

void pwm_setup(void){
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_GPIOA);
	
	rcc_periph_clock_enable(RCC_AFIO);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	
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

	rtc_awake_from_off(RCC_LSE);

	// RTC crystal is 32,768 kHz and we want a 1 Hz isr
	rtc_set_prescale_val(32766);

	rtc_interrupt_enable(RTC_SEC);
}

void rtc_isr(void){
	// The interrupt flag isn't cleared by hardware, we have to do it
	rtc_clear_flag(RTC_SEC);

	gpio_toggle(GPIOC, GPIO13);

}

/** --- LAMP VARIABLES --- **/
bool lamp_on = false;
bool previous_button_state = false;
bool button_state = false;
uint16_t pot_val = 0;

int main(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);

	systick_setup();
	pwm_setup();
	adc_setup();

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
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO5);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);

	// Off by default
	timer_disable_counter(TIM1);
	gpio_clear(GPIOA, GPIO8); 
	gpio_clear(GPIOA, GPIO11);

	while (1) {
		if(tick){
			tick = false;

			// Read the current button state
			button_state = gpio_get(GPIOA, GPIO5);
			if(lamp_on){
				// Start ADC conversion and wait for end of conversion
				adc_start_conversion_direct(ADC1);
				while (!(ADC_SR(ADC1) & ADC_SR_EOC));

				pot_val = ADC_DR(ADC1);

				// Set the PWM duty cycle from the pot value
				timer_set_oc_value(TIM1, TIM_OC1, pot_val);

				// Check if the button was pressed (rising edge)
				if(button_state == true && previous_button_state == false){
					lamp_on = false;

					// Disable pwm timer
					timer_disable_counter(TIM1);

					// Make sure the pwm pin is low
					gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);
					gpio_clear(GPIOA, GPIO8);
					
					// Set led enable pin low
					gpio_clear(GPIOA, GPIO11);

					// Set the brightness value to zero so we dont 
					// get blinded when turning it on
					//
					// PWM period is 4096, so having the output compare 
					// value be 4095 makes duty cycle = 0%
					pot_val = 4095;
					timer_set_oc_value(TIM1, TIM_OC1, pot_val);

					// Turn off the ADC to conserve power
					adc_power_off(ADC1);

				}
			}else{
				// Check if the button was pressed (rising edge)
				if(button_state == true && previous_button_state == false){
					lamp_on = true;

					// Turn on the ADC
					adc_on();

					// Give the timer control of the PWM pin
					gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);

					// Enable pwm timer
					timer_enable_counter(TIM1);

					// Set led enable pin high
					gpio_set(GPIOA, GPIO11);

				}
			}
			// Store the button state for next loop
			previous_button_state = button_state;
		}
	}

	return 0;
}
