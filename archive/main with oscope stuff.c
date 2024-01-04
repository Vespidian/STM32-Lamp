#include "global.h"
#include "utility.h"

#include <stdlib.h>
#include <stdbool.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/timer.h>

#include "usart.h"
#include "terminal.h"

extern unsigned int _data;
extern unsigned int _edata;
extern unsigned int _data_loadaddr;

extern unsigned int _bss;
extern unsigned int _ebss;

int main(void);
void reset_handler(){
	// Copy .data section to memory and zero out .bss section
	memcpy(&_data, &_data_loadaddr, sizeof(size_t) * (&_edata - &_data));
	memset(&_bss, 0, &_ebss - &_bss);

	main();
}

uint16_t sample_buffer[1024] = {};
uint16_t current_sample = 0;

bool trigger_enable = true;
bool trigger_is_rising = true;
uint16_t num_samples = 100;
uint8_t sample_time = ADC_SMPR_SMP_28DOT5CYC;

// uint16_t window_width = ?;

void adc1_2_isr(void){
	sample_buffer[current_sample++] = ADC_DR(ADC1);
	
	// If we have a full frame, pause conversion until further notice
	if(current_sample > num_samples){
		current_sample = 0;
		adc_set_single_conversion_mode(ADC1);
	}
}


static void adc_setup(void)
{
	int i;

	rcc_periph_clock_enable(RCC_ADC1);

	/* Make sure the ADC doesn't run during config. */
	adc_power_off(ADC1);
	adc_enable_eoc_interrupt(ADC1);

	/* We configure everything for one single conversion. */
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	for (i = 0; i < 800000; i++)    /* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

/**
 * What we need (FOR OSCILLOSCOPE):
 * Continuous adc conversion on a single channel
 * Adjustable conversion time (sampling rate)
 * Adjustable # of samples (memory)
 * 
 * A way to automatically adjust sample rate and window size 
 * based on length of time to capture (capture window width)
 * 
 * Adjustable trigger voltage to start capture (rising and falling)
 * 
 * 
*/

void pwm_setup(void){
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_GPIOA);
	
	rcc_periph_clock_enable(RCC_AFIO);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	
	rcc_periph_clock_enable(RCC_TIM1);
	timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1,
				TIM_CR1_DIR_UP);
	timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_PWM2);
	timer_enable_oc_output(TIM1, TIM_OC1);
	timer_enable_break_main_output(TIM1);
	timer_set_oc_value(TIM1, TIM_OC1, 4); // duty cycle
	timer_set_period(TIM1, 4096); // frequency
	timer_enable_counter(TIM1);
}

void setup_systick(void){
	systick_set_reload(9999);
	systick_clear();

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);

	systick_interrupt_enable();

	systick_counter_enable();
}

bool tick = false;
void sys_tick_handler(){
	tick = true;
	// gpio_toggle(GPIOC, GPIO13);
}

bool lamp_on = false;
bool previous_button_state = false;
bool button_state = false;

int main(void)
{

	rcc_periph_clock_enable(RCC_GPIOA);

	setup_systick();
	pwm_setup();
	// USARTInit();

	/* Send a message on USART1. */
	// usart_send_blocking(USART1, 's');
	// usart_send_blocking(USART1, 't');
	// usart_send_blocking(USART1, 'm');
	// usart_send_blocking(USART1, '\r');
	// usart_send_blocking(USART1, '\n');


	uint8_t channel_array[16];
	uint16_t pot_val = 0;
	adc_setup();
	channel_array[0] = 1; // pin PA1
	adc_set_regular_sequence(ADC1, 1, channel_array);

	/**
	 * PA1:		Potentiometer		Input	AF (analog input)
	 * PA5:		On / Off Button		Input	GP
	 * PA8:		PWM 				Output	AF (Timer 1)
	 * PA11:	LED Enable pin		Output	GP
	*/

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO1);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO5);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);

	gpio_set(GPIOA, 11);

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

				timer_set_oc_value(TIM1, TIM_OC1, pot_val);

				if(button_state == true && previous_button_state == false){
					lamp_on = false;

					// Disable pwm timer
					timer_disable_counter(TIM1);

					// Make sure the pwm pin is low
					gpio_clear(GPIOA, GPIO8);
					
					// Set led enable pin low
					gpio_clear(GPIOA, GPIO11);
				}
			}else{
				if(button_state == true && previous_button_state == false){
					lamp_on = true;

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
