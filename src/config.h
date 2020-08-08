#ifndef CONFIG_H
#define CONFIG_H

#include "symboldefinition.h"

typedef struct q {
  const char *name;
  const char *value;
} featureT;

//Load configuration from file
void loadConfig(const char *configfilepath);
void unloadConfig();

size_t getSymboldefinitionsCount();

size_t getAllFeaturesCount();
const char * getFeatureName(size_t index);
int getFeatureIndex(const char *featureName, size_t *index);

size_t getFeatureValuesCount(size_t nameIndex);
const char * getFeatureValueName(size_t nameIndex, size_t valueIndex);
int hasFeatureNameAndValue(const char *featureName, const char *featureValue);

void loadConfigSymbols(symbolDefinition configSymbols[]);
void loadTemplateCode(const char **symbolString);
void loadFeaturedCode(const featureT *features,
		      char **featuredCode);
void loadOutPin(unsigned int *outPin);  //Output
void loadDescription(const char **description);
void loadFrequency(int *frequency);
void loadDutyCycle(double *dutycycle);

#endif
