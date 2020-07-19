#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include "irslinger.h"
#include "log.h"
#include "config.h"

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

/* typedef struct p { */
/*   char *text; */
/*   int ircode; */
/*   int position;  */
/* } parameter; */

/* parameter legal_temp[] = {{"8",0,0}, {"10",0,0}, {"16",0,0}, {"17",0,0}, {"18",0,0}, {"19",0,0}, {"20",0,0}, {"21",0,0}, {"22",0,0}, {"23",0,0}, {"24",0,0}, {"25",0,0}, {"26",0,0}, {"27",0,0}, {"28",0,0}, {"29",0,0}, {"30",0,0}, {0, 0, 0}}; */
/* parameter legal_mode[] = {{"AUTO",0,0}, {"HEAT",0,0}, {"COOL",0,0}, {"FAN",0,0}, {"OFF",0,0}, {0, 0, 0}}; */
/* parameter legal_option[] = {{"AUTO",0,0}, {"QUIET",0,0}, {"POWERFUL",0,0}}; */
/* parameter legal_fan[] = {{"AUTO",0,0}, {"VERYSLOW",0,0}, {"SLOW",0,0}, {"MEDIUM",0,0}, {"FAST",0,0}, {"VERYFAST",0,0}, {0, 0, 0}}; */
/* parameter legal_updown[] = {{"AUTO",0,0}, {"VERYLOW",0,0}, {"LOW",0,0}, {"MIDDLE",0,0}, {"HIGH",0,0}, {"VERYHIGH",0,0}, {0, 0, 0}}; */
/* parameter legal_leftright[] = {{"AUTO",0,0}, {"VERYLEFT",0,0}, {"LEFT",0,0}, {"MIDDLE",0,0}, {"RIGHT",0,0}, {"VERYRIGHT",0,0}, {0, 0, 0}}; */

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

void insertFeatureValue(const char *Name, const char *Value, const char **featureNames, const char **featureValues) {
  size_t noOfNames = sizeof(featureNames);
  size_t noOfValues = sizeof(featureValues);
  size_t featurePosition = 0;
  for ( ; featurePosition < noOfNames ; featurePosition++)
    if (strcmp(Name,featureNames[featurePosition])==0)
      break;
  if (featurePosition<noOfNames && featurePosition<noOfValues)
    featureValues[featurePosition] = Value;
  else
    log_error("Feature '%s'='%s' does not match configuration",
	      Name, Value);
}

uint8_t readLeftBinary(const char * str, int *index) {
  uint8_t result = 0;
  while (str[*index]!='0' && str[*index]!='1')
    *index = *index + 1;
  for (int i=8 ; i>=0 ; i--) {
    result = result << 1;
    result += str[i+(*index)]=='1';
  }
  *index = *index + 8;
  return result;
}

int main(int argc, char *argv[])  
{
  PROGRAMNAME = argv[0];
  int c;
  //  parameter *temp=20, *mode=&legal_mode[0], *option=&legal_option[0], *fan=&legal_fan[0], *updown=&legal_updown[0], *leftright=&legal_leftright[0];
  
  char configfilepath[256];
  strcpy(configfilepath, DATADIR);
  strcat(configfilepath, "/conf/remotes/hvac/panasonic_jke.json");
  size_t configFeaturesCount = getFeaturesCount(configfilepath);

  //Declace and initialize feature name and for now empty values
  const char *featureNames[configFeaturesCount];
  const char *featureValues[configFeaturesCount];
  for (int i=0 ; i<configFeaturesCount ; i++) {
    featureNames[i] = getFeaturesName(configfilepath, i);
    featureValues[i] = 0;
  }

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

      switch (c)
        {
	case 't':
	  //temp = getParameter(optarg, legal_temp);
	  insertFeatureValue("temperature", optarg, featureNames,  featureValues);
	  break;
	case 'm':
	  //mode = getParameter(optarg, legal_mode);
	  insertFeatureValue("mode", optarg, featureNames,  featureValues);
	  break;
	case 'o':
	  //option = getParameter(optarg, legal_option);
	  insertFeatureValue("option", optarg, featureNames,  featureValues);
	  break;
	case 'f':
	  //fan = getParameter(optarg, legal_fan);
	  insertFeatureValue("fan", optarg, featureNames,  featureValues);
	  break;
	case 'u':
	  //updown = getParameter(optarg, legal_updown);
	  insertFeatureValue("updown", optarg, featureNames,  featureValues);
	  break;
	case 'l':
	  //leftright = getParameter(optarg, legal_leftright);
	  insertFeatureValue("leftright", optarg, featureNames,  featureValues);
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
  
  // fail if extra unknown arg
  if (optind < argc) {
    for(; optind < argc; optind++){      
      log_error("Unknown argument: %s\n", argv[optind]);  
    }
    usage();
  }

  // Log settings
  size_t noOfFeatures = getFeaturesCount(configfilepath);
  for (size_t featurePosition = 0 ; featurePosition < noOfFeatures ; featurePosition++)
    log_info("%s = %s",featureNames[featurePosition],featureValues[featurePosition]);
  
  const char *description = NULL;
  unsigned int frequency = 0;
  double dutycycle = 0;
  unsigned int outPin = 0; // The Broadcom pin number the signal will be sent on
  char *symbolString = NULL;

  int noOfConfigSymbols = getSymboldefinitionsCount(configfilepath);
  symbolDefinition configSymbols[noOfConfigSymbols];

  log_info("Configuration '%s' - %s", configfilepath, description);
  loadConfig(configfilepath,
	     featureValues,
	     &description,
	     &frequency,
	     &dutycycle,
	     &symbolString,
	     configSymbols);
  log_info("%s", description);

  // Calculate and insert checksum
  uint8_t checksum = 0b11110100;
  int index = 0;
  for (int i=0; i<26; i++)
    checksum += readLeftBinary(symbolString, &index);
  for (int i=0 ; i<8 ; i++) {
    while (symbolString[index]!='0' && symbolString[index]!='1')
      index++;
    symbolString[index+(7-i)] = (checksum << i) & 0b10000000 ? '1':'0';
  }
  log_debug("Transmitting: %s", symbolString);  

  // Load RaspbeeryPI outPin number from config file
  loadOutPin(configfilepath, &outPin);
  
  // Transmit symbol string
  int result = irSlingGeneric(
			      (uint32_t)outPin,
			      frequency,
			      dutycycle,
			      configSymbols,
			      symbolString
			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10010000-01010100-00000001-11110101-10110000-00000000-01110000-00000111-10000000-00000000-10000001-00000000-00000000-10100111-0" // Original 21 AUTO POWERFUL

			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10010000-01010100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-00100111-0" // Original 21 AUTO

			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10011100-01010100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-00101000-0" //Original 21 COOL
			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10010110-01101100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-00001010-0" //Original 21 FAN
			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10010000-00010100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-01000111-0" // Original 20 AUTO
			      //"H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-00010000-01010100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-11000111-0" //21 AUTO OFF
			      // "H-01000000-00000100-00000111-00100000-00000000-00000000-00000000-01100000-WH-01000000-00000100-00000111-00100000-00000000-10010000-00111100-00000001-11110101-10110000-00000000-01110000-00000111-00000000-00000000-10000001-00000000-00000000-01101111-0" // Original 30 AUTO
			      );
  
  return result;
}

