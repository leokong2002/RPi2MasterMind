#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

//Used to set GPIO memory mapping, and read/write to pins
#define BlockSize (4*1024)
#define GPIO_Base 0x3f200000
#define GPFSEL_OUTPUT 0x01
#define GPFSEL_INPUT 0x00


//LCD definitions
#define	INST_CLEAR	0x01
#define	INST_HOME	0x02
#define	INST_ENTRY	0x04
#define	INST_DISPLAY	0x08
#define	INST_SHIFT	0x10
#define	INST_FUNCSET	0x20
#define	INST_CGADDR	0x40
#define	INST_DDADDR	0x80

#define	ENTRY_SHIFT	0x01
#define	ENTRY_INC	0x02

#define DISPLAY_BLINK 0x01
#define	DISPLAY_CURSOR	0x02
#define	DISPLAY_DISPLAY	0x04

#define SHIFT_RIGHT 0x04
#define SHIFT_DISPLAY 0x08

#define	FUNCSET_FONT	0x04
#define	FUNCSET_ROWS	0x08
#define	FUNCSET_DATALEN	0x10

#define BLOCK_SIZE (4*1024)
#define BUTTON 19
#define TIMEOUT 4

//C and Assembly implementations of the function to set the mode of individual GPIO pins
 
 /*
void setMode (int pin, int mode) {

    int fSel    = pin/10;
    int shift   = (pin % 10) * 3;

    if (mode == INPUT)
        *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) ; // Sets bits to zero = input
    else if (mode == OUTPUT)
        *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) | (1 << shift) ;
}
*/

setMode(volatile int * gpio, int pin, unsigned char function){
	int GPFSEL = (pin/10)*4;
	int bitshift = (pin%10)*3;
	function = function & 0x07;
	asm(
	"	MOV R0, %[GPIO]\n"
	"	MOV R1, %[bitshift]\n"
	"	MOV R2, %[function]\n"
	"	ADD R0, %[GPFSEL]\n"
	"	LDR R3, [R0]\n"
	"	here:MOV R4, #7 @prepare 111\n"
	"	MVN R4, R4, LSL R1 @shift and negate 111\n"
	"	AND R3, R3, R4\n"
	"	ORR R2, R3, R2, LSL R1\n"
	"	STR R2, [R0]\n"
	:[function] "+r" (function)
	:[GPIO] "r" (gpio), [bitshift] "r" (bitshift), [GPFSEL] "r" (GPFSEL)
	:"r0", "r1", "r2", "r3", "r4", "cc"
	);
}

/*
writePin implementations in C and assembly (to directly set the value of GPIO pins)
 GPSET register for 1 
 GPCLR register for 0 
*/

/*
void writePin (int pin, int value) {
    // if write high = GPSET
    // if write low = GPCLR
    int offset = pin / 32;
    if (value == HIGH)
    {
        offset = offset + 7;    // 7 = GPSET0
        *(gpio + offset) = 1 << (pin & 31) ;
    }
    else
    {
        offset = offset + 10;   // 10 = GPCLR0
        *(gpio + offset) = 1 << (pin & 31) ;
    }
}
*/

writePin(volatile int * gpio, int pin, int state){
    int offset = pin % 32;
    int offset1 = pin / 32;
    if (state == 1)
    {
        offset1 = (offset1 + 7) * 4;    // 7 = GPSET0
        asm(
            "\tLDR R0, %[gpi]\n"
            "\tMOV R1, %[offset1]\n"
            "\tMOV R2, %[offset]\n"
            "\tMOV R3, R0\n"
            "\tADD R3, R1\n"
            "\tMOV R4, #1\n"
            "\tLSL R4, R2\n"
            "\tSTR R4, [R3]\n"
            :: [gpi] "m" (gpio),
               [offset] "r" (offset),
               [offset1] "r" (offset1)
            : "r0", "r1", "r2", "r3", "r4", "cc", "memory"
        );
    }
    else
    {
        offset1 = (offset1 + 10) * 4;    // 10 = GPSET0
        asm(
            "\tLDR R0, %[gpi]\n"
            "\tMOV R1, %[offset1]\n"
            "\tMOV R2, %[offset]\n"
            "\tMOV R3, R0\n"
            "\tADD R3, R1\n"
            "\tMOV R4, #1\n"
            "\tLSL R4, R2\n"
            "\tSTR R4, [R3]\n"
            :: [gpi] "m" (gpio),
               [offset] "r" (offset),
               [offset1] "r" (offset1)
            : "r0", "r1", "r2", "r3", "r4", "cc", "memory"
        );
    }
}


/*returns 0 or 1 depending on input value from GPIO pin*/


/*
int readPin (int pin) {
    int offset = (pin / 32) + 13; // 13 = GPLEV0

    if ((*(gpio + offset) & (1 << (pin & 31))) != 0)
      return 1;
    return 0;
}
*/

int readPin (volatile int * gpio, int pin) {
    int offset = ((pin / 32) + 13) * 4;
    int pinSet = pin % 32;
    int r;
    asm(
        "\tLDR R0, %[gpi]\n"
        "\tMOV R1, R0\n"
        "\tADD R1, %[offset]\n"
        "\tLDR R1, [R1]\n"
        "\tMOV R2, #1\n"
        "\tLSL R2, %[pinShift]\n"
        "\tAND %[r], R2, R1\n"
        : [r] "=r" (r)
        : [gpi] "m" (gpio),
          [offset] "r" (offset),
          [pinShift] "r" (pinSet)
        : "r0", "r1", "r2", "cc", "memory"
    );

    if (r != 0)
      return 1;
    return 0;
}


//creates a memory mapping between the raspberry pi GPIO pins and memory
volatile int * mapgpio(){
	volatile int * gpio;
	int directory;

	if ( (directory = open("/dev/mem",O_RDWR | O_SYNC | O_CLOEXEC)) < 0) {
		printf("cannot open directory");
		exit(0);
	}

	gpio = mmap(0, BlockSize, PROT_READ | PROT_WRITE , MAP_SHARED, directory, GPIO_Base);

	if ((int) gpio ==-1 ){

		exit(0);
	}

	return gpio;
}


//structs to load the data to be displayed on the LCD and manipulate them 
struct {
	int strobe, registerSelect;
	int data[4];
} typedef LCDData;

struct {
	LCDData * pins;
	int rows, columns, cursorX, cursorY;
} typedef LCD;

//read lcd pins
lcdStrobe(volatile int * gpio, LCDData * pins){
	writePin(gpio, pins->strobe, 1);
	usleep(200);
	writePin(gpio, pins->strobe, 0);
	usleep(200);
}


//sends bits of data to the specified pins on the LCD
lcd4Bits(volatile int * gpio, LCDData * pins, unsigned char data){
	int i;
	for (i=0;i<4;i++){
		writePin(gpio, pins->data[i], (data&1));
		data>>=1;
	}
	
	lcdStrobe(gpio, pins);
	
}

lcd8Bits(volatile int * gpio, LCDData * pins, unsigned char data){
	//Note, send high four bits first.
	lcd4Bits(gpio, pins, ((data>>4) & 0x0F));
	lcd4Bits(gpio, pins, (data & 0x0F));
}


//sending commands to LCD
lcdCommand(volatile int * gpio, LCD * screen, unsigned char command){
	writePin(gpio, screen->pins->registerSelect, 0);
	lcd8Bits(gpio, screen->pins, command);
}

//adjust cursor 
lcdSyncCursor(volatile int * gpio, LCD * screen){
	lcdCommand(gpio, screen, screen->cursorX + (INST_DDADDR | (screen->cursorY==1 ? 0x40 : 0x00)));
}

//return the carriage
lcdCarriageReturn(volatile int * gpio, LCD * screen){
	screen->cursorX = 0;
	lcdSyncCursor(gpio, screen);
}

//feed data into a line
lcdLineFeed(volatile int * gpio, LCD * screen){
	screen->cursorY++;
	if(screen->cursorY >= screen->rows){
		screen->cursorY = 0;
	}
	
	lcdSyncCursor(gpio, screen);
}

//take new line
lcdNewLine(volatile int * gpio, LCD * screen){
	lcdLineFeed(gpio, screen);
	lcdCarriageReturn(gpio, screen);
}

//clear the LCD
lcdClear(volatile int * gpio, LCD * screen){
	lcdCommand(gpio, screen, INST_CLEAR);
	usleep(50000);
	screen->cursorX =0;
	screen->cursorY =0;
}

//HOME the LCD
lcdHome(volatile int * gpio, LCD * screen){
	lcdCommand(gpio, screen, INST_HOME);
	usleep(50000);
	screen->cursorX =0;
	screen->cursorY =0;
}

/*
 * Command to write 8 bits of data to DDRAM/CGRAM
 */
lcdWrite(volatile int * gpio, LCD * screen, unsigned char data){
	writePin(gpio, screen->pins->registerSelect, 1);
	lcd8Bits(gpio, screen->pins, data);
	screen->cursorX++;
}

//print a string to the lcd
lcdPutString(volatile int * gpio, LCD * screen, char * string, int lineWrapping, int udelay){
	while (*string){
		lcdWrite(gpio, screen, *string++);
		if (lineWrapping && screen->cursorX == screen->columns)
			lcdNewLine(gpio, screen);
		
		usleep(udelay);
	}
}


//initialise the lcd
lcdInitialise(volatile int * gpio, LCD * screen){
	usleep(50000);
	lcd4Bits(gpio, screen->pins, 3);
	usleep(10000);
	lcd4Bits(gpio, screen->pins, 3);
	usleep(10000);
	lcd4Bits(gpio, screen->pins, 3);
	usleep(10000);
	lcd4Bits(gpio, screen->pins, 2); //4-bit mode here
	
	
	lcdCommand(gpio, screen, INST_FUNCSET | FUNCSET_ROWS );
	lcdCommand(gpio, screen, INST_DISPLAY);
	lcdCommand(gpio, screen, INST_CLEAR);
	lcdCommand(gpio, screen, INST_ENTRY | ENTRY_INC);
	usleep(10000);

	
	lcdCommand(gpio, screen, INST_DISPLAY | DISPLAY_DISPLAY);
	
	usleep(10000);
}


//initial set up of lcd attaching to relevant pins
LCDData * lcdPinSetter(volatile int * gpio, int strobe, int regselect, int * data){
	LCDData * pins = malloc(sizeof(LCDData));
	if (pins == NULL){
		printf("cannot allocate enough space");
		exit(0);
	}
	
	writePin(gpio, strobe, 0);
	setMode(gpio, strobe, GPFSEL_OUTPUT);
	pins->strobe = strobe;
	
	writePin(gpio, regselect, 0);
	setMode(gpio, regselect, GPFSEL_OUTPUT);
	pins->registerSelect = regselect;
	
	int i;
	
	for (i=0; i<4; i++){
		writePin(gpio, data[i], 0);
		setMode(gpio, data[i], GPFSEL_OUTPUT);
		pins->data[i] = data[i];
	}
	
	return pins;
	
}

//set up lcd with a given number of rows and columns
LCD * lcdFactory(int rows, int columns, LCDData * pins){
	
	if (rows>2){
		printf("lcd has only 2 rows");
		exit(0);
	}
	
	LCD * screen = malloc(sizeof(LCD));
	if (screen == NULL) {
		printf("cannot allocate enogh space");
		exit(0);
	}
		
	screen->rows = rows;
	screen->columns = columns;
	screen->cursorX = 0;
	screen->cursorY = 0;
	screen->pins = pins;
	
	return screen;
} 

static volatile int * gpio;
static LCD * screen;
static int redPin, greenPin;
static volatile uint32_t *timer;
static volatile unsigned int timerBase;

//lcd results
lcdShowResult(int exact, int approximate){
	char string[20];

	lcdClear(gpio, screen);


	sprintf(string, "Exact: %d", exact);
	lcdPutString(gpio, screen, string, 0, 0);

	lcdNewLine(gpio, screen);

	sprintf(string, "Approximate: %d", approximate);
	lcdPutString(gpio, screen, string, 0, 0);

}

//lcd success message
lcdSuccess(int attempts){
	char string[20];

	lcdClear(gpio, screen);
	lcdPutString(gpio, screen, "Success!", 0, 350000);
	lcdNewLine(gpio, screen);

	sprintf(string, "Attempts: %d", attempts);
	lcdPutString(gpio, screen, string, 0, 0);
}


//lcd message while in game
lcdInputPrompt(){
	lcdClear(gpio, screen);
	lcdHome(gpio, screen);
	lcdPutString(gpio, screen, "Make your guess", 0, 0);
}


//flash a led attached to a pin, a given amount of times
pinFlash(int pin, int times){
	int i;

	for (i=0; i<times; i++){
		writePin(gpio, pin, 1);
		usleep(500000);
		writePin(gpio, pin, 0);
		usleep(500000);
	}
}

//flash the red led given amount of times
redFlash(int times){
	pinFlash(redPin, times);
}

//flash the green led given amount of times
greenFlash(int times){
	pinFlash(greenPin, times);
}

//flash the red led once to show input received, then flash the green led the amount times input was pressed
ledInputRecieved(int input){
	redFlash(1);
	greenFlash(input);
}

//show results via the led
ledShowResult(int exact, int approximate){

	greenFlash(exact);
	redFlash(1);
	greenFlash(approximate);
	redFlash(3); //New round signifier
}

//flash green 3 times to show success
ledSuccess(){
	writePin(gpio, redPin, 1);
	greenFlash(3);
	writePin(gpio, redPin, 0);
}


//read the button input
int getButtonInput() {
    int in = 0;
    time_t stime;
    time(&stime);
    while ((time(NULL) - stime) < TIMEOUT) {
        if(readPin(gpio, BUTTON)){
			usleep(300000);
            in++;
		}
	}
	
    return in;
}

//initialise the lcd data pins and led pins, and set up the mode of the corresponding gpio pins
initialise(){
	int dataPins[4] = {23, 10, 27, 22};

	redPin = 5;
	greenPin = 13;

	gpio = mapgpio();

	screen = lcdFactory(2, 16, lcdPinSetter(gpio, 24, 25, dataPins));

	lcdInitialise(gpio, screen);

	setMode(gpio, redPin, GPFSEL_OUTPUT);
	writePin(gpio, redPin, 0);

	setMode(gpio, greenPin, GPFSEL_OUTPUT);
	writePin(gpio, greenPin, 0);
}

//show the start message 
start(){
	writePin(gpio, redPin, 1);
	writePin(gpio, greenPin, 1);

	lcdPutString(gpio, screen, "Mastermind", 0, 0);
	lcdNewLine(gpio, screen);
	usleep(500000);
	lcdPutString(gpio, screen, "ar111 & cwk2", 0, 250000);
	usleep(2000000);

	lcdClear(gpio, screen);
	lcdHome(gpio, screen);

	writePin(gpio, redPin, 0);
	writePin(gpio, greenPin, 0);

}

//good bye message before ending the game
byeMessage(){

	lcdClear(gpio, screen);
	lcdPutString(gpio, screen, "See you later!", 0, 0);
	usleep(2000000);
	lcdClear(gpio, screen);
}

struct {
	int exact;
	int approximate;
} typedef Result;


int debug_mode_boolean, codeLength;

//checks the result of the users guess
Result UsersAttempt(int * guess, int * answer){
	int index =0, approx=0, exact=0, inner;
	Result res= {0, 0};

	int * answercpy = malloc(sizeof(*answercpy) * codeLength);
	memcpy(answercpy, answer, sizeof(*answercpy) * codeLength);

	for (index=0;index<codeLength;index++){
		if (guess[index] == answercpy[index]){
			guess[index]= -1;
			answercpy[index]=-1;
			exact++;
		}
	}
	for (index=0;index<codeLength;index++){
		for (inner=0;inner<codeLength; inner++){
			if (inner!= index && guess[index] == answercpy[inner] && guess[index] != -1){
				guess[index]=-1;
				answercpy[inner]=-1;
				approx++;
			}
		}
	}

	free(answercpy);
	free(guess);

	res.exact=exact;
	res.approximate = approx;
	return res;
}

//receive the input from the button
int * getGuess(){
	int * guess;
	int i;

	guess = malloc(sizeof(*guess) * codeLength);
	for (i = 0; i<codeLength; i++){
		guess[i] = getButtonInput();
		if (debug_mode_boolean) printf("Input: %d\n", guess[i]);
		ledInputRecieved(guess[i]);
	}

	redFlash(2);

	return guess;
}

//show the result in lcd
void showResult(Result res){

	lcdShowResult(res.exact, res.approximate);
	ledShowResult(res .exact, res.approximate);

	if(debug_mode_boolean) printf("Exact: %d\nApproximate: %d\n", res.exact, res.approximate);
}

//generate a secret code which the user has to guess
int * generateCode(int colourCount){
	int * answer;
	answer = malloc(sizeof(*answer) * codeLength);
	int i;

	if (debug_mode_boolean) printf("Secret code: ");

	for (i=0; i<codeLength; i++){
		answer[i] = (rand() % colourCount) +1; //+1 required else answer code may include 0
		if (debug_mode_boolean) printf("%d  ", answer[i]);
	}

	if (debug_mode_boolean) printf("\n");

	return answer;
}





