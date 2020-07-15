#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include "irslinger.h"
#include "log.h"


static const char *PROGRAMNAME = 0;

/*
 * gcc -g -o ac ac.c -lpigpio
 */

/* Flag set by ‘--verbose’. */

#define AUTO 0

#define MODE_HEAT 1
#define MODE_COOL 2
#define MODE_FAN 3
#define MODE_OFF 4

#define OPTION_QUIET = 1
#define OPTION_POWERFUL = 2

typedef struct p {
  char *text;
  int ircode;
  int position; 
} parameter;

parameter legal_temp[] = {{"8",0,0}, {"10",0,0}, {"16",0,0}, {"17",0,0}, {"18",0,0}, {"19",0,0}, {"20",0,0}, {"21",0,0}, {"22",0,0}, {"23",0,0}, {"24",0,0}, {"25",0,0}, {"26",0,0}, {"27",0,0}, {"28",0,0}, {"29",0,0}, {"30",0,0}, {0, 0, 0}};
parameter legal_mode[] = {{"AUTO",0,0}, {"HEAT",0,0}, {"COOL",0,0}, {"FAN",0,0}, {"OFF",0,0}, {0, 0, 0}};
parameter legal_option[] = {{"AUTO",0,0}, {"QUIET",0,0}, {"POWERFUL",0,0}};
parameter legal_fan[] = {{"AUTO",0,0}, {"VERYSLOW",0,0}, {"SLOW",0,0}, {"MEDIUM",0,0}, {"FAST",0,0}, {"VERYFAST",0,0}, {0, 0, 0}};
parameter legal_updown[] = {{"AUTO",0,0}, {"VERYLOW",0,0}, {"LOW",0,0}, {"MIDDLE",0,0}, {"HIGH",0,0}, {"VERYHIGH",0,0}, {0, 0, 0}};
parameter legal_leftright[] = {{"AUTO",0,0}, {"VERYLEFT",0,0}, {"LEFT",0,0}, {"MIDDLE",0,0}, {"RIGHT",0,0}, {"VERYRIGHT",0,0}, {0, 0, 0}};

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
   {"verbose", required_argument, 0, 'v'},
   {0, 0, 0, 0}
  };

void usage() {
  printf("\nusage: %s\n", PROGRAMNAME);
  printf("       --temperature, -t (8|10|16|17|..|30)\n");
  printf("       [--mode, -m (AUTO|HEAT|COOL|FAN|OFF)]\n");
  printf("       [--option, -o (AUTO|QUIET|POWERFUL)]\n");
  printf("       [--fan, -f (AUTO|VERYSLOW|SLOW|MEDIUM|FAST|VERYFAST)]\n");
  printf("       [--updown, -u (AUTO|VERYLOW|LOW|MIDDLE|HIGH|VERYHIGH)]\n");
  printf("       [--leftright, -l (AUTO|VERYLEFT|LEFT|MIDDLE|RIGHT|VERYRIGHT)]\n");
  printf("       [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)\n");
  abort();
}

parameter* getParameter(char *value, parameter *optionlist) {
  parameter *result = 0;
  for (int i = 0 ; optionlist[i].text ; i++ ) {
    parameter *tempOpt = &optionlist[i];
    if (0==(int)strcmp(tempOpt->text,value)) {
      result = tempOpt;
      break;
    }
  }
  if (result==0)
    {
      log_fatal("Parameter '%s' illegal", value);
      usage();
    }
  return result;
}

int main(int argc, char *argv[])  
{
  PROGRAMNAME = argv[0];
  int c;
  parameter *temp=0, *mode=&legal_mode[0], *option=&legal_option[0], *fan=&legal_fan[0], *updown=&legal_updown[0], *leftright=&legal_leftright[0];

  //Log only offending errors
  log_set_level(LOG_FATAL);
  
  while (1)
    {
      int option_index = -1; // getopt_long stores the option index here. 
      c = getopt_long (argc, argv, ":t:m:o:f:u:l:v:",
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
	case 'v':
	  //log_set_quiet(false);
	  log_set_level(log_level_int(optarg));
	  break;
        default:
	  printf("Unknown option '%c'", optopt);
	  usage();
        }
    }

  // fail if extra arg
  if (optind < argc) {
    for(; optind < argc; optind++){      
      printf("Unknown argument: %s\n", argv[optind]);  
    }
    usage();
  }

  // fail if missing temp
  if (!temp) {
    printf("Missing mandatory temperature argument\n");
    usage();
  }
      

  uint32_t outPin = 17;            // The Broadcom pin number the signal will be sent on
  int frequency = 38000;           // The frequency of the IR signal in Hz
  double dutyCycle = 0.5;          // The duty cycle of the IR signal. 0.5 means for every cycle,
	
  int
    headerMarkDuration=3515,
    headerSpaceDuration=1750,
    widespaceMarkDuration=435,
    widespaceSpaceDuration=10330,
    zeroMarkDuration=435,
    zeroSpaceDuration=435,
    oneMarkDuration=435,
    oneSpaceDuration=1310;

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
