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

void loadConfigSymbols(const char *configfilepath,
		       symbolDefinition configSymbols[]);
void loadTemplateCode(const char *configfilepath,
		      const char **symbolString);
void loadFeaturedCode(const char *configfilepath,
		      const featureT *features,
		      char **featuredCode);
void loadOutPin(const char *configfilepath,  // Input
		unsigned int *outPin);  //Output
void loadDescription(const char *configfilepath,
		     const char **description);
void loadFrequency(const char *configfilepath,
		   int *frequency);
void loadDutyCycle(const char *configfilepath,
		   double *dutycycle);

#endif
