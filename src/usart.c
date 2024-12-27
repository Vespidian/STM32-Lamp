#include "global.h"
#include "utility.h"
#include <stdlib.h>
#include <stdint.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/gpio.h>
// #include "stdlib.h"
// #include "rcc.h"
// #include "nvic.h"
// #include "gpio.h"
// #include "usart.h"
// #include "stm32f103xb.h"

#define MANTISSA FREQUENCY / (BAUD * 16)
#define FRACTION (((((long long)FREQUENCY * 100) / (BAUD * 16)) - (MANTISSA * 100)) * 16) / 100

typedef struct USARTBuffer{
	char buffer[256];
	uint8_t head;
	uint8_t tail;	
}USARTBuffer;

char USART1_buffer_tx[256];
uint8_t usart_buffer_tx_head;
uint8_t usart_buffer_tx_tail;

#define USART1_BUFFER_SIZE 256
char USART1_buffer_rx[256];
uint8_t usart_buffer_rx_head;
uint8_t usart_buffer_rx_tail;

bool usart_interrupt_ready = false;

// void USARTSetBaud(){
//     // unsigned short mantissa = current_clock_speed / (baud * 16);
//     // unsigned short fraction = (((((long long)current_clock_speed * 100) / (baud * 16)) - (mantissa * 100)) * 16) / 100;

// 	USART1->BRR |= (FRACTION << USART_BRR_DIV_Fraction_Pos) & USART_BRR_DIV_Fraction_Msk;
// 	USART1->BRR |= (MANTISSA << USART_BRR_DIV_Mantissa_Pos) & USART_BRR_DIV_Mantissa_Msk;
// }


void USARTInit(){
	// Zero out the TX and RX buffers
	memset(USART1_buffer_rx, 0, USART1_BUFFER_SIZE);
	memset(USART1_buffer_tx, 0, USART1_BUFFER_SIZE);
	
	// Enable the usart interrupt in the NVIC
	// NVICEnableInterrupt(37);
	nvic_enable_irq(NVIC_USART1_IRQ);
	
	// Enable USART clock
	// RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_USART1);

	// Set Tx pin as output alternate function push-pull
	// GPIOSetPinMode(GPIO_PORT_A, 9, GPIO_MODE_OUTPUT_10MHZ, GPIO_CONFIG_OUTPUT_AF_PUSHPULL);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_TX);

	
	// Set Rx pin as input pull-up
	// GPIOSetPinMode(GPIO_PORT_A, 10, GPIO_MODE_INPUT, GPIO_CONFIG_INPUT_FLOATING);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO_USART1_RX);

	// Set baud rate
	// USARTSetBaud(); // Set BRR register (baud rate)
	usart_set_baudrate(USART1, 9600);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	// Set the usart control register (Enable peripheral and interrupts)
	// USART1->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_TCIE;
	usart_enable_rx_interrupt(USART1);
	// USART_CR1(USART1) |= USART_CR1_RXNEIE;
	usart_enable_tx_complete_interrupt(USART1);
	// usart_enable_tx_interrupt(USART1);

	usart_enable(USART1);
}

void usart1_isr(void){
	// if((USART1->SR & USART_SR_RXNE) != 0){
	if(usart_get_flag(USART1, USART_SR_RXNE) == 1){

		// USART1_buffer_rx[usart_buffer_rx_head++] = (char)USART1->DR;
		// USART1_buffer_rx[usart_buffer_rx_head++] = (char)usart_recv(USART1);
		USART1_buffer_rx[usart_buffer_rx_head++] = (char)USART_DR(USART1);
	}

	// if((USART1->SR & USART_SR_TC) != 0){
	if(usart_get_flag(USART1, USART_SR_TC) == 1){
		if(usart_buffer_tx_tail != usart_buffer_tx_head){
			// USART1->DR = USART1_buffer_tx[usart_buffer_tx_tail++];
			usart_send(USART1, USART1_buffer_tx[usart_buffer_tx_tail++]);

		}
		// USART1->SR &= ~USART_SR_TC;
		// USART1->SR &= ~USART_SR_TXE;
		USART_SR(USART1) &= ~(USART_SR_TC | USART_SR_TXE);
	}
}

void USARTWriteByte(uint8_t byte){
	usart_send_blocking(USART1, byte);

	// TXE (Wait for the transmit data register to be empty (1))
	// while((USART1->SR & USART_SR_TXE) == 0);

	// Set the data register's data to 'byte'
	// USART1->DR = byte;


	/** -- OR -- **/
	// USART1_buffer_tx[usart_buffer_tx_head++] = byte; // Doesnt work for some reason
}

void USARTWrite(const char *str){
	if(str != NULL){
		// Put the string into the tx buffer and make the interrupt tx it all
		// for(int i = 0; (str[i] != 0) && (usart_buffer_tx_head != usart_buffer_tx_tail); i++){
		for(int i = 0; (str[i] != 0); i++){
			USART1_buffer_tx[usart_buffer_tx_head++] = str[i];
			// if(usart_buffer_tx_tail == USART1_BUFFER_SIZE){
			// 	usart_buffer_tx_tail = 0;
			// }
		}
		// Now tx the 1st character to allow the ISR to do the rest
		// USARTWriteByte(str[0]);
	}
}

void USARTWriteInt(uint32_t num){
	if(num == 0){
		// USARTWriteByte('0');
		USARTWrite("0");
		return;
	}

	uint8_t num_digits = 0;
	char digits[10] = {0};
	while(num > 0){
		digits[num_digits] = num % 10;
		num /= 10;
		num_digits++;
	}

	char str[11];
	for(int i = 0; i < num_digits; i++){
		str[i] = '0' + digits[num_digits - i - 1];
	}
	str[num_digits] = 0;
	USARTWrite(str);
}

void USARTWriteHex(uint8_t num){
	// char str[4] = "0x00";
	char str[2] = "00";

	uint8_t val = (num >> 4);
	// str[2] = ((val > 9) ? (val + 'A' - 10) : (val + '0'));
	str[0] = ((val > 9) ? (val + 'A' - 10) : (val + '0'));
	
	val = (num & 0x0f);
	// str[3] = ((val > 9) ? (val + 'A' - 10) : (val + '0'));
	str[1] = ((val > 9) ? (val + 'A' - 10) : (val + '0'));

	USARTWrite(str);
}

void USARTWriteBin8(uint8_t num){
	char str[8] = "xxxxxxxx";
	for(int i = 0; i < 8; i++){
		str[i] = ((num >> (7 - i)) & 1) + '0';
	}
	USARTWrite(str);
}

void USARTWriteBin32(uint32_t num){
	for(int i = 0; i < 4; i++){
		USARTWriteBin8((num >> ((3 - i) * 8)) & 0xFF);
	}
}

uint8_t USARTReadByte(){
	uint8_t c = 0;
	if(usart_buffer_rx_head != usart_buffer_rx_tail){
		c = USART1_buffer_rx[usart_buffer_rx_tail++];
		if(usart_buffer_rx_tail == USART1_BUFFER_SIZE){
			usart_buffer_rx_tail = 0;
		}
	}
	return c;
}