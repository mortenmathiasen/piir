#ifndef STATE_H
#define STATE_H

// Remotes with a toggle bit flip it on every press. PIIR remembers the
// state of each remote between runs in a small file:
//   $PIIR_STATE_DIR/toggle-state if PIIR_STATE_DIR is set,
//   /var/lib/piir/toggle-state when run as root, otherwise
//   $XDG_STATE_HOME/piir/toggle-state or ~/.local/state/piir/toggle-state
// Each line holds a state (0 or 1) and the path of a remote file.

// The state file path, to be freed, or NULL if none can be determined
char *toggleStateFile(void);

// The toggle state of a remote, 0 when it has none yet or on errors
int readToggleState(const char *stateFile, const char *remote);

// Store the toggle state of a remote. Returns 0 on success.
int writeToggleState(const char *stateFile, const char *remote, int state);

#endif
