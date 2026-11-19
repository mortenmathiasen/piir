#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "remotes.h"

static const char SUFFIX[] = ".json";

static void addName(remoteListT *list, const char *name) {
  list->names = realloc(list->names, (list->count + 1) * sizeof(char *));
  list->names[list->count++] = strdup(name);
}

static int hasSuffix(const char *name) {
  size_t len = strlen(name), suffixLen = strlen(SUFFIX);
  return len > suffixLen && strcmp(name + len - suffixLen, SUFFIX) == 0;
}

static char *joinPath(const char *a, const char *b) {
  size_t la = strlen(a);
  char *path = malloc(la + strlen(b) + 2);
  strcpy(path, a);
  if (la && a[la - 1] != '/')
    strcat(path, "/");
  strcat(path, b);
  return path;
}

// Collect "prefix/name" (without suffix) for every JSON file below dir
static void scan(const char *dir, const char *prefix, remoteListT *list, int depth) {
  if (depth > 8)
    return;
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *entry;
  while ((entry = readdir(d))) {
    if (entry->d_name[0] == '.')
      continue;
    char *path = joinPath(dir, entry->d_name);
    char *name = *prefix ? joinPath(prefix, entry->d_name) : strdup(entry->d_name);
    struct stat st;
    if (stat(path, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
	scan(path, name, list, depth + 1);
      } else if (S_ISREG(st.st_mode) && hasSuffix(name)) {
	name[strlen(name) - strlen(SUFFIX)] = 0;
	addName(list, name);
      }
    }
    free(name);
    free(path);
  }
  closedir(d);
}

static int compareNames(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void sortUnique(remoteListT *list) {
  if (!list->count)
    return;
  qsort(list->names, list->count, sizeof(char *), compareNames);
  size_t out = 1;
  for (size_t i = 1; i < list->count; i++) {
    if (strcmp(list->names[i], list->names[out - 1]) == 0)
      free(list->names[i]);
    else
      list->names[out++] = list->names[i];
  }
  list->count = out;
}

void listRemotes(const char *const dirs[], size_t dirCount, remoteListT *list) {
  list->names = NULL;
  list->count = 0;
  for (size_t i = 0; i < dirCount; i++)
    if (dirs[i])
      scan(dirs[i], "", list, 0);
  sortUnique(list);
}

void freeRemoteList(remoteListT *list) {
  for (size_t i = 0; i < list->count; i++)
    free(list->names[i]);
  free(list->names);
  list->names = NULL;
  list->count = 0;
}

static char *remoteFile(const char *dir, const char *name) {
  char *base = joinPath(dir, name);
  char *path = malloc(strlen(base) + strlen(SUFFIX) + 1);
  strcpy(path, base);
  strcat(path, SUFFIX);
  free(base);
  return path;
}

size_t findRemote(const char *const dirs[], size_t dirCount, const char *name,
		  char **path, remoteListT *matches) {
  *path = NULL;
  if (matches) {
    matches->names = NULL;
    matches->count = 0;
  }
  if (!*name || strstr(name, ".."))
    return 0;

  // Full name below a remote directory, e.g. "sony/RM-ED009"
  for (size_t i = 0; i < dirCount; i++) {
    if (!dirs[i])
      continue;
    char *file = remoteFile(dirs[i], name);
    if (access(file, F_OK) == 0) {
      *path = file;
      return 1;
    }
    free(file);
  }

  // Unique file name in any subdirectory, e.g. "RM-ED009"
  if (strchr(name, '/'))
    return 0;
  remoteListT all;
  listRemotes(dirs, dirCount, &all);
  remoteListT found = {NULL, 0};
  for (size_t i = 0; i < all.count; i++) {
    const char *slash = strrchr(all.names[i], '/');
    if (slash && strcmp(slash + 1, name) == 0)
      addName(&found, all.names[i]);
  }
  freeRemoteList(&all);

  size_t count = found.count;
  if (count == 1)
    findRemote(dirs, dirCount, found.names[0], path, NULL);
  if (matches && count > 1)
    *matches = found;
  else
    freeRemoteList(&found);
  return count;
}
