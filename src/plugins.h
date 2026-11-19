#ifndef PLUGINS_H
#define PLUGINS_H

// Apply the remote specific post processing (e.g. checksums) named by the
// "plugin" setting of a remote to its symbol string. plugin may be NULL.
// Returns 0 on success and 1 for an unknown plugin.
int applyRemotePlugin(const char *plugin, char *symbolString);

// The plugin for a remote without a "plugin" setting. PIIR 1.x and 2.0
// chose the plugin from the file name, so files from those versions keep
// working: a path containing "hvac_panasonic" uses that plugin.
// Returns NULL when no plugin applies.
const char *legacyRemotePlugin(const char *configfilepath);

#endif
