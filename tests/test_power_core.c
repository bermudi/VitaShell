/* Host test for the portable power-lock refcount (power_core). */

#include "power_core.h"

#include <pthread.h>
#include <stdio.h>

#define ARRAY_SIZE(values) (sizeof(values) / sizeof((values)[0]))
#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
    return 1; \
  } \
} while (0)

static int testSingleLockUnlockTransitions(void) {
  PowerState state;
  powerStateInit(&state);
  CHECK(state.count == 0);
  CHECK(state.shell_locked == 0);
  CHECK(powerStateShouldTick(&state) == 0);

  CHECK(powerStateLock(&state) == 1);
  CHECK(state.count == 1);
  CHECK(state.shell_locked == 1);
  CHECK(powerStateShouldTick(&state) != 0);

  CHECK(powerStateLock(&state) == 0);
  CHECK(state.count == 2);
  CHECK(state.shell_locked == 1);

  CHECK(powerStateUnlock(&state) == 0);
  CHECK(state.count == 1);
  CHECK(state.shell_locked == 1);
  CHECK(powerStateShouldTick(&state) != 0);

  CHECK(powerStateUnlock(&state) == 1);
  CHECK(state.count == 0);
  CHECK(state.shell_locked == 0);
  CHECK(powerStateShouldTick(&state) == 0);
  return 0;
}

static int testUnmatchedUnlockIsNoop(void) {
  PowerState state;
  powerStateInit(&state);
  CHECK(powerStateUnlock(&state) == 0);
  CHECK(state.count == 0);
  CHECK(state.shell_locked == 0);
  CHECK(powerStateLock(&state) == 1);
  CHECK(powerStateUnlock(&state) == 1);
  CHECK(powerStateUnlock(&state) == 0);
  CHECK(state.count == 0);
  CHECK(state.shell_locked == 0);
  return 0;
}

static int testOverlappingHoldersKeepGuard(void) {
  PowerState state;
  int shell_locks = 0;
  int shell_unlocks = 0;
  powerStateInit(&state);

  /* Two overlapping holders (e.g. FTP server + VPK install). */
  if (powerStateLock(&state))
    shell_locks++;
  if (powerStateLock(&state))
    shell_locks++;
  CHECK(shell_locks == 1);
  CHECK(state.count == 2);
  CHECK(state.shell_locked == 1);

  /* First holder finishes: guard must stay acquired. */
  if (powerStateUnlock(&state))
    shell_unlocks++;
  CHECK(shell_unlocks == 0);
  CHECK(state.count == 1);
  CHECK(state.shell_locked == 1);
  CHECK(powerStateShouldTick(&state) != 0);

  /* Second holder finishes: guard released exactly once. */
  if (powerStateUnlock(&state))
    shell_unlocks++;
  CHECK(shell_unlocks == 1);
  CHECK(state.count == 0);
  CHECK(state.shell_locked == 0);
  CHECK(shell_locks == shell_unlocks);
  return 0;
}

typedef struct {
  PowerState state;
  pthread_mutex_t mutex;
  int shell_lock_calls;
  int shell_unlock_calls;
  int invariant_violated;
} GuardedPower;

static void guardedInit(GuardedPower *guard) {
  powerStateInit(&guard->state);
  pthread_mutex_init(&guard->mutex, NULL);
  guard->shell_lock_calls = 0;
  guard->shell_unlock_calls = 0;
  guard->invariant_violated = 0;
}

static void guardedDestroy(GuardedPower *guard) {
  pthread_mutex_destroy(&guard->mutex);
}

static void checkGuardInvariant(GuardedPower *guard) {
  int expected_locked = guard->state.count > 0 ? 1 : 0;
  if (guard->state.count < 0)
    guard->invariant_violated = 1;
  if (!!guard->state.shell_locked != expected_locked)
    guard->invariant_violated = 1;
  if (guard->shell_lock_calls - guard->shell_unlock_calls != guard->state.shell_locked)
    guard->invariant_violated = 1;
}

/* Mirrors utils.c: core transition plus shell guard run under one mutex. */
static void guardedLock(GuardedPower *guard) {
  pthread_mutex_lock(&guard->mutex);
  int acquire = powerStateLock(&guard->state);
  if (acquire) {
    guard->shell_lock_calls++;
  }
  checkGuardInvariant(guard);
  pthread_mutex_unlock(&guard->mutex);
}

static void guardedUnlock(GuardedPower *guard) {
  pthread_mutex_lock(&guard->mutex);
  int release = powerStateUnlock(&guard->state);
  if (release) {
    guard->shell_unlock_calls++;
  }
  checkGuardInvariant(guard);
  pthread_mutex_unlock(&guard->mutex);
}

static int guardedShouldTick(GuardedPower *guard) {
  int tick;
  pthread_mutex_lock(&guard->mutex);
  tick = powerStateShouldTick(&guard->state);
  if (!!tick != (guard->state.count > 0))
    guard->invariant_violated = 1;
  pthread_mutex_unlock(&guard->mutex);
  return tick;
}

typedef struct {
  GuardedPower *guard;
  int iterations;
} WorkerArgs;

static void *lockUnlockWorker(void *arg) {
  WorkerArgs *args = (WorkerArgs *)arg;
  for (int i = 0; i < args->iterations; i++) {
    guardedLock(args->guard);
    /* Read the tick flag while another holder is active; exercises the
       power_tick_thread() shared-mutex path under overlap. */
    guardedShouldTick(args->guard);
    guardedUnlock(args->guard);
  }
  return NULL;
}

static int testConcurrentOverlappingStress(void) {
  GuardedPower guard;
  guardedInit(&guard);

  static const int thread_count = 8;
  static const int iterations = 2000;
  pthread_t threads[8];
  WorkerArgs args = { &guard, iterations };

  int created = 0;
  for (int i = 0; i < thread_count; i++) {
    if (pthread_create(&threads[i], NULL, lockUnlockWorker, &args) != 0) {
      for (int j = 0; j < created; j++)
        pthread_join(threads[j], NULL);
      guardedDestroy(&guard);
      return 1;
    }
    created++;
  }
  for (int i = 0; i < thread_count; i++)
    pthread_join(threads[i], NULL);

  CHECK(guard.invariant_violated == 0);
  CHECK(guard.state.count == 0);
  CHECK(guard.state.shell_locked == 0);
  CHECK(guardedShouldTick(&guard) == 0);
  /* Every 0->1 transition pairs with exactly one 1->0 transition: no lost
     updates and no early release while holders remained. */
  CHECK(guard.shell_lock_calls == guard.shell_unlock_calls);
  CHECK(guard.shell_lock_calls > 0);

  guardedDestroy(&guard);
  return 0;
}

int main(void) {
  int (*tests[])(void) = {
    testSingleLockUnlockTransitions,
    testUnmatchedUnlockIsNoop,
    testOverlappingHoldersKeepGuard,
    testConcurrentOverlappingStress,
  };

  for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
    if (tests[i]() != 0)
      return 1;
  }

  printf("power_core: %zu tests passed\n", ARRAY_SIZE(tests));
  return 0;
}
