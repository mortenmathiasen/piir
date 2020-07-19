#include <string.h>
#include "config.h"
#include "parson.h"
#include "log.h"

/* char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
    } */

JSON_Object* getRootObject(const char *configfilepath) {
  JSON_Value *root_value = json_parse_file(configfilepath);
  JSON_Object *root_object = 0;
  if (json_value_get_type(root_value) != JSONObject)
    log_error("JSON file %s root not an object",configfilepath);
  else
    root_object = json_value_get_object(root_value);
  return root_object;
}

JSON_Object* getFeaturesObject(const char *configfilepath) {
  JSON_Object *root_object = getRootObject(configfilepath);
  JSON_Object *features_object = json_object_get_object(root_object, "features");

  if (!features_object)
    log_error("JSON file %s features object within root object",configfilepath);
  return features_object;
}

size_t getFeaturesCount(const char *configfilepath) {
  return json_object_get_count(getFeaturesObject(configfilepath));
}

const char * getFeaturesName(const char *configfilepath, size_t index) {
  JSON_Object *features_object = getFeaturesObject(configfilepath);
  const char *result = json_object_get_name(features_object, index);
  return result;
}

JSON_Object * getSymboldefinitionsObject(const char *configfilepath) {
  JSON_Object *root_object = getRootObject(configfilepath);
  return json_object_get_object(root_object, "symboldefinitions");
}

size_t getSymboldefinitionsCount(const char * configfilepath) {
  JSON_Object *symboldefs_object = getSymboldefinitionsObject(configfilepath);
  return json_object_get_count(symboldefs_object);
}

void loadConfig(const char *configfilepath,
		const char **features,
		const char **description,
		unsigned int *frequency,
		double *dutycycle,
		char **symbolString,
		symbolDefinition configSymbols[]) {
  *description = 0;
  *frequency = 0;
  *dutycycle = 0;
  *symbolString = 0;

  //Load root object
  JSON_Object *root_object = getRootObject(configfilepath);

  //Load roots simple members
  *description = json_object_get_string(root_object, "description");
  *frequency = json_object_get_number(root_object, "frequency");
  *dutycycle = json_object_get_number(root_object, "dutycycle");

  //Load symbol definitions
  JSON_Object *symbolsdef_object = json_object_get_object(root_object, "symboldefinitions");
  if (!symbolsdef_object)
    log_error("Symbol definitions configuraiton not found");
  else {
    size_t noOfSymbolDefs = json_object_get_count(symbolsdef_object);
    for (size_t i=0 ; i<noOfSymbolDefs ; i++) {
      const char * symbol = json_object_get_name(symbolsdef_object, i);
      JSON_Object *symboldef_object = json_object_get_object(symbolsdef_object, symbol);
      configSymbols[i].symbol = symbol[0];
      /* char markPath[7]; */
      /* char spacePath[8]; */
      /* sprintf(markPath,"%s.mark",symbol); */
      /* sprintf(spacePath,"%s.space",symbol); */
      /* JSON_Value * val = json_object_get_value(symbolsdef_object,markPath); */
      configSymbols[i].markDuration = json_object_get_number(symboldef_object,"mark");
      configSymbols[i].spaceDuration = json_object_get_number(symboldef_object,"space");
    }
  }
  
  //Load template code
  *symbolString = strdup(json_object_get_string(root_object, "template"));
  size_t symbolStrLength = strlen(*symbolString);
  
  //Apply requested features to symbol string
  JSON_Object *features_object = json_object_get_object(root_object, "features");
  size_t featuresArrayLength = json_object_get_count(features_object);
  for (int featureNumber=0 ; featureNumber<featuresArrayLength ; featureNumber++) {
    const char *featureValue = features[featureNumber];
    if (!featureValue)
      continue;
    const char *featureName = getFeaturesName(configfilepath, featureNumber);
    char featureValuePath[sizeof(featureName)+1+sizeof(featureValue)+1];
    sprintf(featureValuePath, "%s.%s", featureName, featureValue);
    const char *featureString = json_object_dotget_string(features_object, featureValuePath);
    size_t featureStrLength = strlen(featureString);
    if (featureStrLength!=symbolStrLength) {
      log_warn("Length of feature code '%s' does match 'template' length %zu",
		featureString, symbolStrLength);
    }
    for (int charNumber=0 ; charNumber<featureStrLength && charNumber<symbolStrLength ; charNumber++) {
      if (featureString[charNumber]!=' ')
	(*symbolString)[charNumber] = featureString[charNumber];
    }
  }
  
  return;
}

void loadOutPin(const char *configfilepath,
		unsigned int *outPin)   //Output
{
  //Load root object
  JSON_Object *root_object = getRootObject(configfilepath);

  //Load pin number
  double outPinNumber = json_object_dotget_number(root_object, "transmitter.outpin");
  *outPin = (unsigned int)outPinNumber;
}
