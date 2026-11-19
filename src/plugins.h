#ifndef PLUGINS_H
#define PLUGINS_H

// Apply remote specific post processing (e.g. checksums) to a symbol string
void applyRemotePlugins(const char *configfilepath, char *symbolString);

#endif
