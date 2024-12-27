// #include "stdlib.h"
#include "global.h"
#include <stdlib.h>
#include <stdbool.h>
#include "usart.h"
#include "utility.h"

#include "lamp.h"

#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/f1/bkp.h>

// #include "stm32f103xb.h"

// #include "rtc.h"


typedef void (*CommandFunction)(const char *command_buffer);

char command_buffer[256];
uint8_t command_buffer_index;

char previous_char;

void FuncHelp(const char *command_buffer);
void FuncReset(const char *command_buffer);
void FuncRegister(const char *command_buffer);
void FuncTransmit(const char *command_buffer);
void FuncTime(const char *command_buffer);
void FuncSet(const char *command_buffer);
void FuncAlarm(const char *command_buffer);
void FuncPing(const char *command_buffer);


const char *command_list[] = {
	"help",
	"reset",
	"reg",
	"transmit",
	"time",
	"set",
	"alarm",
	"ping",
	NULL
};

CommandFunction function_list[] = {
	FuncHelp,
	FuncReset,
	FuncRegister,
	FuncTransmit,
	FuncTime,
	FuncSet,
	FuncAlarm,
	FuncPing,
	NULL
};

/**
 * Compare two strings until either a null terminator or the first instance of 'delimiter'
*/
bool StringCompare(const char *str1, const char *str2, char delimeter){
	bool is_same = true;
	for(size_t i = 0; (str1[i] != '\0') && (str2[i] != '\0') && (str1[i] != delimeter) && (str2[i] != delimeter); i++){
		if(str1[i] != str2[i]){
			is_same = false;
			break;
		}
	}
	if(str1[0] == 0 || str2[0] == 0){
		is_same = false;
	}
	return is_same;
}

/**
 * Finds and returns the length until the terminating character or the first instance of 'delimiter'
*/
size_t StringLength(const char *str, char delimiter){
	size_t len = 0;
	while(str[len] != '\0' && str[len] != delimiter){
		len++;
	}
	return len;
}

/**
 * Finds the first instance of 'c' and returns a pointer to it
*/
char *FindChar(const char *str, char c){
	char *found_char = NULL;

	for(int i = 0; str[i] != '\0'; i++){
		if(str[i] == c){
			found_char = (char *)&str[i];
			break;
		}
	}
	return found_char;
}

/**
 * Iterates through the 'command_list' array and finds which matches the input
*/
unsigned int FindCommand(const char *command){
	int index = 0;
	bool found = false;
	while(command_list[index] != NULL){
		if(StringCompare(command_list[index], command, ' ')){
			found = true;
			break;
		}
		index++;
	}

	if(!found){
		index = -1;
	}
	return index;
}

unsigned int CountChars(const char *str, char delimiter){
	unsigned int num_chars = 0;
	for(int i = 0; str[i] != '\0'; i++){
		if(str[i] == delimiter){
			num_chars++;
		}
	}
	return num_chars;
}

/**
 * Convert string to integer value
*/
unsigned int StrToInt(const char *str, char delimiter){
	unsigned int num = 0;
	unsigned int digit = 1;
	if(str != NULL){
		for(int i = StringLength(str, ' ') - 1; i >= 0; i--){
			if(str[i] >= '0' && str[i] <= '9'){
				num += (str[i] - '0') * digit;
				digit *= 10;
			}
		}
	}
	return num;
}

unsigned int HexStrToInt(const char *hex, char delimiter){
	unsigned int num = 0;;
	if(hex != NULL){

		// If there is a '0x' prefix, get rid of it
		if(hex[0] == '0' && hex[1] == 'x'){
			hex += 2;
		}
		size_t num_digits = StringLength(hex, delimiter);
		
		// If there are more than 8 digits to the number, return zero since we cant hold more than a 32-bit num
		if(num_digits > 8){
			return 0;
		}

		for(int i = 0; i < num_digits; i++){
			char offset_char = 0;
			if(hex[i] <= '9' && hex[i] >= '0'){
				offset_char = '0';
			}else if(hex[i] <= 'F' && hex[i] >= 'A'){
				offset_char = 'A' - 10;
			}else if(hex[i] <= 'f' && hex[i] >= 'a'){
				offset_char = 'a' - 10;
			}else{
				return 0;
			}
			num += (hex[i] - offset_char) << ((num_digits - 1 - i) * 4);
		}
	}
	return num;
}

void GetCommand(){
	if(command_buffer_index != 0){

		command_buffer[command_buffer_index] = 0;
		command_buffer_index = 0;

		unsigned int function_id = FindCommand(command_buffer);
		if(function_id == -1){
			USARTWrite(command_buffer);
			USARTWrite(": command not found");
		}else if(function_list[function_id] == NULL){
			USARTWrite(command_buffer);
			USARTWrite(": command not implemented");
		}else{
			function_list[function_id](command_buffer);
		}

		command_buffer[0] = 0;
		
		// Display the prompt
		USARTWrite("\nstm32$ ");
	}else if(previous_char == '\n'){
		// Display the prompt
		USARTWrite("stm32$ ");
	}
}

// #include "gpio.h"
void Terminal(){
	char c;
	c = USARTReadByte();
	previous_char = c;
	switch(c){
		case 0:
			break;
		case 0x03: // Ctrl + c
			command_buffer_index = 0;
			break;
		case 0x7f: // Backspace 
			if(command_buffer_index != 0){
				command_buffer_index--;
				USARTWrite("\x1b[D \x1b[D");
			}
			break;
		case '\r': // Carriage return
			break;
		case '\n': // Line feed
			// Process the command
			GetCommand();
			break;
		default:
			command_buffer[command_buffer_index++] = c;
			break;
	}
	if(c != 0){
		// Return the character to the sender, for terminal like operation
		USARTWriteByte(c);
	}
}

/* --- COMMAND FUNCTIONS --- */

void FuncHelp(const char *command_buffer){
	USARTWrite("The following commands are currently defined:\n\n");
	for(int i = 0; command_list[i] != NULL; i++){
		USARTWrite(command_list[i]);
		USARTWrite(" ");
	}
	USARTWrite("\n");
}

extern void reset_handler(void);
void FuncReset(const char *command_buffer){
	reset_handler();
}

void FuncRegister(const char *command_buffer){
	if(StringCompare(FindChar(command_buffer, ' ') + 1, "set", ' ')){
		command_buffer = (FindChar(command_buffer, ' ') + 1);
		unsigned int *reg = (unsigned int *)HexStrToInt(command_buffer = (FindChar(command_buffer, ' ') + 1), ' ');
		USARTWriteBin32(*reg);
	}else if(StringCompare(FindChar(command_buffer, ' ') + 1, "get", ' ')){
		command_buffer = (FindChar(command_buffer, ' ') + 1);
		if(CountChars(command_buffer, ' ') != 2){
			goto invalid;
		}
		unsigned int *reg = (unsigned int *)HexStrToInt(command_buffer, ' ');
		USARTWriteBin32(*reg);
	}else{
		invalid:
		USARTWrite("reg: invalid usage\n	reg [get/set] [address] [bit] [0 or 1]\n");
	}

}

#include "ir.h"
void FuncTransmit(const char *command_buffer){
	char *dat = FindChar(command_buffer, ' ') + 1;
	uint8_t dat_len = StringLength(dat, '\n');


	while(ir_state != IR_STATE_CTS);
	
	uint8_t crc = 0;
	IRSendPacket(0x0001, 0x02); // STX (start of text) (initializing terminal mode)
	while(ir_state != IR_STATE_CTS);
	for(int i = 0; i < dat_len; i++){
		crc ^= dat[i];
		IRSendPacket(0x0001, dat[i]);
		while(ir_state != IR_STATE_CTS);
	}

	IRSendPacket(0x0001, crc); // CRC
	while(ir_state != IR_STATE_CTS);
	
	IRSendPacket(0x0001, 0x03); // ETX (end of text)

}

extern const uint32_t DAY_LENGTH;
uint32_t RTCCalculateSeconds(uint32_t day, uint32_t hour, uint32_t minute, uint32_t second){
	return day * DAY_LENGTH + hour * 3600 + minute * 60 + second;
}
void RTCCalculateTime(uint32_t *second, uint32_t *day, uint32_t *hour, uint32_t *minute){
	*day = *second / DAY_LENGTH;
	*second -= (*second / DAY_LENGTH * DAY_LENGTH);
	*hour = *second / 3600;
	*second -= (*hour * 3600);
	*minute = *second / 60;
	*second -= *minute * 60;
}

void FuncTime(const char *command_buffer){
	const char *param = FindChar(command_buffer, ' ') + 1;
	if(StringCompare(param, "set", ' ')){
		// Set
		if(CountChars(param, ' ') == 4){
			uint32_t day, hour, minute, second;
			day = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			hour = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			minute = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			second = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			rtc_set_counter_val(RTCCalculateSeconds(day, hour, minute, second));
			USARTWriteInt(rtc_get_counter_val());

		}else{
			USARTWrite("time set: invalid usage\n	time set [day] [hour] [minute] [second]\n");

		}
	}else{
		uint32_t day, hour, minute, second = rtc_get_counter_val();
		RTCCalculateTime(&second, &day, &hour, &minute);

		USARTWriteInt(rtc_get_counter_val());
		USARTWrite("\nCurrent time:\n");
		switch(day % 7){
			case 0:
				USARTWrite("Mon");
				break;
			case 1:
				USARTWrite("Tue");
				break;
			case 2:
				USARTWrite("Wed");
				break;
			case 3:
				USARTWrite("Thr");
				break;
			case 4:
				USARTWrite("Fri");
				break;
			case 5:
				USARTWrite("Sat");
				break;
			case 6:
				USARTWrite("Sun");
				break;
		}
		USARTWrite(" - ");
		USARTWriteInt(hour);
		USARTWrite(":");
		USARTWriteInt(minute);
		USARTWrite(":");
		USARTWriteInt(second);

		USARTWrite("\nUptime: ");
		USARTWriteInt(day);
		USARTWrite(" days");

		USARTWriteByte('\n');

	}
}

extern bool alarm_set;
void FuncSet(const char *command_buffer){
	if(StringCompare(FindChar(command_buffer, ' ') + 1, "true", ' ')){
		alarm_set = true;
	}else{ // false
		alarm_set = false;
	}
	USARTWrite("alarm_set state: ");
	USARTWriteInt(alarm_set);
	USARTWriteByte('\n');
}

void FuncAlarm(const char *command_buffer){
	const char *param = FindChar(command_buffer, ' ') + 1;
	if(StringCompare(param, "set", ' ')){
		// Set
		uint8_t num_params = CountChars(param, ' ');
		if((num_params > 1) && (num_params <= 4)){
			uint32_t day, hour, minute, second;
			day = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			hour = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			minute = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			second = StrToInt(param = (FindChar(param, ' ') + 1), ' ');
			// USARTWriteInt(rtc_get_alarm_val());
			alarms[day % 7] = RTCCalculateSeconds(0, hour, minute, second);

			*(uint16_t *)(0x40006c04 + ((day % 7) * 0x04)) = alarms[day % 7] / 60;

			if((day % 7) == ((rtc_get_counter_val() % DAY_LENGTH) % 7)){
				rtc_set_alarm_time(RTCCalculateSeconds(rtc_get_counter_val() % DAY_LENGTH, hour, minute, second));
			}

		}else{
			USARTWrite("alarm set: invalid usage\n	time set <day of week> [hour] [minute] [second]\n");

		}
	}else{
		// Get
		uint32_t day, hour, minute, second = rtc_get_alarm_val();
		RTCCalculateTime(&second, &day, &hour, &minute);

		// Display the currently set alarm (waiting to trigger)
		USARTWriteInt(rtc_get_alarm_val());
		USARTWrite("\nAlarm set for:\n");
		switch(day % 7){
			case 0:
				USARTWrite("Mon");
				break;
			case 1:
				USARTWrite("Tue");
				break;
			case 2:
				USARTWrite("Wed");
				break;
			case 3:
				USARTWrite("Thr");
				break;
			case 4:
				USARTWrite("Fri");
				break;
			case 5:
				USARTWrite("Sat");
				break;
			case 6:
				USARTWrite("Sun");
				break;
		}
		USARTWrite(" - ");
		USARTWriteInt(hour);
		USARTWrite(":");
		USARTWriteInt(minute);
		USARTWrite(":");
		USARTWriteInt(second);

		USARTWriteByte('\n');


		// Display alarms for each day of the week
		USARTWrite("\nAlarms:\n");
		for(int i = 0; i < 7; i++){
			second = alarms[i];
			RTCCalculateTime(&second, &day, &hour, &minute);
			switch(i){
				case 0:
					USARTWrite("Mon");
					break;
				case 1:
					USARTWrite("Tue");
					break;
				case 2:
					USARTWrite("Wed");
					break;
				case 3:
					USARTWrite("Thr");
					break;
				case 4:
					USARTWrite("Fri");
					break;
				case 5:
					USARTWrite("Sat");
					break;
				case 6:
					USARTWrite("Sun");
					break;
			}
			USARTWrite(" - ");
			USARTWriteInt(hour);
			USARTWrite(":");
			USARTWriteInt(minute);
			USARTWrite(":");
			USARTWriteInt(second);
			USARTWrite("\n");

		}
		USARTWriteByte('\n');
	}
}

void FuncPing(const char *command_buffer){
	for(int i = 0; i < StrToInt(FindChar(command_buffer, ' ') + 1, ' '); i++){
		USARTWrite("pong\n");
	}
}



