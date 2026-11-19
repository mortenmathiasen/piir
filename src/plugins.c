#include <string.h>
#include "plugins.h"
#include "hvac_panasonic_plugin.h"

void applyRemotePlugins(const char *configfilepath, char *symbolString) {
  if (strstr(configfilepath, "hvac_panasonic") != NULL)
    hvac_panasonic_plugin(symbolString);
}
