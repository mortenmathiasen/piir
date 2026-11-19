#include <unistd.h>  
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>

#include "irgeneric.h"
#include "log.h"
#include "config.h"

#include "plugins.h"
#include "remotes.h"

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
   {"list", no_argument, 0, 'l'},
   {0, 0, 0, 0}
  };

void printAvailableConfigFiles();

void usage() {
  printf("\nusage:      %s\n", PROGRAMNAME);
  printf("            --remote, -r REMOTE\n");
  printf("            [--feature, -f NAME=VALUE]\n");
  printf("            [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)]\n");
  printf("            [--list, -l]\n");
  printf("            [--help, -h]\n");
  printf("\nREMOTE is a remote name from --list, e.g. sony/RM-ED011, a file name\n");
  printf("that is unique among the remotes, e.g. RM-ED011, or a path to a JSON file.\n");
  printf("\nexamples:   %s --help\n", PROGRAMNAME);
  printf("            %s --list\n", PROGRAMNAME);
  printf("            %s --remote hvac_panasonic --help\n", PROGRAMNAME);
  printf("            %s --remote hvac_panasonic --feature temperature=21\n", PROGRAMNAME);
  printf("            %s --remote sony/RM-ED011 --feature key=KEY_POWER\n", PROGRAMNAME);
  exit(EXIT_FAILURE);
}

void printAvailableFeatures() {
  assert("No features without configfile" && configfilepath);
  
  size_t noOfNames = getAllFeaturesCount();
  printf("Features in config file %s:\n", configfilepath);
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
  globalpath = (char*) malloc(strlen(DATADIR)+strlen(remotesubpath)+1);
  strcpy(globalpath, DATADIR);
  strcat(globalpath, remotesubpath);

  // Get current working directory
  char *tmplocalpath = getcwd(NULL,0); 
  localpath = (char*) malloc(strlen(tmplocalpath)+strlen(remotesubpath)+1);
  strcpy(localpath, tmplocalpath);
  strcat(localpath, remotesubpath);
  free(tmplocalpath);
}

void loadConfigfile(char *configfile) {
 if (configfilepath)
    return;
 
  // Initialize data paths
  initializeDataPaths();

  // An existing file path is used as is, otherwise the remote name is
  // looked up in the local and then the global remotes directory
  if (strchr(configfile, '/') && access( configfile, F_OK ) == 0) {
    configfilepath = strdup(configfile);
  } else {
    const char *dirs[] = {localpath, globalpath};
    remoteListT matches;
    size_t found = findRemote(dirs, 2, configfile, &configfilepath, &matches);
    if (found > 1) {
      printf("Remote '%s' is ambiguous, use one of:\n", configfile);
      for (size_t i = 0; i < matches.count; i++)
	printf("    %s\n", matches.names[i]);
      freeRemoteList(&matches);
      usage();
    }
    if (!found) {
      printf("Cannot find remote '%s'\n", configfile);
      printAvailableConfigFiles();
      usage();
    }
  }

  // load config file
  loadConfig(configfilepath);
  if (!isConfigLoaded()) {
    printf("Cannot load remote '%s' from %s\n", configfile, configfilepath);
    printAvailableConfigFiles();
    usage();
  }

  // Find max number of features
  size_t configFeaturesCount = getAllFeaturesCount();

  //Declace and initialize feature name and for now empty values
  requestedFeatures = (featureT*)malloc(sizeof(featureT)*configFeaturesCount);
  for (size_t i=0 ; i<configFeaturesCount ; i++) {
    requestedFeatures[i].name = 0;
    requestedFeatures[i].value = 0;
  }

  
}

void insertFeatureValue(const char *Name, const char *Value) {
  // Requested features are stored at their index in the configuration file,
  // so repeating a feature replaces the earlier value
  size_t featureIndex = 0;
  if (hasFeatureNameAndValue(Name, Value) && getFeatureIndex(Name, &featureIndex)) {
    requestedFeatures[featureIndex].name = Name;
    requestedFeatures[featureIndex].value = Value;
  } else {
    printf("Feature '%s'='%s' does not match configuration file\n",
	   Name, Value);
    printAvailableFeatures();
    usage();
  }
}

// Remotes are listed in full up to this number, otherwise --list does it
#define MAX_REMOTES_IN_HELP 40

void printAvailableConfigFiles() {
  const char *dirs[] = {localpath, globalpath};
  remoteListT list;
  listRemotes(dirs, 2, &list);
  if (list.count == 0)
    printf("No remotes found in %s or %s\n", localpath, globalpath);
  else if (list.count > MAX_REMOTES_IN_HELP)
    printf("%zu remotes available in %s and %s, list them with: %s --list\n",
	   list.count, localpath, globalpath, PROGRAMNAME);
  else {
    printf("Available remotes in %s and %s:\n", localpath, globalpath);
    for (size_t i = 0; i < list.count; i++)
      printf("    %s\n", list.names[i]);
  }
  freeRemoteList(&list);
}

void listAllRemotes() {
  const char *dirs[] = {localpath, globalpath};
  remoteListT list;
  listRemotes(dirs, 2, &list);
  size_t count = list.count;
  for (size_t i = 0; i < count; i++)
    printf("%s\n", list.names[i]);
  freeRemoteList(&list);
  exit(count ? EXIT_SUCCESS : EXIT_FAILURE);
}

int main(int argc, char *argv[])  
{
  PROGRAMNAME = argv[0];
  int helprequested = false;

  //Initialize installation paths
  initializeDataPaths();

  //Log only offending errors
  log_set_level(LOG_FATAL);

  // Load remote agnostic parameters
  while (1) {
    int option_index = -1; // getopt_long stores the option index here. 
    int c = getopt_long (argc, argv, ":r:v:hlf:",
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
      helprequested = true;
      break;

    case 'l':
      listAllRemotes();
      break;

    case 'f':
      if (!configfilepath) {
	printf("Missing configfile before feature parameter\n");
	printAvailableConfigFiles();
	usage();
      }
      {
	// Call strtok in sequence; argument evaluation order is unspecified
	const char *name = strtok(optarg, "=");
	const char *value = strtok(NULL, "=");
	if (!name || !value) {
	  printf("Feature must be given as NAME=VALUE\n");
	  usage();
	}
	insertFeatureValue(name, value);
      }
      break;

    default:
      printf("Unknown argument: %c %s %s\n", c, argv[optind], optarg);  
      usage();
    }
  }

  // Print help information
  if (helprequested) {
    if (!configfilepath)
      printAvailableConfigFiles();
    else
      printAvailableFeatures();
    usage();
  }
  
  // Initialze features if not done already
  if (!configfilepath) {
    printf("Missing configfile setting\n");
    printAvailableConfigFiles();
    usage();
  }

  // Load config file settings
  const char *description = 0;
  int frequency = 0;
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
  const char *plugin = 0;
  loadPlugin(&plugin);
  if (!plugin)
    plugin = legacyRemotePlugin(configfilepath);
  if (applyRemotePlugin(plugin, symbolString)) {
    printf("Unknown plugin '%s' in %s\n", plugin, configfilepath);
    exit(EXIT_FAILURE);
  }
  log_debug("Transmitting: %s", symbolString);  
  // Load outPin number from config file
  loadOutPin(&outPin);
  
  // Transmit symbol string
  int result = irSlingGeneric(
			      (uint32_t)outPin,
			      frequency,
			      dutycycle,
			      noOfConfigSymbols,
			      configSymbols,
			      symbolString
			      );
  
  return result;
}


