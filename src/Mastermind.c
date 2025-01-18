#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <time.h> 
#include <string.h> 
#include "Func_Imp.c"

//main logic of the program
void main(int argc, char ** argv){
	int * answer;
	int * guess;
	Result res;
	int opt;
	int colours;
	int attempts;

	debug_mode_boolean = 0;
	//code and colour length cab be adjusted here
	codeLength = 3;
	colours=3;
	attempts=0;
	srand(time(NULL)); 
	
	
//allocate space ato store the answer
	answer=malloc(sizeof(*answer) * codeLength);

//initialise the set up of the LCD pins and LED pins 
	initialise();


	
	while((opt=getopt(argc, argv, "d")) != -1){	
		if(opt == 'd')		
			debug_mode_boolean = 1;
	}

//generate a code
	answer = generateCode(colours);
	
	start();
	int success_bool = 0;
	while(success_bool == 0){
		lcdInputPrompt();		
		guess = getGuess();
		attempts++;
		
		res = UsersAttempt(guess, answer);
		if (res.exact == codeLength)
			success_bool = 1;
		if (success_bool == 0){
		showResult(res);
		usleep(100000);
		}
	}

	
	//on success end the program gracefully

	lcdSuccess(attempts);
	ledSuccess();
	usleep(1500000);
	byeMessage();
	exit(0);
}
