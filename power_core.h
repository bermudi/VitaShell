/*
  Portable power-lock refcount shared by VitaShell and host tests.

  The Vita wrapper (utils.c) serializes every access with a single
  SceKernelLwMutex and performs the SceShellUtil lock/unlock transitions
  while holding that mutex. The host test mirrors the same pattern with a
  pthread mutex. The core itself is pure state so the 0->1 / 1->0 transition
  logic is testable without Vita headers.

  Invariants:
  - count tracks overlapping holders; shell_locked mirrors (count > 0).
  - powerStateLock() reports 1 exactly on the 0->1 transition: the caller
    acquires the PS-button guard once, while holding the shared mutex.
  - powerStateUnlock() reports 1 exactly on the 1->0 transition: the caller
    releases the guard once, while holding the shared mutex. Unlocking an
    idle state is a no-op and never reports a release, so an unmatched
    unlock cannot drop another operation's guard.
*/

#ifndef __POWER_CORE_H__
#define __POWER_CORE_H__

typedef struct {
  int count;
  int shell_locked;
} PowerState;

void powerStateInit(PowerState *state);

/* Returns 1 when the caller must acquire the shell guard (0->1). */
int powerStateLock(PowerState *state);

/* Returns 1 when the caller must release the shell guard (1->0). */
int powerStateUnlock(PowerState *state);

/* Returns non-zero while at least one holder is active. */
int powerStateShouldTick(const PowerState *state);

#endif
