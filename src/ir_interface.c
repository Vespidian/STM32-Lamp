#include "global.h"

#include <stdlib.h>
#include <libopencm3/stm32/gpio.h>

#include "usart.h"
#include "terminal.h"
#include "ir.h"
#include "lamp.h"

extern bool lamp_ev_ir_onbutton;
extern enum LAMP_EVENT lamp_ev_ir_brightness;

#define IR_DEVICE_ADDRESS 0x0001

bool terminal_mode = false;
uint16_t terminal_timeout = 60; // 1 = 10ms, 2 = 20ms, etc
uint16_t terminal_timeout_counter = 0;

extern IRPacket rx_packet_buffer[256];
extern uint8_t rx_buffer_head;
extern uint8_t rx_buffer_tail;
static bool IRComputeCRC(){
    bool ret = false;

    uint8_t crc = command_buffer[0];
    for(int i = 1; i < (command_buffer_index - 1); i++){
        crc ^= command_buffer[i];
    }

    if(crc == command_buffer[command_buffer_index - 1]){
        ret = true;
    }
    USARTWrite("\nrec:");
    USARTWriteHex(command_buffer[command_buffer_index - 1]);
    USARTWrite(" cmp:");
    USARTWriteHex(crc);
    USARTWriteByte('.');

    return ret;
}

void IRSendString(char *str){
	while(ir_state != IR_STATE_CTS);
	IRSendPacket(IR_DEVICE_ADDRESS, 0x02); // STX (start of text) (initializing terminal mode)
	
	for(int i = 0; str[i] != '\0'; i++){
		IRSendPacket(IR_DEVICE_ADDRESS, str[i]);
		while(ir_state != IR_STATE_CTS);
	}
	IRSendPacket(IR_DEVICE_ADDRESS, 0x03); // ETX (end of text)
}

void IRCheckCommands(void){
    IRPacket packet = IRGetPacket();
    if(packet.address != 0xFFFF){
        terminal_timeout_counter = 0;

        if(terminal_mode){
            // extern char previous_char;
            // previous_char = packet.command;
            switch(packet.command){
                case '\r': // Carriage return
                case '\n': // Newline
                case 0x03: // ETX (end of transmit)
                    terminal_mode = false;
                    USARTWriteByte('\n');
                    if(IRComputeCRC()){
                        command_buffer_index--;
                        GetCommand();
                        USARTWrite("\nACKY\n");
                        USARTWriteByte('\n');
                        IRSendPacket(IR_DEVICE_ADDRESS, 0x06); // ACK (valid message received)
                    }else{
                        // TODO: make this send to the address the message was received from
                        command_buffer_index = 0;
                        USARTWrite("\nNAKY\n");
                        USARTWriteByte('\n');
                        IRSendPacket(IR_DEVICE_ADDRESS, 0x15); // NAK (CRC not correct, message is corrupt)
                    }
                break;

                default:
                    command_buffer[command_buffer_index++] = packet.command;
                break;
            }

            if(packet.command != 0){
                // Return the character to the sender, for terminal like operation
                USARTWriteByte(packet.command);
            }
        }else{
            bool cancel_print = false;

            switch(packet.command){
                case 0x02: // STX (start of transmit) (enter terminal mode)
                    terminal_mode = true;
                    command_buffer_index = 0;
                break;

                case 0x06: // ACK (receiver device received valid message)
                    USARTWrite("\nACK\n");
                    USARTWriteByte('\n');
                    cancel_print = true;
                break;

                case 0x15: // NAK (receiver device could not verify message crc)
                    USARTWrite("\nNAK\n");
                    USARTWriteByte('\n');
                    cancel_print = true;
                break;

                case 0x17: // (receiver device received incomplete message) (no 0x03 at message end)
                    USARTWrite("\nTIMEOUT\n");
                    USARTWriteByte('\n');
                    cancel_print = true;
                break;

                case 0x21:  // Power button
                    lamp_ev_ir_onbutton = true;
                break;

                case 0x2D:  // Brightness +
                    lamp_ev_ir_brightness = LAMP_EVENT_BRIGHTNESS_INC;
                break;

                case 0x2B:  // Brightness -
                    lamp_ev_ir_brightness = LAMP_EVENT_BRIGHTNESS_DEC;
                break;

				case 0x3E:  // Max Brightness
                    lamp_ev_ir_brightness = LAMP_EVENT_BRIGHTNESS_MAX;
                break;

				case 0x3C:  // Min Brightness
                    lamp_ev_ir_brightness = LAMP_EVENT_BRIGHTNESS_MIN;
                break;

                // case 0x:
                    // break;
                default:
                break;
            }

            if(!terminal_mode && !cancel_print){
                USARTWrite("\n");
                USARTWrite("0x");
                USARTWriteHex(packet.address & 0xFF);
                USARTWriteHex((packet.address >> 8) & 0xFF);
                USARTWriteHex(packet.command_inv);
                USARTWriteHex(packet.command);
                USARTWriteByte('.');
            }
        }
    }

    if(terminal_mode){
        if(terminal_timeout_counter >= terminal_timeout){
            terminal_mode = false;
            USARTWrite("\nTIMMIEOUT\n");
            USARTWriteByte('\n');
            IRSendPacket(IR_DEVICE_ADDRESS, 0x17); // Terminal receive timeout
        }

        terminal_timeout_counter++;
    }
}
