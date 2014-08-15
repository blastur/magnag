
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/power.h>
#include <util/delay.h>
#include <util/atomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include "usb_keyboard_debug.h"
#include "print.h"

//#define DEBUG
//#define RESET_BOOTCOUNT

#define DEBUG_PRINT(x) print(x)
#define DEBUG_NUM(x) phex16(x)

#ifdef DEBUG
	#define ONE_SECOND (1000)
	#define PRANK_INITIAL_DELAY (1) /* Bootup delay (s) */
	#define PRANK_MIN_DELAY (0)  /* Minimum delay (s) between pranks */
	#define PRANK_UPTIME_TRIGGER (10)
	#define PRANK_UPTIME_CRAZY (60)
#else
	#define ONE_SECOND (1000)
	#define PRANK_INITIAL_DELAY (60) /* Bootup delay (s) */
	#define PRANK_MIN_DELAY (10)  /* Minimum delay (s) between pranks */
	#define PRANK_UPTIME_TRIGGER (518400U) /* 6 days */
	#define PRANK_UPTIME_CRAZY (864000U) /* 10 days */
#endif

uint16_t EEMEM nv_bootcount = 0;
uint16_t EEMEM nv_seed = 0xCAFE;

volatile uint16_t ticks = 0;
volatile uint32_t __uptime = 0;

#define TICKS_PER_SECOND (F_CPU / 64)
#define MINS_TO_SECS(x) ((x)*60)
#define ROLL_URL "http://goo.gl/EqyxaA"
#define TAUNT_STRING "Magnus is the champion of the world!"

static void led_init(void)
{
	DDRD |= (1 << 6);
}

static void led_toggle(void)
{
	PORTD ^= (1 << 6);
}

/* Uptime, in milliseconds */
static uint32_t current_uptime(void)
{
	uint32_t uptime;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		uptime = __uptime;
	}

	return uptime;
}

/* Delay arbitrary number of seconds */
static void long_delay(int seconds)
{
	int i;
	DEBUG_PRINT("Longdelay(0x");
	DEBUG_NUM(seconds);
	DEBUG_PRINT(")\n");

	for (i = 0; i < seconds; i++)
		_delay_ms(ONE_SECOND);
}

/* Generate integer 0..N-1; N must be << RAND_MAX (0x7FFF)*/
static int random_int(const uint16_t N)
{
	return (rand() / (RAND_MAX / N + 1));
}

static void do_rickroll(void)
{
	DEBUG_PRINT("Rickroll!\n");
	usb_keyboard_press(KEY_R, KEY_LEFT_GUI);
	_delay_ms(500);
	usb_keyboard_putstr(ROLL_URL);
	usb_keyboard_press(KEY_ENTER, 0);
}

static void do_nonintrusive_key(void)
{
	DEBUG_PRINT("Non-intrusive key\n");
	switch(random_int(11))
	{
	case 0:
	case 1:
		usb_keyboard_press(KEY_CAPS_LOCK, 0);
		break;
	case 2:
	case 3:
		usb_keyboard_press(KEY_PAGE_UP, 0);
		break;
	case 4:
	case 5:
		usb_keyboard_press(KEY_PAGE_DOWN, 0);
		break;
	case 6:
		usb_keyboard_press(KEY_DOWN, 0);
		break;
	case 7:
		usb_keyboard_press(KEY_LEFT, 0);
		break;
	case 8:
		usb_keyboard_press(KEY_UP, 0);
		break;
	case 9:
		usb_keyboard_press(KEY_RIGHT, 0);
		break;
	case 10:
		usb_keyboard_press(KEY_M, KEY_LEFT_GUI);
		break;
	}
}

static void do_taunt(void)
{
	DEBUG_PRINT("Taunt\n");
	usb_keyboard_press(KEY_D, KEY_LEFT_ALT);
	usb_keyboard_putstr(TAUNT_STRING);
}

static void do_prank(uint16_t bootcount, uint32_t uptime)
{
	if (bootcount > 10 || uptime >= PRANK_UPTIME_CRAZY) {
		/* 11..inf boots: rickrolls + high rate keystrokes + taunts */
		switch(random_int(10))
		{
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
				do_nonintrusive_key();
				long_delay(random_int(MINS_TO_SECS(2)));
				break;
			case 6:
			case 7:
			case 8:
				do_rickroll();
				long_delay(MINS_TO_SECS(1) + random_int(MINS_TO_SECS(3)));
				break;
			case 9:
				do_taunt();
				long_delay(random_int(MINS_TO_SECS(2)));
				break;
		}
	} else if (uptime >= PRANK_UPTIME_TRIGGER) {
		/* Start sending keystrokes if machine is not rebooted for a while */
		uint32_t time_left_until_crazy = PRANK_UPTIME_CRAZY - uptime;
		uint32_t sane_time_period = PRANK_UPTIME_CRAZY - PRANK_UPTIME_TRIGGER;
		float sanity_left = (time_left_until_crazy / (float) (sane_time_period));
		uint16_t max_delay = (uint16_t) (MINS_TO_SECS(5) * sanity_left);
		DEBUG_PRINT("Sanity delay is 0x");
		DEBUG_NUM(max_delay);
		DEBUG_PRINT("\n");
		do_nonintrusive_key();
		long_delay(MINS_TO_SECS(1) + random_int(max_delay));
	} else if (bootcount >= 5) {
		/* 5..10 boots: Send keystroke once every 1 .. 1+rand(10-bootcount) mins */
		do_nonintrusive_key();
		long_delay(MINS_TO_SECS(1) + random_int(MINS_TO_SECS(10 - bootcount)));
	} else {
		/* Less than 5 boots and a few days of uptime; do nothing! */
		DEBUG_PRINT("NO-OP\n");
		for (int i = 0; i < 10; i++)
		{
			led_toggle();
			_delay_ms(100);
		}
	}
}

int main(void)
{
	uint16_t bootcount;
	uint16_t seed;

	/* Run CPU at 1 MHz (16MHz with 16 div) */
	clock_prescale_set(clock_div_16);

	/* Read and update bootcount from EEPROM */
	bootcount = eeprom_read_word(&nv_bootcount);
#ifdef RESET_BOOTCOUNT
	eeprom_write_word(&nv_bootcount, 0);
	bootcount = 0xFF;
#else
	eeprom_write_word(&nv_bootcount, bootcount + 1);
#endif

	/* Initialize random "seed" */
	seed = eeprom_read_word(&nv_seed);
	srand(seed);
	eeprom_write_word(&nv_seed, (uint16_t) rand());

	/* Init LED */
	led_init();

	/* Setup timer1 at F_CPU / 64 */
	TCCR1A = 0x00;
	TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10); /* CTC mode + Prescaler 64*/
	/* Set Output compare TOP */
	OCR1AH = (TICKS_PER_SECOND >> 8);
	OCR1AL = (TICKS_PER_SECOND & 0xFF);
	TIMSK1 = _BV(OCIE1A); /* Enable Output Compare channnel A irq */

	/* Initialize the USB subsystem */
	usb_init();
	while (!usb_configured()) /* wait */ ;

	/* Print current bootcount to HID debug channel */
	print("Bootcount is 0x");
	phex16(bootcount);
	print("\nSeed is 0x");
	phex16(seed);
	print("\n");

	/* Wait for OS to boot, and stuff.. */
	DEBUG_PRINT("Initial delay..\n");
	long_delay(PRANK_INITIAL_DELAY);

	/* Main loop */
	DEBUG_PRINT("Initiating mainloop..\n");
	while (1) {
		uint32_t uptime = current_uptime();
		DEBUG_PRINT("Uptime is 0x");
		DEBUG_NUM(uptime >> 16);
		DEBUG_NUM(uptime & 0xFFFF);
		DEBUG_PRINT("\n");

		do_prank(bootcount, uptime);
		long_delay(PRANK_MIN_DELAY);
	}
}

ISR(TIMER1_COMPA_vect)
{
	__uptime++;
	led_toggle();
}
