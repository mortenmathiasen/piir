#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <dirent.h>

#include "irslinger.h"
#include "log.h"
#include "config.h"

#include "hvac_panasonic_plugin.h"

featureT *requestedFeatures = 0;

char *configfilepath = 0;
char *remotepath = 0;

static const char *PROGRAMNAME = 0;

typedef struct p {
  char *text;
  int ircode;
  int position; 
} parameter;

static struct option long_options[] =
  {
   /* These options don’t set a flag.
      We distinguish them by their indices. */
   {"remote",  required_argument, 0, 'r'},
   {"feature",  required_argument, 0, 'f'},
   {"verbose", required_argument, 0, 'v'},
   {"help", no_argument, 0, 'h'},
   {0, 0, 0, 0}
  };

void usage() {
  printf("\nusage:      %s\n", PROGRAMNAME);
  printf("            --remote, -r CONFIGFILE\n");
  printf("            [--feature, -f NAME=VALUE]\n");
  printf("            [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)]\n");
  printf("            [--help, -h]\n");
  printf("\nexamples:   %s --help\n", PROGRAMNAME);
  printf("            %s --remote hvac_panasonic --help\n", PROGRAMNAME);
  printf("            %s --remote hvac_panasonic --feature temperature=21\n", PROGRAMNAME);
  printf("            %s --remote tv_sony --feature action=TURN_ON\n", PROGRAMNAME);
  abort();
}

void print_available_features() {
  size_t noOfNames = getAllFeaturesCount(configfilepath);
  printf("Features in config file:\n");
  for (size_t featureNameIndex = 0 ; featureNameIndex < noOfNames ; featureNameIndex++) {
    printf("       %s:\n", getFeatureName(configfilepath,featureNameIndex));
    size_t noOfValues = getFeatureValuesCount(configfilepath,featureNameIndex);
    for (size_t featureValueIndex = 0; featureValueIndex < noOfValues; featureValueIndex++)
      printf("              %s\n", getFeatureValueName(configfilepath, featureNameIndex, featureValueIndex));
  }
}

void initializeDataPaths() {
  if (remotepath)
    return;
  
  // Setup config default config file path
  char *remotesubpath = "/conf/remotes/";
  remotepath = (char*) malloc(strlen(DATADIR)+strlen(remotesubpath));
  strcpy(remotepath, DATADIR);
  strcat(remotepath, remotesubpath);
}

void initializeConfigfilePath(char *configfile) {
  if (configfilepath)
    return;

  // Initialize data paths
  initializeDataPaths();

  // setup path
  char *suffix = ".json";
  configfilepath = (char*) malloc(strlen(remotepath)+strlen(configfile)+strlen(suffix));
  strcat(configfilepath, remotepath);
  strcat(configfilepath, configfile);
  strcat(configfilepath, suffix);
}

void initializeFeatureContainers(char *configfile) {
  // Initialize configfilepath
  initializeConfigfilePath(configfile);

  // Find max number of features
  size_t configFeaturesCount = getAllFeaturesCount(configfilepath);

  //Declace and initialize feature name and for now empty values
  requestedFeatures = (featureT*)malloc(sizeof(featureT)*configFeaturesCount);
  for (int i=0 ; i<configFeaturesCount ; i++) {
    requestedFeatures[i].name = 0;
    requestedFeatures[i].value = 0;
  }
}

void insertFeatureValue(const char *Name, const char *Value) {
  if (!configfilepath)
    return;
  initializeFeatureContainers(configfilepath);

  // prepare
  static int nextFeatureIndex = 0;
  
  // add feature
  if (hasFeatureNameAndValue(configfilepath, Name, Value)) {
    requestedFeatures[nextFeatureIndex].name = Name;
    requestedFeatures[nextFeatureIndex].value = Value;
  } else {
    printf("Feature '%s'='%s' does not match configuration file\n",
	   Name, Value);
    print_available_features();
    usage();
  }
}

void printAvailableConfigFiles() {
    struct dirent *de; // Pointer for directory entry 

    // opendir() returns a pointer of DIR type.
    DIR *dr = opendir(remotepath); 

    if (dr == NULL) // opendir returns NULL if couldn't open directory 
      { 
	log_fatal("Could not open current directory %s", remotepath);
	usage();
      } 

    // Refer http://pubs.opengroup.org/onlinepubs/7990989775/xsh/readdir.html 
    // for readdir()
    printf("Available configuration files:\n");
    while ((de = readdir(dr)) != NULL) {
      char *head = strtok(de->d_name, ".");
      char *suffix = strtok(NULL, ".");
      char *tail = strtok(NULL, ".");
      if (head && strcmp("json",suffix)==0  && !tail)
	printf("    %s\n", head); 
    }
    closedir(dr);	 
}

int main(int argc, char *argv[])  
{
  PROGRAMNAME = argv[0];
  int c;

  //Initialize installation paths
  initializeDataPaths();

  //Log only offending errors
  log_set_level(LOG_FATAL);

  // Load remote agnostic parameters
  while (1) {
    int option_index = -1; // getopt_long stores the option index here. 
    c = getopt_long (argc, argv, ":r:v:h:f:",
		     long_options, &option_index);

    // detect the end of the options
    if (c == -1)
      break;
	  
    switch (c) {
    case 'r':
      initializeConfigfilePath(optarg);
      break;
	  
    case 'v':
      log_set_level(log_level_int(optarg));
      break;
	  
    case 'h':
      usage();
      break;

    case 'f':
      if (!configfilepath) {
	printf("Missing configfile before feature parameter\n");
	printAvailableConfigFiles();
	usage();
      }
      insertFeatureValue(strtok(optarg, "="),
			 strtok(NULL, "="));
      break;

    default:
      printf("Unknown argument: %c %s %s\n", c, argv[optind], optarg);  
      usage();
    }
  }
  
  // Initialze features if not done already
  if (!configfilepath) {
    printf("Missing configfile setting\n");
    printAvailableConfigFiles();
    usage();
  }

  // Load config file settings
  const char *description = NULL;
  unsigned int frequency = 0;
  double dutycycle = 0;
  unsigned int outPin = 0; // The Broadcom pin number the signal will be sent on
  char *symbolString = NULL;

  int noOfConfigSymbols = getSymboldefinitionsCount(configfilepath);
  symbolDefinition configSymbols[noOfConfigSymbols];

  log_info("Configuration '%s' - %s", configfilepath, description);
  loadConfig(configfilepath,
	     requestedFeatures,
	     &description,
	     &frequency,
	     &dutycycle,
	     &symbolString,
	     configSymbols);
  log_info("%s", description);

  // Remote specific plugin 
  if (strstr(configfilepath, "hvac_panasonic") != NULL) {
    hvac_panasonic_plugin(symbolString);
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


