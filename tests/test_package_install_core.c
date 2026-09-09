#include "package_install_core.h"

#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(values) (sizeof(values) / sizeof((values)[0]))
#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
    return 1; \
  } \
} while (0)

typedef struct {
  int rename_result;
  int rename_calls;
  int remove_result;
  int remove_calls;
  char source[64];
  char destination[64];
  char removed[64];
} StagingFake;

static int fakeRename(void *context, const char *source, const char *destination) {
  StagingFake *fake = context;
  fake->rename_calls++;
  snprintf(fake->source, sizeof(fake->source), "%s", source);
  snprintf(fake->destination, sizeof(fake->destination), "%s", destination);
  return fake->rename_result;
}

static int fakeRemove(void *context, const char *path) {
  StagingFake *fake = context;
  fake->remove_calls++;
  snprintf(fake->removed, sizeof(fake->removed), "%s", path);
  return fake->remove_result;
}

static int testUnownedStagingIsNotRemoved(void) {
  StagingFake fake = { 0 };

  CHECK(packageCleanupStaging(PACKAGE_STAGING_UNOWNED, "stage", &fake,
                              fakeRemove) == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testMovedSourceIsNotRemoved(void) {
  StagingFake fake = { 0 };

  CHECK(packageCleanupStaging(PACKAGE_STAGING_MOVED_SOURCE, "stage", &fake,
                              fakeRemove) == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testDisposableStagingIsRemoved(void) {
  StagingFake fake = { 0 };

  CHECK(packageCleanupStaging(PACKAGE_STAGING_DISPOSABLE, "stage", &fake,
                              fakeRemove) == 0);
  CHECK(fake.remove_calls == 1);
  CHECK(strcmp(fake.removed, "stage") == 0);
  return 0;
}

static int testDisposableCleanupFailureIsReturned(void) {
  StagingFake fake = { .remove_result = -101 };

  CHECK(packageCleanupStaging(PACKAGE_STAGING_DISPOSABLE, "stage", &fake,
                              fakeRemove) == -101);
  CHECK(fake.remove_calls == 1);
  return 0;
}

static int testSuccessfulRestoreReleasesStaging(void) {
  StagingFake fake = { 0 };
  PackageStagingOwnership ownership = PACKAGE_STAGING_MOVED_SOURCE;

  CHECK(packageRestoreMovedSource(&ownership, "source", "stage", &fake,
                                  fakeRename) == 0);
  CHECK(ownership == PACKAGE_STAGING_UNOWNED);
  CHECK(fake.rename_calls == 1);
  CHECK(strcmp(fake.source, "stage") == 0);
  CHECK(strcmp(fake.destination, "source") == 0);
  CHECK(packageCleanupStaging(ownership, "stage", &fake, fakeRemove) == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testFailedRestorePreservesMovedSource(void) {
  StagingFake fake = { .rename_result = -102 };
  PackageStagingOwnership ownership = PACKAGE_STAGING_MOVED_SOURCE;

  CHECK(packageRestoreMovedSource(&ownership, "source", "stage", &fake,
                                  fakeRename) == -102);
  CHECK(ownership == PACKAGE_STAGING_MOVED_SOURCE);
  CHECK(fake.rename_calls == 1);
  CHECK(packageCleanupStaging(ownership, "stage", &fake, fakeRemove) == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testRestoreDoesNothingWithoutMovedSource(void) {
  StagingFake fake = { 0 };
  PackageStagingOwnership ownership = PACKAGE_STAGING_DISPOSABLE;

  CHECK(packageRestoreMovedSource(&ownership, "source", "stage", &fake,
                                  fakeRename) == 0);
  CHECK(ownership == PACKAGE_STAGING_DISPOSABLE);
  CHECK(fake.rename_calls == 0);
  return 0;
}

int main(void) {
  int (*tests[])(void) = {
    testUnownedStagingIsNotRemoved,
    testMovedSourceIsNotRemoved,
    testDisposableStagingIsRemoved,
    testDisposableCleanupFailureIsReturned,
    testSuccessfulRestoreReleasesStaging,
    testFailedRestorePreservesMovedSource,
    testRestoreDoesNothingWithoutMovedSource,
  };

  for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
    if (tests[i]() != 0)
      return 1;
  }

  printf("package_install_core: %zu tests passed\n", ARRAY_SIZE(tests));
  return 0;
}
