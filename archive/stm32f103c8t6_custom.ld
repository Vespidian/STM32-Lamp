MEMORY{
	rom 	(rx)	: ORIGIN = 0x08000000, LENGTH = 64k
	ram		(rxw)	: ORIGIN = 0x20000000, LENGTH = 20k
}

/* Enforce emmition of the vector table. */
EXTERN (vector_table)

/* Define the entry point of the output file. */
ENTRY(reset_handler)

SECTIONS{
	. = 0x0;

	.text : {
		*(.vectors)	/*	Vector table should be at the start of flash */
		*(.text*)	/*	Contains the main program code */
		. = ALIGN(4);
		/*	Contains read-only data.
			constants, strings, and other unchanging information */
		*(.rodata*)
		. = ALIGN(4);
	} > rom

	/* ram, but not cleared on reset, eg boot/app comms */
	.noinit (NOLOAD) : {
		*(.noinit*)
	} >ram
	. = ALIGN(4);

	/*	Space set aside for variables and other changing information */
	.data : {
		. = ALIGN(4);
		_data_s = .;
		*(.data)
		*(.data*)
		_data_e = .;
		. = ALIGN(4);
	} > ram AT > rom
	_data_flash_addr = LOADADDR(.data);

	/*	Similar to 'data' section except, it is initialized to all 0s */
	.bss : {
		_bss_s = .;
		*(.bss)
		_bss_e = .;
	} > ram
}
