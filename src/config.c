#include <string.h>
#include "config.h"
#include "parson.h"
#include "log.h"

JSON_Object* getRootObject(const char *configfilepath) {
  static JSON_Object *root_object = 0;
  if (root_object)
    return root_object;

  JSON_Value *root_value = json_parse_file(configfilepath);
  if (json_value_get_type(root_value) != JSONObject)
    log_error("JSON file %s root not an object",configfilepath);
  else
    root_object = json_value_get_object(root_value);
  return root_object;
}

JSON_Object* getAllFeaturesObject(const char *configfilepath) {
  static JSON_Object *all_features_object = 0;
  if (all_features_object)
    return all_features_object;

  JSON_Object *root_object = getRootObject(configfilepath);
  all_features_object = json_object_get_object(root_object, "features");

  if (!all_features_object)
    log_error("JSON file %s features object within root object",configfilepath);

  return all_features_object;
}

size_t getAllFeaturesCount(const char *configfilepath) {
  return json_object_get_count(getAllFeaturesObject(configfilepath));
}

const char * getFeatureName(const char *configfilepath, size_t index) {
  JSON_Object *all_features_object = getAllFeaturesObject(configfilepath);
  const char *result = json_object_get_name(all_features_object, index);
  return result;
}

JSON_Object * getFeatureObject(const char *configfilepath, size_t index) {
  JSON_Object *all_features_object = getAllFeaturesObject(configfilepath);
  const char *feature_name = json_object_get_name(all_features_object, index);
  JSON_Object *feature_object = json_object_get_object(all_features_object, feature_name);
  return feature_object;
}

size_t getFeatureValuesCount(const char *configfilepath, size_t nameIndex) {
  JSON_Object *feature_object = getFeatureObject(configfilepath, nameIndex);
  return json_object_get_count(feature_object);
}

const char * getFeatureValueName(const char *configfilepath, size_t nameIndex, size_t valueIndex) {
  JSON_Object *feature_object = getFeatureObject(configfilepath, nameIndex);
  return json_object_get_name(feature_object, valueIndex);
}

int hasFeatureNameAndValue(const char *configfilepath, const char *featureName, const char *featureValue) {
  // Search for featureName
  size_t featuresCount = getAllFeaturesCount(configfilepath);
  size_t featureNameIndex=0;
  for (featureNameIndex=0 ; featureNameIndex<featuresCount ; featureNameIndex++)
    if (strcmp(featureName, getFeatureName(configfilepath, featureNameIndex))==0)
      break;
  if (featureNameIndex>=featuresCount)
    return false; //No featureName exists

  // Search for featureValue
  size_t valuesCount = getFeatureValuesCount(configfilepath, featureNameIndex);
  for (size_t valueIndex=0 ; valueIndex<valuesCount ; valueIndex++)
    if (strcmp(featureValue, getFeatureValueName(configfilepath, featureNameIndex, valueIndex))==0)
      return true;

  return false; //No featureName exists
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
		const featureT *features,
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
  for (size_t featureNumber=0 ; featureNumber<featuresArrayLength && features ; featureNumber++) {
    const char *featureValue = features[featureNumber].value;
    if (!featureValue)
      continue;
    const char *featureName = features[featureNumber].name;
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
