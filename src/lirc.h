#ifndef LIRC_H
#define LIRC_H

#include <stddef.h>
#include "symboldefinition.h"

// Maximum number of values the kernel accepts in one write (LIRCBUF_SIZE)
#define MAX_LIRC_VALUES 1024

int lircBuildPulses(size_t sdDefCount,
		    const symbolDefinition *sdDef,
		    const char *code,
		    unsigned int *buffer,
		    size_t *count);

#endif
