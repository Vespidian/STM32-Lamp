/* Linker script for an STM32F103C8T6 board (64K flash, 20K RAM). */

/* Define memory regions. */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Include the common ld script. */
INCLUDE cortex-m-generic.ld
