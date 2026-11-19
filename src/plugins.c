#include <string.h>
#include "plugins.h"
#include "hvac_panasonic_plugin.h"

int applyRemotePlugin(const char *plugin, char *symbolString) {
  if (!plugin)
    return 0;
  if (strcmp(plugin, "hvac_panasonic") == 0) {
    hvac_panasonic_plugin(symbolString);
    return 0;
  }
  return 1;
}
