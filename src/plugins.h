#ifndef PLUGINS_H
#define PLUGINS_H

// Apply the remote specific post processing (e.g. checksums) named by the
// "plugin" setting of a remote to its symbol string. plugin may be NULL.
// Returns 0 on success and 1 for an unknown plugin.
int applyRemotePlugin(const char *plugin, char *symbolString);

#endif
