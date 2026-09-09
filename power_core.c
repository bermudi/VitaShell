/*
  Portable power-lock refcount shared by VitaShell and host tests.
*/

#include "power_core.h"

void powerStateInit(PowerState *state) {
  state->count = 0;
  state->shell_locked = 0;
}

int powerStateLock(PowerState *state) {
  int need_shell_lock = (state->count == 0);
  state->count++;
  if (need_shell_lock)
    state->shell_locked = 1;
  return need_shell_lock;
}

int powerStateUnlock(PowerState *state) {
  if (state->count <= 0)
    return 0;
  state->count--;
  if (state->count == 0 && state->shell_locked) {
    state->shell_locked = 0;
    return 1;
  }
  return 0;
}

int powerStateShouldTick(const PowerState *state) {
  return state->count > 0;
}
