#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"
#include "state.h"

#define STATE_FILE "toggle-state"

static char *join(const char *a, const char *b) {
  char *path = malloc(strlen(a) + strlen(b) + 2);
  sprintf(path, "%s/%s", a, b);
  return path;
}

char *toggleStateFile(void) {
  const char *dir = getenv("IRLINGO_STATE_DIR");
  if (dir && *dir)
    return join(dir, STATE_FILE);
  if (geteuid() == 0)
    return strdup("/var/lib/irlingo/" STATE_FILE);
  const char *xdg = getenv("XDG_STATE_HOME");
  if (xdg && *xdg) {
    char *irlingoDir = join(xdg, "irlingo");
    char *path = join(irlingoDir, STATE_FILE);
    free(irlingoDir);
    return path;
  }
  const char *home = getenv("HOME");
  if (home && *home)
    return join(home, ".local/state/irlingo/" STATE_FILE);
  return NULL;
}

// Create the directories leading to path
static int makeParents(const char *path) {
  char *copy = strdup(path);
  for (char *p = copy + 1; *p; p++) {
    if (*p != '/')
      continue;
    *p = 0;
    if (mkdir(copy, 0755) && errno != EEXIST) {
      free(copy);
      return -1;
    }
    *p = '/';
  }
  free(copy);
  return 0;
}

// Find the state of remote in the open file f; -1 if not listed
static int findState(FILE *f, const char *remote) {
  char line[4200];
  int state = -1;
  rewind(f);
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\n")] = 0;
    if ((line[0] == '0' || line[0] == '1') && line[1] == ' ' && strcmp(line + 2, remote) == 0)
      state = line[0] - '0';
  }
  return state;
}

int readToggleState(const char *stateFile, const char *remote) {
  if (!stateFile)
    return 0;
  FILE *f = fopen(stateFile, "r");
  if (!f)
    return 0;
  flock(fileno(f), LOCK_SH);
  int state = findState(f, remote);
  fclose(f);
  return state < 0 ? 0 : state;
}

int writeToggleState(const char *stateFile, const char *remote, int state) {
  if (!stateFile || makeParents(stateFile)) {
    log_warn("Cannot store the toggle state in %s", stateFile ? stateFile : "(no directory)");
    return -1;
  }
  int fd = open(stateFile, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    log_warn("Cannot store the toggle state in %s: %s", stateFile, strerror(errno));
    return -1;
  }
  flock(fd, LOCK_EX);
  FILE *f = fdopen(fd, "r+");

  // Copy all other remotes, then add this one
  char *content = NULL;
  size_t size = 0;
  FILE *out = open_memstream(&content, &size);
  char line[4200];
  rewind(f);
  while (fgets(line, sizeof(line), f)) {
    char copy[4200];
    snprintf(copy, sizeof(copy), "%s", line);
    copy[strcspn(copy, "\n")] = 0;
    if (strlen(copy) > 2 && strcmp(copy + 2, remote) == 0)
      continue;
    fputs(line, out);
    if (!strchr(line, '\n'))
      fputc('\n', out);
  }
  fprintf(out, "%d %s\n", state ? 1 : 0, remote);
  fclose(out);

  int result = 0;
  rewind(f);
  if (fwrite(content, 1, size, f) != size || fflush(f) || ftruncate(fd, (off_t)size)) {
    log_warn("Cannot store the toggle state in %s: %s", stateFile, strerror(errno));
    result = -1;
  }
  free(content);
  fclose(f);   // also releases the lock
  return result;
}
