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

static  char *configfilepath = 0;
static  char *globalpath = 0;
static  char *localpath = 0;

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
  size_t noOfNames = getAllFeaturesCount();
  printf("Features in config file:\n");
  for (size_t featureNameIndex = 0 ; featureNameIndex < noOfNames ; featureNameIndex++) {
    printf("       %s:\n", getFeatureName(featureNameIndex));
    size_t noOfValues = getFeatureValuesCount(featureNameIndex);
    for (size_t featureValueIndex = 0; featureValueIndex < noOfValues; featureValueIndex++)
      printf("              %s\n", getFeatureValueName(featureNameIndex, featureValueIndex));
  }
}

void initializeDataPaths() {
  if (globalpath)
    return;
  char *remotesubpath = "/conf/remotes/";
  
  // Setup config default config file path
  globalpath = (char*) malloc(strlen(DATADIR)+strlen(remotesubpath));
  strcpy(globalpath, DATADIR);
  strcat(globalpath, remotesubpath);

  // Get current working directory
  char *tmplocalpath = getcwd(NULL,0); 
  localpath = (char*) malloc(strlen(tmplocalpath)+strlen(remotesubpath));
  strcpy(localpath, tmplocalpath);
  strcat(localpath, remotesubpath);
  free(tmplocalpath);
}

void loadConfigfile(char *configfile) {
 if (configfilepath)
    return;
 
  // Initialize data paths
  initializeDataPaths();

  // setup path
  char *suffix = ".json";
  size_t pathlength = strlen(globalpath)>strlen(localpath)?strlen(globalpath):strlen(localpath);
  configfilepath = (char*) malloc(pathlength+strlen(configfile)+strlen(suffix)+2);
  strcpy(configfilepath, localpath);
  strcat(configfilepath, configfile);
  strcat(configfilepath, suffix);
  if (access( configfilepath, F_OK ) == -1 ) {
    strcpy(configfilepath, globalpath);
    strcat(configfilepath, configfile);
    strcat(configfilepath, suffix);
  }
  
  // load config file
  loadConfig(configfilepath);

  // Find max number of features
  size_t configFeaturesCount = getAllFeaturesCount();

  //Declace and initialize feature name and for now empty values
  requestedFeatures = (featureT*)malloc(sizeof(featureT)*configFeaturesCount);
  for (int i=0 ; i<configFeaturesCount ; i++) {
    requestedFeatures[i].name = 0;
    requestedFeatures[i].value = 0;
  }

  
}

void insertFeatureValue(const char *Name, const char *Value) {
  // prepare
  static int nextFeatureIndex = 0;
  
  // add feature
  if (hasFeatureNameAndValue(Name, Value)) {
    requestedFeatures[nextFeatureIndex].name = Name;
    requestedFeatures[nextFeatureIndex].value = Value;
  } else {
    printf("Feature '%s'='%s' does not match configuration file\n",
	   Name, Value);
    print_available_features();
    usage();
  }
}

void printAvailableConfigFilesInPath(const char *filepath) {
    struct dirent *de; // Pointer for directory entry 

    // Guardian
    if (access( filepath, F_OK ) == -1 ) {
      printf("No available configuration files in %s\n", filepath);
      return;
    }

    // opendir() returns a pointer of DIR type.
    DIR *dr = opendir(filepath); 

    if (dr == NULL) // opendir returns NULL if couldn't open directory 
      { 
	log_fatal("Could not open current directory %s", filepath);
	usage();
      } 

    // Refer http://pubs.opengroup.org/onlinepubs/7990989775/xsh/readdir.html 
    // for readdir()
    int headerOutput = false;
    while ((de = readdir(dr)) != NULL) {
      char *head = strtok(de->d_name, ".");
      char *suffix = strtok(NULL, ".");
      char *tail = strtok(NULL, ".");
      if (head && strcmp("json",suffix)==0  && !tail) {
	if (!headerOutput) {
	  printf("Available configuration files in %s:\n", filepath);
	  headerOutput = true;
	}
	printf("    %s\n", head);
      }
    }
    if (!headerOutput)
      printf("No available configuration files in %s\n", filepath);
    closedir(dr);	 
}

void printAvailableConfigFiles() {
  printAvailableConfigFilesInPath(localpath);
  printAvailableConfigFilesInPath(globalpath);
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
      loadConfigfile(optarg);
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
  const char *description = 0;
  unsigned int frequency = 0;
  double dutycycle = 0;
  unsigned int outPin = 0; // The Broadcom pin number the signal will be sent on
  char *symbolString = 0;

  size_t noOfConfigSymbols = getSymboldefinitionsCount();
  symbolDefinition configSymbols[noOfConfigSymbols];

  log_info("Configuration '%s' - %s", configfilepath, description);
  loadConfigSymbols(configSymbols);
  loadFeaturedCode(requestedFeatures,
		   &symbolString);
  loadDescription(&description);
  loadFrequency(&frequency);
  loadDutyCycle(&dutycycle);
  log_info("%s", description);

  // Remote specific plugin 
  if (strstr(configfilepath, "hvac_panasonic") != NULL) {
    hvac_panasonic_plugin(symbolString);
  }
  log_debug("Transmitting: %s", symbolString);  
  // Load RaspbeeryPI outPin number from config file
  loadOutPin(&outPin);
  
  // Transmit symbol string
  int result = irSlingGeneric(
			      (uint32_t)outPin,
			      frequency,
			      dutycycle,
			      noOfConfigSymbols,
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


