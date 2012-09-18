/*
	bitlash-unix.c: A minimal implementation of certain core Arduino functions	
	
	The author can be reached at: bill@bitlash.net

	Copyright (C) 2008-2012 Bill Roy

	Permission is hereby granted, free of charge, to any person
	obtaining a copy of this software and associated documentation
	files (the "Software"), to deal in the Software without
	restriction, including without limitation the rights to use,
	copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following
	conditions:
	
	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
	OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.
*/
#include "bitlash.h"

/*
Issues

BUG: background tasks stop sometimes; 100% cpu
	foreground unaffected

system() using printf()

full help text
boot segfaults ;)
delay should use nanosleep
command line

*/

#if _POSIX_TIMERS	// not on the Mac, unfortunately
struct timespec startup_time, current_time, elapsed_time;

// from http://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
struct timespec time_diff(timespec start, timespec end) {
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

void init_millis(void) {
	clock_gettime(CLOCK_REALTIME, &startup_time);
}

unsigned long millis(void) {
	clock_gettime(CLOCK_REALTIME, &current_time);	
	elapsed_time = time_diff(startup_time, current_time);
	return (elapsed_time.tv_sec * 1000UL) + (elapsed_time.tv_nsec / 1000000UL);
}
#else
#include <sys/time.h>

unsigned long startup_millis, current_millis, elapsed_millis;
struct timeval startup_time, current_time;

// after http://laclefyoshi.blogspot.com/2011/05/getting-nanoseconds-in-c-on-freebsd.html
void init_millis(void) {
	gettimeofday(&startup_time, NULL);
	startup_millis = (startup_time.tv_sec * 1000) + (startup_time.tv_usec /1000);
}

unsigned long millis(void) {
	gettimeofday(&current_time, NULL);
	current_millis = (current_time.tv_sec * 1000) + (current_time.tv_usec / 1000);
	elapsed_millis = current_millis - startup_millis;
	return elapsed_millis;
}

#endif


#if 0
// after http://stackoverflow.com/questions/4025891/create-a-function-to-check-for-key-press-in-unix-using-ncurses
//#include <ncurses.h>

int init_keyboard(void) {
	initscr();
	cbreak();
	noecho();
	nodelay(stdscr, TRUE);
	scrollok(stdscr, TRUE);
}

int serialAvailable(void) {
	int ch = getch();

	if (ch != ERR) {
		ungetch(ch);
		return 1;
	} 
	else return 0;
}

int serialRead(void) { return getch(); }
#endif

#if 0
//#include "conio.h"
int lookahead_key = -1;

int serialAvailable(void) { 
	if (lookahead_key != -1) return 1; 
	lookahead_key = mygetch();
	if (lookahead_key == -1) return 0;
	//printf("getch: %d ", lookahead_key);
	return 1;
}

int serialRead(void) {
	if (lookahead_key != -1) {
		int retval = lookahead_key;
		lookahead_key = -1;
		//printf("key: %d", retval);
		return retval;
	}
	return mygetch();
}
#endif

#if 1
int serialAvailable(void) { 
	return 0;
}

int serialRead(void) {
	return '$';
}

#endif
	
void spb (char c) {
	if (serial_override_handler) (*serial_override_handler)(c);
	else {
		putchar(c);
		//printf("%c", c);
		fflush(stdout);
	}
}
void sp(const char *str) { while (*str) spb(*str++); }
void speol(void) { spb(13); spb(10); }

numvar setBaud(numvar pin, unumvar baud) { return 0; }

// stubs for the hardware IO functions
//
unsigned long pins;
void pinMode(byte pin, byte mode) { ; }
int digitalRead(byte pin) { return ((pins & (1<<pin)) != 0); }
void digitalWrite(byte pin, byte value) {
	if (value) pins |= 1<<pin;
	else pins &= ~(1<<pin);
}
int analogRead(byte pin) { return 0; }
void analogWrite(byte pin, int value) { ; }
int pulseIn(int pin, int mode, int duration) { return 0; }

// stubs for the time functions
//
void delay(unsigned long ms) {
	unsigned long start = millis();
	while (millis() - start < ms) { ; }
}
void delayMicroseconds(unsigned int us) {;}

// fake eeprom
byte fake_eeprom[E2END];
void init_fake_eeprom(void) {
int i=0, fd;
	while (i <= E2END) eewrite(i++, 0xff);
}
byte eeread(int addr) { return fake_eeprom[addr]; }
void eewrite(int addr, byte value) { fake_eeprom[addr] = value; }

FILE *savefd;
void fputbyte(byte b) {
	fwrite(&b, 1, 1, savefd);	
}

numvar func_save(void) {
	char *fname = "eeprom";
	if (getarg(0) > 0) fname = (char *) getarg(1);
	savefd = fopen(fname, "w");
	if (!savefd) return 0;
	setOutputHandler(&fputbyte);
	cmd_ls();
	resetOutputHandler();
	fclose(savefd);
	return 1;
};



// background function thread
#include <pthread.h>
pthread_mutex_t executing;
pthread_t background_thread;
struct timespec wait_time;

void *BackgroundMacroThread(void *threadid) {
	for (;;) {
		pthread_mutex_lock(&executing);
		runBackgroundTasks();
		pthread_mutex_unlock(&executing);

		// sleep until next task runtime
		unsigned long sleep_time = millisUntilNextTask();
		if (sleep_time) {
			unsigned long seconds = sleep_time / 1000;
			wait_time.tv_sec = seconds;
			wait_time.tv_nsec = (sleep_time - (seconds * 1000)) * 1000000L;
			while (nanosleep(&wait_time, &wait_time) == -1) continue;
		}
	}
	return 0;
}


numvar func_system(void) {
	return system((char *) getarg(1));
}

numvar func_exit(void) {
	if (getarg(0) > 0) exit(getarg(1));
	exit(0);
}


int main () {
	init_fake_eeprom();
	addBitlashFunction("system", (bitlash_function) &func_system);
	addBitlashFunction("exit", (bitlash_function) &func_exit);
	addBitlashFunction("save", (bitlash_function) &func_save);
	init_millis();
	initBitlash(0);

	// run background functions on a separate thread
	pthread_create(&background_thread, NULL, BackgroundMacroThread, 0);

	// run the main stdin command loop
	for (;;) {
		char * ret = fgets(lbuf, STRVALLEN, stdin);
		if (ret == NULL) break;	
		doCommand(lbuf);
		initlbuf();
	}

#if 0
	unsigned long next_key_time = 0L;
	unsigned long next_task_time = 0L;

	for (;;) {
		if (millis() > next_key_time) {
//		if (1) {

			// Pipe the serial input into the command handler
			while (serialAvailable()) doCharacter(serialRead());
			next_key_time = millis() + 100L;
		}

		// Background macro handler: feed it one call each time through
		if (millis() > next_task_time) {
			if (!runBackgroundTasks()) next_task_time = millis() + 10L;
		}
	}
#endif

}