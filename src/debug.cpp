#include "debug.h"

#include <xlib/core/watchdog.h>
#include "storage/storage.h"

FIL debug_file;
volatile uint8_t debug_file_open;
Timer debug_timer;
uint32_t debug_last_pc;

uint32_t EEMEM ee_last_pc;
uint16_t EEMEM ee_min_stack;
uint16_t EEMEM ee_max_heap;

volatile uint16_t debug_return_pointer;
volatile uint16_t debug_min_stack_pointer = 0xFFFF;
volatile uint16_t debug_max_heap_pointer = 0x0000;

void debug_print_ram()
{
	DEBUG("\noffset ");
	for (uint8_t i = 0; i < 16; i++)
		DEBUG(" +%01X", i);

	for (uint16_t i=0x2000; i<0x5FFF; i++)
	{
		ewdt_reset();
		uint8_t * tmp = (uint8_t *)(i);
		if (i % 0x10 == 0)
			DEBUG("\n0x%04X: ", i);
		DEBUG("%02X ", *tmp);

		if (i == 7)
			DEBUG(" ");
	}
}

void debug_last_dump()
{
	DEBUG("\nLast WDT reset\n");

	eeprom_busy_wait();
	debug_last_pc = eeprom_read_dword(&ee_last_pc);

	DEBUG(" PC: 0x%06lX (byte address)\n", debug_last_pc);
	DEBUG(" Min stack: 0x%04X\n", eeprom_read_word(&ee_min_stack));
	DEBUG(" Max heap: 0x%04X\n", eeprom_read_word(&ee_max_heap));
}

void debug_timeout_handler()
{
//	DEBUG("[debug_timeout_handler]\n");

//	DEBUG("RP: %04X\n", debug_return_pointer);
//	DEBUG("RP adr: %04X\n", &debug_return_pointer);

	uint8_t r0 = *((uint8_t *)(debug_return_pointer + 0));
	uint8_t r1 = *((uint8_t *)(debug_return_pointer + 1));
	uint8_t r2 = *((uint8_t *)(debug_return_pointer + 2));

//	DEBUG("RA: %02X %02X %02X\n", r0, r1, r2);

	//push decrement PC
	uint32_t ra = (((uint32_t)r0 << 16) & 0x00FF0000) | (((uint16_t)r1 <<  8) & 0xFF00) | (r2);
	//word to byte
	ra = ra * 2;

	//store this info
	eeprom_busy_wait();
	eeprom_update_dword(&ee_last_pc, ra);
	eeprom_update_word(&ee_min_stack, debug_min_stack_pointer);
	eeprom_update_word(&ee_max_heap, debug_max_heap_pointer);

	DEBUG(" *** Warning: I feel WDT is near! ***\n");
	debug_last_dump();

//	debug_print_ram();
//	for(;;);
}


ISR(DEBUG_TIMER_OVF, ISR_NAKED)
{
	//saving all call clobbered
	asm volatile(
		"push r1" "\n\t"			//save "zero" register
		"push r0" "\n\t"			//save tmp register

		"in	r0, __SREG__" "\n\t" 	//save SREG to R0
		"cli" "\n\t"				//disable interrupts (ASAP, so SP will not be changed, hopefully)
		"push r0" "\n\t" 			//store SREG to STACK

		"eor r1, r1" "\n\t" 		//zero "zero" register

		"in	r0, 0x3B" "\n\t" 		//save RAMPZ to R0
		"push r0" "\n\t" 			//store RAMPZ to STACK

		"push r18" "\n\t" 			//store call clobbered
		"push r19" "\n\t"
		"push r20" "\n\t"
		"push r21" "\n\t"
		"push r22" "\n\t"
		"push r23" "\n\t"
		"push r24" "\n\t"
		"push r25" "\n\t"
		"push r26" "\n\t"
		"push r27" "\n\t"
		"push r30" "\n\t"
		"push r31" "\n\t"
		::);
	debug_return_pointer = SP + 17; // 16x push (IRQ prologue) + 1 (SP is pointing to next available location)

	sei(); //enable interrupts since handler is using uart and spi
	debug_timeout_handler();

	asm volatile(
		"pop r31" "\n\t"			//pop call clobbered
		"pop r30" "\n\t"
		"pop r27" "\n\t"
		"pop r26" "\n\t"
		"pop r25" "\n\t"
		"pop r24" "\n\t"
		"pop r23" "\n\t"
		"pop r22" "\n\t"
		"pop r21" "\n\t"
		"pop r20" "\n\t"
		"pop r19" "\n\t"
		"pop r18" "\n\t"

		"pop r0" "\n\t"				//RAMPZ STACK->R0
		"out 0x3B, r0" "\n\t"		//restore RAMPZ

		"pop r0" "\n\t"				//RAMPZ SREG->R0
		"out __SREG__, r0" "\n\t"	//restore SREG

		"pop r0" "\n\t"				//restore tmp
		"pop r1" "\n\t"				//restore zero
		"reti" "\n\t"
	::);
}

void debug_timer_init()
{
	DEBUG_TIMER_PWR_ON;
	debug_timer.Init(DEBUG_TIMER, timer_div1024);
	debug_timer.SetTop(0xDBBA); //1.8s
	debug_timer.EnableInterrupts(timer_overflow);
	debug_timer.SetInterruptPriority(MEDIUM);
	debug_timer.Start();
}

void debug_log(char * msg)
{
	//disabled
	return;

	if (!storage_ready())
		return;

	uint8_t res;
	uint16_t wt;
	uint8_t len;

	//Append file if not opened
	if (!debug_file_open)
	{
		//open or create new
		res = f_open(&debug_file, DEBUG_FILE, FA_WRITE | FA_OPEN_ALWAYS);
		if (res != FR_OK)
			return;

		//seek to end
		res = f_lseek(&debug_file, f_size(&debug_file));
		if (res != FR_OK)
		{
			f_close(&debug_file);
			return;
		}

		//Timestamp
		uint8_t sec, min, hour, day, wday, month;
		uint16_t year;
		char tmp[64];

		datetime_from_epoch(time_get_actual(), &sec, &min, &hour, &day, &wday, &month, &year);

		sprintf_P(tmp, PSTR(" *** %02d.%02d.%04d %02d:%02d.%02d ***\n"), day, month, year, hour, min, sec);

		len = strlen(tmp);
		res = f_write(&debug_file, tmp, len, &wt);
		if (res != FR_OK || wt != len)
		{
			f_close(&debug_file);
			return;
		}

		//file is ready
		debug_file_open = true;

		debug_last_dump();
	}

	//write content
	len = strlen(msg);
	res = f_write(&debug_file, msg, len, &wt);
	if (res != FR_OK || wt != len)
	{
		f_close(&debug_file);
		debug_file_open = false;

		return;
	}

	//sync file
	res = f_sync(&debug_file);
	if (res != FR_OK || wt != len)
	{
		f_close(&debug_file);
		debug_file_open = false;
	}
}

void ewdt_init()
{
	wdt_init(wdt_2s);
	debug_timer_init();
}

void ewdt_reset()
{
	wdt_reset();
	debug_timer.SetValue(0);
}
