#ifndef CONFIG_H
#define CONFIG_H

#include "symboldefinition.h"

size_t getSymboldefinitionsCount(const char *configfilepath);

size_t getFeaturesCount(const char *configfilepath);

const char * getFeaturesName(const char *configfilepath, size_t index);

void loadConfig(const char *configfilepath,  // Input
		const char **features,  // Input
		const char **description,  //Output
		unsigned int *frequency,  //Output
		double *dutycycle,  //Output
		char **symbolString,  //Output
		symbolDefinition configSymbols[]);  //Output

void loadOutPin(const char *configfilepath,  // Input
		unsigned int *outPin);  //Output

#endif
