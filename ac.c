#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "irslinger.h"

/*
 * gcc -g -o ac ac.c -lpigpio
 */

#define AUTO 0

#define MODE_HEAT 1
#define MODE_COOL 2
#define MODE_DRY 3

#define OPTION_QUIET = 1
#define OPTION_POWERFUL = 2

typedef struct p {
  char *text;
  int ircode;
  int position; 
} parameter;

parameter legal_temp[] = {{"08",0,0}, {"10",0,0}, {"16",0,0}, {"17",0,0}, {"18",0,0}, {"19",0,0}, {"20",0,0}, {"21",0,0}, {"22",0,0}, {"23",0,0}, {"24",0,0}, {"25",0,0}, {"26",0,0}, {"27",0,0}, {"28",0,0}, {"29",0,0}, {"30",0,0}};
parameter legal_mode[] = {{"AUTO",0,0}, {"HEAT",0,0}, {"COOL",0,0}, {"DRY",0,0}};
parameter legal_option[] = {{"AUTO",0,0}, {"QUIET",0,0}, {"POWERFUL",0,0}};
parameter legal_levels[] = {{"AUTO",0,0}, {"1",0,0}, {"2",0,0}, {"3",0,0}, {"4",0,0}, {"5",0,0}};
parameter *legal_fan = legal_levels;
parameter *legal_updown = legal_levels;
parameter *legal_leftright = legal_levels;

      static struct option long_options[] =
        {
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"temperature",  required_argument, 0, 't'},
          {"mode",  required_argument, 0, 'm'},
          {"option",  required_argument, 0, 'o'},
          {"fan",  required_argument, 0, 'f'},
          {"updown",  required_argument, 0, 'u'},
          {"leftright",  required_argument, 0, 'l'},
          {0, 0, 0, 0}
        };
            

void usage(char *programname) {
                printf("\nusage: %s\n", programname);
                printf("       --temperature, -t (08|10|16|17|..|30)\n");
                printf("       [--mode, -m (AUTO|HEAT|COOL|DRY)]\n");
                printf("       [--option, -o (AUTO|QUIET|POWERFUL)]\n");
                printf("       [--fan, -f (AUTO|1|2|3|4|5)]\n");
                printf("       [--updown, -u (AUTO|1|2|3|4|5)]\n");
                printf("       [--leftright, -l (AUTO|1|2|3|4|5)]\n");
		abort();
}

parameter* getParameter(char *value, parameter *optionlist) {
  size_t n = sizeof(optionlist);
  for (int i = 0 ;  i < n ; i++ ) {
    parameter *tempOpt = &optionlist[i];
    if (strcmp(tempOpt->text,value)==0)
      return tempOpt;
  }
  return 0;
}

int main(int argc, char *argv[])  
{
  int c;
  parameter *temp=0, *mode=&legal_mode[0], *option=&legal_option[0], *fan=&legal_fan[0], *updown=&legal_updown[0], *leftright=&legal_leftright[0];
  
  while (1)
    {
      int option_index = -1; // getopt_long stores the option index here. 
      c = getopt_long (argc, argv, ":t:m:o:f:u:l:",
                       long_options, &option_index);

      // detect the end of the options
      if (c == -1)
        break;

      /*  
	  printf ("option '%c'", c);
          if (optarg)
            printf (" with arg '%s'", optarg);
	  else
	    printf (" with no argument");
	  printf ("\n"); 
      */
	  
      switch (c)
        {
	case 't':
	  temp = getParameter(optarg, legal_temp);
	  break;
	case 'm':
	  mode = getParameter(optarg, legal_mode);
	  break;
	case 'o':
	  option = getParameter(optarg, legal_option);
	  break;
	case 'f':
	  fan = getParameter(optarg, legal_fan);
	  break;
	case 'u':
	  updown = getParameter(optarg, legal_updown);
	  break;
	case 'l':
	  leftright = getParameter(optarg, legal_leftright);
	  break;
        default:
	  printf("Unknown option '%c'", optopt);
	  usage(argv[0]);
        }
    }

    // fail if extra arg
    if (optind < argc) {
      for(; optind < argc; optind++){      
        printf("Unknown argument: %s\n", argv[optind]);  
      }
      usage(argv[0]);
    }
    // fail if missing temp
    if (!temp) {
      printf("Missing mandatory temperature argument\n");
      usage(argv[0]);
    }
      

	uint32_t outPin = 17;            // The Broadcom pin number the signal will be sent on
	int frequency = 38000;           // The frequency of the IR signal in Hz
	double dutyCycle = 0.5;          // The duty cycle of the IR signal. 0.5 means for every cycle,
	                                 // the LED will turn on for half the cycle time, and off the other half
	/*	int leadingPulseDuration = 9000; // The duration of the beginning pulse in microseconds
	int leadingGapDuration = 4500;   // The duration of the gap in microseconds after the leading pulse
	int onePulse = 562;              // The duration of a pulse in microseconds when sending a logical 1
	int zeroPulse = 562;             // The duration of a pulse in microseconds when sending a logical 0
	int oneGap = 1688;               // The duration of the gap in microseconds when sending a logical 1
	int zeroGap = 562;               // The duration of the gap in microseconds when sending a logical 0
	int sendTrailingPulse = 1;       // 1 = Send a trailing pulse with duration equal to "onePulse"
	                                 // 0 = Don't send a trailing pulse
					 */
	
	int
	  headerMarkDuration=3500,
	  headerSpaceDuration=1800,
	  widespaceMarkDuration=0,
	  widespaceSpaceDuration=10000,
	  zeroMarkDuration=420,
	  zeroSpaceDuration=470,
	  oneMarkDuration=420,
	  oneSpaceDuration=1350;

	symbolDefinition configSymbols[] = 
		{{'H',headerMarkDuration,headerSpaceDuration},
		 {'W',widespaceMarkDuration,widespaceSpaceDuration},
		 {'0',zeroMarkDuration,zeroSpaceDuration},
		 {'1',oneMarkDuration,oneSpaceDuration}};
	
	int result = irSlingGeneric(
		outPin,
		frequency,
		dutyCycle,
		configSymbols,
		"H0100000000000100000001110010000000000000000000000000000001100000WH01000000000001000000011100100000000000001001000001010100000000011111010110110000000000000111000000000111000000000000000010000001000000000000000000100111");
	
	return result;
}
