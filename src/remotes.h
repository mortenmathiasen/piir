#ifndef REMOTES_H
#define REMOTES_H

#include <stddef.h>

// Remote configuration files are JSON files in one or more remote
// directories, optionally in subdirectories, e.g. sony/RM-ED009.json.
// A remote is named by its path below the remote directory without the
// .json suffix ("sony/RM-ED009"), or just by its file name ("RM-ED009")
// when that is unique.

typedef struct {
  char **names;   // sorted remote names, e.g. "sony/RM-ED009"
  size_t count;
} remoteListT;

// List the remotes in the given directories. A name found in more than one
// directory is listed once.
void listRemotes(const char *const dirs[], size_t dirCount, remoteListT *list);
void freeRemoteList(remoteListT *list);

// Find the file for a remote name. Returns the number of matching remotes:
// 1 and the file path in *path (to be freed) when found, 0 when not found,
// and more than 1 when a file name matches remotes in several directories;
// their names are then returned in *matches (when not NULL).
size_t findRemote(const char *const dirs[], size_t dirCount, const char *name,
		  char **path, remoteListT *matches);

#endif
