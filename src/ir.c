#include "global.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/exti.h>

#include "utility.h"
#include "usart.h"
#include "ir.h"

IR_STATE ir_state = IR_STATE_RX;

const uint8_t IR_PACKET_LENGTH = 32;

// Transmission
static IRPacket tx_packet;
int16_t tx_timings[67];
unsigned char current_tx_timing;

// Reception
int8_t ir_rx_bit_num = 0;
uint32_t ir_rx_raw = 0;
IRPacket rx_packet_buffer[256] = {0};
uint8_t rx_buffer_head = 0;
uint8_t rx_buffer_tail = 0;

/**
 * This interrupt is always active
 * For transmission, it fires at defined intervals depending on transmitted data
 * For reception, it fires every 45ms if not reset
 * The exti interrupt for reception will reset it whenever triggered
*/
void tim2_isr(void){
	timer_clear_flag(TIM2, TIM_SR_UIF);
	timer_set_counter(TIM2, 0);

	switch(ir_state){
		case IR_STATE_TX:
			
			current_tx_timing++;
			// Negative timing means time where LED is not transmitting
			if(tx_timings[current_tx_timing] < 0){
				// gpio_set(GPIOC, GPIO13);
				timer_disable_counter(TIM3);
				timer_set_period(TIM2, (uint16_t)-tx_timings[current_tx_timing]);
			}else{
				// gpio_clear(GPIOC, GPIO13);
				timer_enable_counter(TIM3);
				timer_set_period(TIM2, (uint16_t)tx_timings[current_tx_timing]);
			}


			if(current_tx_timing >= 67){
				timer_disable_counter(TIM2);
				// gpio_set(GPIOC, GPIO13);
				timer_disable_counter(TIM3);

				gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_TIM3_CH1);
				gpio_clear(GPIOA, GPIO_TIM3_CH1);

				// Switch to receive mode
				ir_state = IR_STATE_RX;
				timer_set_period(TIM2, 45000); // 45ms period
				timer_enable_counter(TIM2);
				exti_enable_request(EXTI4);
			}

			break;
		case IR_STATE_RX:
			// If the code reaches here, there hasnt been a reception in 45ms, this means we are clear to transmit
			ir_state = IR_STATE_CTS;
			break;
		case IR_STATE_CTS:
			break;
	}

	
}

void exti4_isr(void){
	exti_reset_request(EXTI4);
	ir_state = IR_STATE_RX;

	timer_disable_counter(TIM2);
	uint32_t counter = timer_get_counter(TIM2);
	timer_set_counter(TIM2, 0);
	uint16_t margin = 150; // Timing margins in microseconds (can be +-margin off from expected value)

	if((counter > (1100 - margin)) && (counter < (1100 + margin))){
		// Zero
		ir_rx_bit_num++;
	}else if((counter > (2200 - margin)) && (counter < (2200 + margin))){
		// One
		ir_rx_raw |= 1 << (ir_rx_bit_num);
		ir_rx_bit_num++;
	}else if((counter > (13500 - margin)) && (counter < (13500 + margin))){
		// Start bit
		ir_rx_bit_num = 0;
		ir_rx_raw = 0;
	}else if(counter == 0){
		// Start of packet or repeat
		ir_rx_bit_num = 0;
		ir_rx_raw = 0;
	}else{
		// Unknown (something else)
		ir_rx_bit_num = 0;
		ir_rx_raw = 0;
	}

	// Full packet received
	if(ir_rx_bit_num == IR_PACKET_LENGTH){

		// Check if packet fits in buffer and add it if it does
		if((rx_buffer_head + 1) != rx_buffer_tail){
			rx_packet_buffer[rx_buffer_head].address = (ir_rx_raw & 0xFFFF);
			rx_packet_buffer[rx_buffer_head].command = ((ir_rx_raw >> 16) & 0xFF);
			rx_packet_buffer[rx_buffer_head].command_inv = ((ir_rx_raw >> 24) & 0xFF);
			rx_buffer_head++;
		}

		ir_rx_bit_num = 0;
		ir_rx_raw = 0;
	}

	/** Start a timer  counting when we hit a falling edge
	 *  When the next falling edge hits, stop the timer
	 *  approx 1100us = zero
	 *  approx 2200us = one
	 *  approx 13.5ms = start bit
	 *  approx 11.3ms = repeat bit
	 * 	significantly less than 1100 = ignore
	 * 	significantly more than 2200 = ignore
	 * 
	 */

	timer_enable_counter(TIM2);
}

/**
 * What i've learned:
 * The IR receiver only responds to 38kHz modulated transmissions
 * So what the transmitter has to do is send out a 38kHz signal as a carrier
 * and modulate the data on top of the 38kHz carrier
 * 
 * What do next:
 * Set up a timer with a 38kHz frequency
 * have the transmit function modulate the timer on and off
*/

void IRSendPacket(uint16_t address, uint8_t command){

	tx_packet.address = address;
	tx_packet.command = command;

	// Filling out the packet timings array (negative value means no transmit)
	tx_timings[0] = 9000;
	tx_timings[1] = -4500;

	// ADDRESS
	uint8_t offset = 2;
	for(int i = 0; i < 32; i += 2){
		tx_timings[i + offset] = 500;
		if(((address >> (i / 2)) & 1) == 0){
			tx_timings[i + offset + 1] = -630;
		}else{
			tx_timings[i + offset + 1] = -1795;
		}
	}

	// COMMAND
	offset += 32;
	for(int i = 14; i >= 0; i -= 2){
	// for(int i = 0; i < 16; i += 2){
		tx_timings[i + offset] = 500;
		if(((command >> (i / 2)) & 1) == 0){
			tx_timings[i + offset + 1] = -630;
		}else{
			tx_timings[i + offset + 1] = -1795;
		}
	}

	// COMMAND INVERSE
	offset += 16;
	// for(int i = 0; i < 16; i += 2){
	for(int i = 14; i >= 0; i -= 2){
		tx_timings[i + offset] = 500;
		if(((command >> (i / 2)) & 1) == 1){
			tx_timings[i + offset + 1] = -630;
		}else{
			tx_timings[i + offset + 1] = -1795;
		}
	}

	// STOP BIT
	offset += 16;
	tx_timings[offset] = 562;
	current_tx_timing = 0;

	// Wait for "clear to send" flag (make sure there are no ongoing receptions)
	while(ir_state != IR_STATE_CTS);

	// Set up for transmission
	ir_state = IR_STATE_TX;

	// Pause reception while we transmit
	exti_disable_request(EXTI4);
	
	timer_disable_counter(TIM2);
	timer_set_period(TIM2, tx_timings[current_tx_timing]);
	timer_set_counter(TIM2, 0);
	timer_enable_counter(TIM2);

	// Initialize the timer and start transmission
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH1);
	timer_enable_counter(TIM3);

}

IRPacket IRGetPacket(void){
	// If theres nothing in the receive buffer return a packet with address 0xFFFF
	IRPacket packet = {0xFFFF, 0x00};

	if(rx_buffer_tail != rx_buffer_head){
		packet = rx_packet_buffer[rx_buffer_tail];
		rx_buffer_tail++;
	}

	return packet;
}

static void pwm_setup(void){
	// Set up timer 3 to generate a 38kHz 50% duty cycle PWM signal on PA6
	
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_TIM3);
	
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_TIM3_CH1);

	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1, TIM_CR1_DIR_UP);
	timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM2);
	timer_enable_oc_output(TIM3, TIM_OC1);
	timer_enable_break_main_output(TIM3);

	// 38kHz 50% duty cycle PWM
	timer_set_oc_value(TIM3, TIM_OC1, 51);
	timer_set_period(TIM3, 103);
	timer_disable_counter(TIM3);
}

void IRSetup(void){
	ir_state = IR_STATE_RX;

	rcc_periph_clock_enable(RCC_GPIOA);

	// Set up transmitter
	pwm_setup();

	// Set up receiver
	nvic_enable_irq(NVIC_EXTI4_IRQ);

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO4);

	// Enable the falling edge triggered interrupt for the receiver
	exti_select_source(EXTI4, GPIOA);
	exti_set_trigger(EXTI4, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI4);

	// Setup timer 2 for both transmit and receive
	nvic_enable_irq(NVIC_TIM2_IRQ);
	nvic_set_priority(NVIC_TIM2_IRQ, 1);
	rcc_periph_clock_enable(RCC_TIM2);
	timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT_MUL_2, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM2, 7);
	timer_set_period(TIM2, 45000);
	timer_set_counter(TIM2, 0);
	timer_enable_irq(TIM2, TIM_DIER_UIE);
	timer_enable_counter(TIM2);

}