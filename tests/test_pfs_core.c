#include "pfs_core.h"

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
  int id_results[PFS_KNOWN_ID_COUNT];
  int id_calls;
  int fallback_result;
  int fallback_calls;
  PfsAttempt attempts[PFS_KNOWN_ID_COUNT + 1];
  int attempt_calls;
} MountFake;

static int fakeMountById(void *context, const char *path, int id) {
  MountFake *fake = context;
  CHECK(strcmp(path, "ux0:app/TITLE0001") == 0);
  CHECK(id == pfs_known_ids[fake->id_calls]);
  return fake->id_results[fake->id_calls++];
}

static int fakeMountGameData(void *context, const char *path) {
  MountFake *fake = context;
  CHECK(strcmp(path, "ux0:app/TITLE0001") == 0);
  fake->fallback_calls++;
  return fake->fallback_result;
}

static void fakeAttempt(void *context, const PfsAttempt *attempt) {
  MountFake *fake = context;
  fake->attempts[fake->attempt_calls++] = *attempt;
}

static PfsMountOps mountOps(MountFake *fake) {
  PfsMountOps ops = { fake, fakeMountById, fakeMountGameData, fakeAttempt };
  return ops;
}

static int testFirstIdSuccessStops(void) {
  MountFake fake = { .id_results = { 7 } };
  PfsMountOps ops = mountOps(&fake);
  int read_only = -1;

  CHECK(pfsMountSequence("ux0:app/TITLE0001", &ops, &read_only) == 7);
  CHECK(fake.id_calls == 1);
  CHECK(fake.fallback_calls == 0);
  CHECK(fake.attempt_calls == 1);
  CHECK(fake.attempts[0].kind == PFS_ATTEMPT_BY_ID);
  CHECK(fake.attempts[0].id == 0x6E);
  CHECK(read_only == 0);
  return 0;
}

static int testLaterIdSuccessPreservesOrder(void) {
  MountFake fake = { .id_results = { -1, -2, 8 } };
  PfsMountOps ops = mountOps(&fake);
  int read_only = -1;

  CHECK(pfsMountSequence("ux0:app/TITLE0001", &ops, &read_only) == 8);
  CHECK(fake.id_calls == 3);
  CHECK(fake.attempt_calls == 3);
  CHECK(fake.attempts[1].id == 0x12E);
  CHECK(fake.attempts[2].id == 0x12F);
  CHECK(read_only == 0);
  return 0;
}

static int testFallbackSuccessIsReadOnly(void) {
  MountFake fake = {
    .id_results = { -1, -2, -3, -4 },
    .fallback_result = 9,
  };
  PfsMountOps ops = mountOps(&fake);
  int read_only = -1;

  CHECK(pfsMountSequence("ux0:app/TITLE0001", &ops, &read_only) == 9);
  CHECK(fake.id_calls == PFS_KNOWN_ID_COUNT);
  CHECK(fake.fallback_calls == 1);
  CHECK(fake.attempt_calls == PFS_KNOWN_ID_COUNT + 1);
  const int expected_ids[] = { 0x6E, 0x12E, 0x12F, 0x3ED };
  for (size_t i = 0; i < ARRAY_SIZE(expected_ids); i++) {
    CHECK(fake.attempts[i].kind == PFS_ATTEMPT_BY_ID);
    CHECK(fake.attempts[i].id == expected_ids[i]);
    CHECK(fake.attempts[i].result == fake.id_results[i]);
    CHECK(strcmp(fake.attempts[i].path, "ux0:app/TITLE0001") == 0);
  }
  CHECK(fake.attempts[4].kind == PFS_ATTEMPT_GAME_DATA);
  CHECK(read_only == 1);
  return 0;
}

static int testFallbackErrorIsReturned(void) {
  MountFake fake = {
    .id_results = { -1, -2, -3, -4 },
    .fallback_result = -5,
  };
  PfsMountOps ops = mountOps(&fake);
  int read_only = -1;

  CHECK(pfsMountSequence("ux0:app/TITLE0001", &ops, &read_only) == -5);
  CHECK(fake.attempts[4].result == -5);
  CHECK(read_only == -1);
  return 0;
}

static int testDiagnosticFormattingKeepsResultBeforeLongPath(void) {
  char long_path[700];
  memset(long_path, 'x', sizeof(long_path) - 1);
  long_path[sizeof(long_path) - 1] = '\0';
  PfsAttempt attempt = { PFS_ATTEMPT_BY_ID, long_path, 0x6E, (int)0x80010002u };
  char message[96];

  CHECK(pfsFormatAttempt(message, sizeof(message), &attempt) > (int)sizeof(message));
  CHECK(strstr(message, "id=0x6E") != NULL);
  CHECK(strstr(message, "0x80010002") != NULL);
  return 0;
}

typedef struct {
  const int *results;
  int result_count;
  int calls;
  char paths[4][64];
} PathFake;

static int fakeMountPath(void *context, const char *path) {
  PathFake *fake = context;
  snprintf(fake->paths[fake->calls], sizeof(fake->paths[fake->calls]), "%s", path);
  int result = fake->results[fake->calls];
  fake->calls++;
  return result;
}

static int testFallbackDiagnosticFormatting(void) {
  PfsAttempt attempt = {
    PFS_ATTEMPT_GAME_DATA,
    "ux0:patch/TITLE0001",
    0,
    (int)0x80010002u,
  };
  char message[160];

  CHECK(pfsFormatAttempt(message, sizeof(message), &attempt) > 0);
  CHECK(strcmp(message,
               "PFS mount: sceAppMgrGameDataMount returned 0x80010002; "
               "path=ux0:patch/TITLE0001\n") == 0);
  return 0;
}

static int testNonPatchUsesDirectPathOnly(void) {
  const int results[] = { -10 };
  PathFake fake = { results, 1, 0, {{ 0 }} };
  char source[64];

  CHECK(pfsMountBrowserEntry("ux0:app/", "TITLE0001", fakeMountPath, &fake,
                             source, sizeof(source)) == -10);
  CHECK(fake.calls == 1);
  CHECK(strcmp(fake.paths[0], "ux0:app/TITLE0001") == 0);
  return 0;
}

static int testPatchFallbackStopsOnUxAppSuccess(void) {
  const int results[] = { -10, 11 };
  PathFake fake = { results, 2, 0, {{ 0 }} };
  char source[64];

  CHECK(pfsMountBrowserEntry("ux0:patch/", "TITLE0001", fakeMountPath, &fake,
                             source, sizeof(source)) == 11);
  CHECK(fake.calls == 2);
  CHECK(strcmp(fake.paths[0], "ux0:patch/TITLE0001") == 0);
  CHECK(strcmp(fake.paths[1], "ux0:app/TITLE0001") == 0);
  CHECK(strcmp(source, "ux0:app/TITLE0001") == 0);
  return 0;
}

static int testPatchFallsBackToGroApp(void) {
  const int results[] = { -10, -11, 12 };
  PathFake fake = { results, 3, 0, {{ 0 }} };
  char source[64];

  CHECK(pfsMountBrowserEntry("GRW0:PATCH", "TITLE0001", fakeMountPath, &fake,
                             source, sizeof(source)) == 12);
  CHECK(fake.calls == 3);
  CHECK(strcmp(fake.paths[0], "GRW0:PATCH/TITLE0001") == 0);
  CHECK(strcmp(fake.paths[2], "gro0:app/TITLE0001") == 0);
  return 0;
}

static int testScePfsPathHasExactlyOneSeparator(void) {
  char path[64];
  CHECK(pfsBuildScePfsPath(path, sizeof(path), "ux0:app/", "TITLE0001") == 0);
  CHECK(strcmp(path, "ux0:app/TITLE0001/sce_pfs") == 0);
  CHECK(pfsBuildScePfsPath(path, sizeof(path), "ux0:app", "TITLE0001") == 0);
  CHECK(strcmp(path, "ux0:app/TITLE0001/sce_pfs") == 0);
  return 0;
}

static int testPatchPrefixDoesNotMatch(void) {
  const int results[] = { -10 };
  PathFake fake = { results, 1, 0, {{ 0 }} };
  char source[64];

  CHECK(pfsMountBrowserEntry("ux0:patches/", "TITLE0001", fakeMountPath, &fake,
                             source, sizeof(source)) == -10);
  CHECK(fake.calls == 1);
  return 0;
}

static int testPathOverflowDoesNotCallMount(void) {
  const int results[] = { 1 };
  PathFake fake = { results, 1, 0, {{ 0 }} };
  char source[8];

  CHECK(pfsMountBrowserEntry("ux0:app", "TITLE0001", fakeMountPath, &fake,
                             source, sizeof(source)) == -1);
  CHECK(fake.calls == 0);
  return 0;
}

int main(void) {
  int (*tests[])(void) = {
    testFirstIdSuccessStops,
    testLaterIdSuccessPreservesOrder,
    testFallbackSuccessIsReadOnly,
    testFallbackErrorIsReturned,
    testDiagnosticFormattingKeepsResultBeforeLongPath,
    testFallbackDiagnosticFormatting,
    testNonPatchUsesDirectPathOnly,
    testPatchFallbackStopsOnUxAppSuccess,
    testPatchFallsBackToGroApp,
    testScePfsPathHasExactlyOneSeparator,
    testPatchPrefixDoesNotMatch,
    testPathOverflowDoesNotCallMount,
  };

  for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
    if (tests[i]() != 0)
      return 1;
  }

  printf("pfs_core: %zu tests passed\n", ARRAY_SIZE(tests));
  return 0;
}
