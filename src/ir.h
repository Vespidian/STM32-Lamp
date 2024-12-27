#ifndef IR_H_
#define IR_H_

typedef struct IRPacket{
	uint16_t address;
	uint8_t command;
	uint8_t command_inv;
}IRPacket;

typedef enum IR_STATE{
	IR_STATE_RX,	// Currently receiving data, cannot call any transmit calls
	IR_STATE_TX,	// Currently transmitting data, receiving is disabled
	IR_STATE_CTS	// Listening for transmissions but not actively receiving (clear to send / transmit)
}IR_STATE;

// Holds the state for the state machine which dictates when to transmit and receive
extern IR_STATE ir_state;

/**
 * @brief Initializes TIM2, TIM3, PA6, PA4, and EXTI4 for IR transmission and reception
*/
void IRSetup(void);

/**
 * @brief Transmit an IR packet with 'address' and 'command' fields
 * @param address The device you want to receive this command
 * @param command The command to send to the receiving device
*/
void IRSendPacket(uint16_t address, uint8_t command);

/**
 * @brief Retrieve a received message from the circular buffer (buffer size is 256)
 * @return The oldest received IR packet
*/
IRPacket IRGetPacket(void);

/**
 * Interface:
 * An ir read function which returns the pending received ir packet, if no packet return address 0xffff
 * An ir send function which prepares a tx_packet for transmission and waits until reception is done to transmit it
 * 
 * 
 * Timings:
 * End of reception
 * 45ms pause (long enough for a repeat code to be sent)
 * Start transmission
 * 
 * End of transmission
 * Immediately return to reception mode
 * 
 * No active reception
 * 45ms pause (optional)
 * Immediately start transmission
 * 
 * 
 * Modes:
 * - command mode: each packet is a command with a defined action
 * - terminal mode: each packet is an ascii character (a predefined command will enter and exit terminal mode)
 * 
 * Terminal mode:
 * - terminal mode must exit if no data or 'end of text' byte is received within 300ms
 * 
*/

#endif