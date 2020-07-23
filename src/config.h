#ifndef CONFIG_H
#define CONFIG_H

#include "symboldefinition.h"

typedef struct q {
  const char *name;
  const char *value;
} featureT;

size_t getSymboldefinitionsCount(const char *configfilepath);

size_t getAllFeaturesCount(const char *configfilepath);
const char * getFeatureName(const char *configfilepath, size_t index);
int getFeatureIndex(const char *configfilepath, const char *featureName, size_t *index);

size_t getFeatureValuesCount(const char *configfilepath, size_t nameIndex);
const char * getFeatureValueName(const char *configfilepath, size_t nameIndex, size_t valueIndex);
int hasFeatureNameAndValue(const char *configfilepath, const char *featureName, const char *featureValue);

void loadConfig(const char *configfilepath,  // Input
		const featureT *features,  // Input
		const char **description,  //Output
		unsigned int *frequency,  //Output
		double *dutycycle,  //Output
		char **symbolString,  //Output
		symbolDefinition configSymbols[]);  //Output

void loadOutPin(const char *configfilepath,  // Input
		unsigned int *outPin);  //Output

#endif
