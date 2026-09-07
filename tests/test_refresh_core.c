#include "refresh_core.h"

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
  int rename_results[4];
  int rename_result_count;
  int rename_calls;
  char rename_sources[4][64];
  char rename_destinations[4][64];
  RefreshPromotionResult promotion;
  const char *expected_promotion_path;
  int promotion_calls;
  int report_calls;
  int remove_results[4];
  int remove_result_count;
  int remove_calls;
  char remove_paths[4][64];
} TransactionFake;

static int fakeRename(void *context, const char *source, const char *destination) {
  TransactionFake *fake = context;
  snprintf(fake->rename_sources[fake->rename_calls],
           sizeof(fake->rename_sources[fake->rename_calls]), "%s", source);
  snprintf(fake->rename_destinations[fake->rename_calls],
           sizeof(fake->rename_destinations[fake->rename_calls]), "%s", destination);
  int result = fake->rename_calls < fake->rename_result_count
                   ? fake->rename_results[fake->rename_calls]
                   : 0;
  fake->rename_calls++;
  return result;
}

static RefreshPromotionResult fakePromote(void *context, const char *path) {
  TransactionFake *fake = context;
  const char *expected = fake->expected_promotion_path != NULL
                             ? fake->expected_promotion_path
                             : "stage";
  if (strcmp(path, expected) != 0) {
    RefreshPromotionResult unexpected_path = { -999, 0, 0 };
    return unexpected_path;
  }
  fake->promotion_calls++;
  return fake->promotion;
}

static void fakeReport(void *context, int error, const char *operation,
                       const char *path) {
  TransactionFake *fake = context;
  if (error < 0 && operation != NULL && path != NULL)
    fake->report_calls++;
}

static int fakeRemoveStaging(void *context, const char *path) {
  TransactionFake *fake = context;
  snprintf(fake->remove_paths[fake->remove_calls],
           sizeof(fake->remove_paths[fake->remove_calls]), "%s", path);
  int result = fake->remove_calls < fake->remove_result_count
                   ? fake->remove_results[fake->remove_calls]
                   : 0;
  fake->remove_calls++;
  return result;
}

static const RefreshOperationNames operation_names = {
  "stage", "promote", "restore", "work.bin", "cleanup"
};

static RefreshTransactionOps transactionOps(TransactionFake *fake) {
  RefreshTransactionOps ops = { fake, fakeRename, fakePromote, fakeReport, fakeRemoveStaging };
  return ops;
}

static int testPsmContentIdParsing(void) {
  uint8_t content_id[48] = "ABCD123PCSG00001_00-ABCDEFGHIJKLMNOP";
  char title_id[10] = { 0 };
  char copied_content_id[49] = { 0 };

  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), title_id,
                                 copied_content_id) == 0);
  CHECK(strcmp(title_id, "PCSG00001") == 0);
  CHECK(memcmp(copied_content_id, content_id, sizeof(content_id)) == 0);
  CHECK(copied_content_id[48] == '\0');
  return 0;
}

static int testPsmContentIdRejectsMalformedInput(void) {
  uint8_t content_id[48] = { 0 };
  char title_id[10] = { 0 };

  CHECK(refreshParsePsmContentId(content_id, 47, title_id, NULL) < 0);
  CHECK(refreshParsePsmContentId(NULL, sizeof(content_id), title_id, NULL) < 0);
  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), NULL, NULL) < 0);

  memcpy(content_id + 7, "PCSG00001", 9);
  content_id[7] = 'p';
  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), title_id,
                                 NULL) < 0);
  content_id[7] = '.';
  content_id[8] = '.';
  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), title_id,
                                 NULL) < 0);
  memcpy(content_id + 7, "PC/000001", 9);
  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), title_id,
                                 NULL) < 0);
  memcpy(content_id + 7, "PCSG00001", 9);
  content_id[11] = '\0';
  CHECK(refreshParsePsmContentId(content_id, sizeof(content_id), title_id,
                                 NULL) < 0);
  return 0;
}

static int testStageFailureStopsPromotion(void) {
  TransactionFake fake = { .rename_results = { -101 }, .rename_result_count = 1 };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_STAGE_FAILED);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.promotion_calls == 0);
  CHECK(results.first_error == -101);
  CHECK(results.refreshed == 0);
  return 0;
}

static int testPromotionSuccess(void) {
  TransactionFake fake = { .promotion = { 0, -102, 1 } };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_PROMOTED);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.promotion_calls == 1);
  CHECK(strcmp(fake.rename_sources[0], "source") == 0);
  CHECK(strcmp(fake.rename_destinations[0], "stage") == 0);
  CHECK(results.refreshed == 1);
  CHECK(results.first_work_bin_error == -102);
  CHECK(fake.remove_calls == 1);
  CHECK(strcmp(fake.remove_paths[0], "stage") == 0);
  return 0;
}

static int testStagingReusedAfterCleanup(void) {
  TransactionFake fake = { .promotion = { 0, 0, 0 } };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  /* Two entries staging through the same path (apps/patches reuse one temp
     dir): without cleanup between promotions the second staging rename
     collides with the leftover copy. */
  CHECK(refreshStageAndPromote(&results, "one", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_PROMOTED);
  CHECK(refreshStageAndPromote(&results, "two", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_PROMOTED);
  CHECK(fake.rename_calls == 2);
  CHECK(fake.promotion_calls == 2);
  CHECK(fake.remove_calls == 2);
  CHECK(strcmp(fake.remove_paths[0], "stage") == 0);
  CHECK(strcmp(fake.remove_paths[1], "stage") == 0);
  CHECK(results.refreshed == 2);
  return 0;
}

static int testCleanupFailureKeepsSuccessCounted(void) {
  TransactionFake fake = {
    .promotion = { 0, 0, 0 },
    .remove_results = { -107 },
    .remove_result_count = 1,
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_PROMOTED);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.remove_calls == 1);
  CHECK(results.refreshed == 1);
  CHECK(results.first_error == -107);
  CHECK(refreshReportedError(&results) == -107);
  return 0;
}

static int testPromotionFailureRestores(void) {
  TransactionFake fake = {
    .rename_results = { 0, 0 },
    .rename_result_count = 2,
    .promotion = { -103, 0, 0 },
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_RESTORED);
  CHECK(fake.rename_calls == 2);
  CHECK(strcmp(fake.rename_sources[1], "stage") == 0);
  CHECK(strcmp(fake.rename_destinations[1], "source") == 0);
  CHECK(results.first_promotion_error == -103);
  CHECK(results.restore_error == 0);
  /* The restore moved the data back; the staging path must not be deleted. */
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testRestoreFailureBlocksReuse(void) {
  TransactionFake fake = {
    .rename_results = { 0, -104 },
    .rename_result_count = 2,
    .promotion = { -103, 0, 0 },
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_RESTORE_FAILED);
  CHECK(results.restore_error == -104);
  CHECK(refreshStageAndPromote(&results, "other", "stage", &ops,
                               &operation_names) == REFRESH_TRANSACTION_BLOCKED);
  CHECK(fake.rename_calls == 2);
  CHECK(fake.promotion_calls == 1);
  CHECK(refreshReportedError(&results) == -104);
  /* Staging now holds the only copy of the user's data. Never delete it. */
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testPostCommitCleanupFailureDoesNotRestore(void) {
  TransactionFake fake = {
    .promotion = { -105, 0, 1 },
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshStageAndPromote(&results, "source", "stage", &ops,
                               &operation_names) ==
        REFRESH_TRANSACTION_COMMITTED_WITH_ERROR);
  CHECK(fake.rename_calls == 1);
  CHECK(results.refreshed == 1);
  CHECK(results.first_promotion_error == -105);
  CHECK(fake.remove_calls == 1);
  return 0;
}

static int testPreStagedDlcDoesNotStageAgain(void) {
  TransactionFake fake = {
    .rename_results = { 0 },
    .rename_result_count = 1,
    .promotion = { -106, 0, 0 },
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshPromoteStaged(&results, "original", "stage", &ops,
                             &operation_names) == REFRESH_TRANSACTION_RESTORED);
  CHECK(fake.rename_calls == 1);
  CHECK(strcmp(fake.rename_sources[0], "stage") == 0);
  CHECK(strcmp(fake.rename_destinations[0], "original") == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testPreStagedCmaPromotionsCleanExactTitlePath(void) {
  const char *staging_paths[] = {
    "ux0:pspemu/temp/game/PSP/GAME/ULUS12345",
    "ux0:temp/game/PCSG00001",
  };

  for (size_t i = 0; i < ARRAY_SIZE(staging_paths); i++) {
    TransactionFake fake = {
      .promotion = { 0, 0, 1 },
      .expected_promotion_path = staging_paths[i],
    };
    RefreshResults results = { 0 };
    RefreshTransactionOps ops = transactionOps(&fake);

    CHECK(refreshPromoteStaged(&results, "original", staging_paths[i], &ops,
                               &operation_names) ==
          REFRESH_TRANSACTION_PROMOTED);
    CHECK(fake.rename_calls == 0);
    CHECK(fake.promotion_calls == 1);
    CHECK(fake.remove_calls == 1);
    CHECK(strcmp(fake.remove_paths[0], staging_paths[i]) == 0);
    CHECK(results.refreshed == 1);
  }
  return 0;
}

static int testPreStagedCmaCommittedErrorCleansWithoutRestore(void) {
  const char *staging = "ux0:temp/game/PCSG00001";
  TransactionFake fake = {
    .promotion = { -108, 0, 1 },
    .expected_promotion_path = staging,
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshPromoteStaged(&results, "ux0:psm/PCSG00001", staging, &ops,
                             &operation_names) ==
        REFRESH_TRANSACTION_COMMITTED_WITH_ERROR);
  CHECK(fake.rename_calls == 0);
  CHECK(fake.remove_calls == 1);
  CHECK(strcmp(fake.remove_paths[0], staging) == 0);
  CHECK(results.refreshed == 1);
  CHECK(results.first_promotion_error == -108);
  return 0;
}

static int testPreStagedCmaFailureRestoresWithoutCleanup(void) {
  const char *staging = "ux0:pspemu/temp/game/PSP/GAME/ULUS12345";
  TransactionFake fake = {
    .promotion = { -109, 0, 0 },
    .expected_promotion_path = staging,
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = transactionOps(&fake);

  CHECK(refreshPromoteStaged(&results, "ux0:pspemu/PSP/GAME/ULUS12345",
                             staging, &ops, &operation_names) ==
        REFRESH_TRANSACTION_RESTORED);
  CHECK(fake.rename_calls == 1);
  CHECK(strcmp(fake.rename_sources[0], staging) == 0);
  CHECK(strcmp(fake.rename_destinations[0],
               "ux0:pspemu/PSP/GAME/ULUS12345") == 0);
  CHECK(fake.remove_calls == 0);
  CHECK(results.refreshed == 0);
  return 0;
}

static int testErrorPriorityAndStickiness(void) {
  RefreshResults results = {
    .first_error = -1,
    .first_promotion_error = -2,
    .first_work_bin_error = -3,
    .restore_error = -4,
  };
  CHECK(refreshReportedError(&results) == -4);
  results.restore_error = 0;
  CHECK(refreshReportedError(&results) == -2);
  results.first_promotion_error = 0;
  CHECK(refreshReportedError(&results) == -1);
  results.first_error = 0;
  CHECK(refreshReportedError(&results) == -3);
  results.first_work_bin_error = 0;
  CHECK(refreshReportedError(&results) == 0);
  return 0;
}

typedef struct {
  int open_result;
  int writes[4];
  int write_calls;
  const uint8_t *expected_rif;
  size_t expected_offsets[4];
  size_t expected_sizes[4];
  int close_result;
  int remove_result;
  int rename_result;
  int close_calls;
  int remove_calls;
  int rename_calls;
  int expectation_failed;
  char opened_path[64];
  char renamed_source[64];
  char renamed_destination[64];
} WorkFake;

static int fakeOpen(void *context, const char *path) {
  WorkFake *fake = context;
  snprintf(fake->opened_path, sizeof(fake->opened_path), "%s", path);
  return fake->open_result;
}

static int fakeWrite(void *context, int fd, const void *buffer, size_t size) {
  WorkFake *fake = context;
  if (fd != fake->open_result ||
      buffer != (const void *)(fake->expected_rif + fake->expected_offsets[fake->write_calls]) ||
      size != fake->expected_sizes[fake->write_calls])
    fake->expectation_failed = 1;
  int result = fake->writes[fake->write_calls];
  fake->write_calls++;
  return result;
}

static int fakeClose(void *context, int fd) {
  WorkFake *fake = context;
  if (fd != fake->open_result)
    fake->expectation_failed = 1;
  fake->close_calls++;
  return fake->close_result;
}

static int fakeRemove(void *context, const char *path) {
  WorkFake *fake = context;
  if (strcmp(path, "work.bin.tmp") != 0)
    fake->expectation_failed = 1;
  fake->remove_calls++;
  return fake->remove_result;
}

static int fakeWorkRename(void *context, const char *source, const char *destination) {
  WorkFake *fake = context;
  fake->rename_calls++;
  snprintf(fake->renamed_source, sizeof(fake->renamed_source), "%s", source);
  snprintf(fake->renamed_destination, sizeof(fake->renamed_destination), "%s", destination);
  return fake->rename_result;
}

static RefreshWorkBinOps workOps(WorkFake *fake) {
  RefreshWorkBinOps ops = {
    fake, fakeOpen, fakeWrite, fakeClose, fakeRemove, fakeWorkRename
  };
  return ops;
}

static int testWorkBinOpenFailureDoesNothingElse(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = { .open_result = -199, .expected_rif = rif };
  RefreshWorkBinOps ops = workOps(&fake);
  int cleanup_error = -1;

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, &cleanup_error) == -199);
  CHECK(fake.write_calls == 0);
  CHECK(fake.close_calls == 0);
  CHECK(fake.rename_calls == 0);
  CHECK(fake.remove_calls == 0);
  CHECK(cleanup_error == 0);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinFullWriteIsCommitted(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { 512 },
    .expected_rif = rif,
    .expected_sizes = { 512 },
  };
  RefreshWorkBinOps ops = workOps(&fake);
  int cleanup_error = 0;

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, &cleanup_error) == 0);
  CHECK(fake.close_calls == 1);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.remove_calls == 0);
  CHECK(strcmp(fake.opened_path, "work.bin.tmp") == 0);
  CHECK(strcmp(fake.renamed_source, "work.bin.tmp") == 0);
  CHECK(strcmp(fake.renamed_destination, "work.bin") == 0);
  CHECK(cleanup_error == 0);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinRetriesShortWrites(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { 200, 312 },
    .expected_rif = rif,
    .expected_offsets = { 0, 200 },
    .expected_sizes = { 512, 312 },
  };
  RefreshWorkBinOps ops = workOps(&fake);

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, NULL) == 0);
  CHECK(fake.write_calls == 2);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinZeroWriteRemovesPartial(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { 0 },
    .expected_rif = rif,
    .expected_sizes = { 512 },
  };
  RefreshWorkBinOps ops = workOps(&fake);

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, NULL) == -200);
  CHECK(fake.close_calls == 1);
  CHECK(fake.rename_calls == 0);
  CHECK(fake.remove_calls == 1);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinCloseFailureIsVisible(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { 512 },
    .expected_rif = rif,
    .expected_sizes = { 512 },
    .close_result = -201,
  };
  RefreshWorkBinOps ops = workOps(&fake);

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, NULL) == -201);
  CHECK(fake.rename_calls == 0);
  CHECK(fake.remove_calls == 1);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinRenameFailureRemovesTemporaryFile(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { 512 },
    .expected_rif = rif,
    .expected_sizes = { 512 },
    .rename_result = -204,
  };
  RefreshWorkBinOps ops = workOps(&fake);

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, NULL) == -204);
  CHECK(fake.rename_calls == 1);
  CHECK(fake.remove_calls == 1);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

static int testWorkBinCleanupFailureIsSeparate(void) {
  uint8_t rif[REFRESH_WORK_BIN_SIZE] = { 1 };
  WorkFake fake = {
    .open_result = 7,
    .writes = { -202 },
    .expected_rif = rif,
    .expected_sizes = { 512 },
    .remove_result = -203,
  };
  RefreshWorkBinOps ops = workOps(&fake);
  int cleanup_error = 0;

  CHECK(refreshWriteWorkBin("work.bin", "work.bin.tmp", rif, -200,
                            &ops, &cleanup_error) == -202);
  CHECK(cleanup_error == -203);
  CHECK(fake.expectation_failed == 0);
  return 0;
}

/* --- DLC restore-or-promote cascade tests --- */

typedef struct {
  int rename_results[8];
  int rename_result_count;
  int rename_calls;
  char rename_sources[8][128];
  char rename_destinations[8][128];
  RefreshPromotionResult promotion_results[8];
  int promotion_result_count;
  int promotion_calls;
  char promotion_paths[8][128];
  int report_calls;
  int remove_results[8];
  int remove_result_count;
  int remove_calls;
  char remove_paths[8][128];
} DlcFake;

static int dlcRename(void *context, const char *source, const char *destination) {
  DlcFake *fake = context;
  snprintf(fake->rename_sources[fake->rename_calls],
           sizeof(fake->rename_sources[fake->rename_calls]), "%s", source);
  snprintf(fake->rename_destinations[fake->rename_calls],
           sizeof(fake->rename_destinations[fake->rename_calls]), "%s", destination);
  int result = fake->rename_calls < fake->rename_result_count
                   ? fake->rename_results[fake->rename_calls]
                   : 0;
  fake->rename_calls++;
  return result;
}

static RefreshPromotionResult dlcPromote(void *context, const char *path) {
  DlcFake *fake = context;
  snprintf(fake->promotion_paths[fake->promotion_calls],
           sizeof(fake->promotion_paths[fake->promotion_calls]), "%s", path);
  RefreshPromotionResult result =
      fake->promotion_calls < fake->promotion_result_count
          ? fake->promotion_results[fake->promotion_calls]
          : (RefreshPromotionResult){ 0, 0, 0 };
  fake->promotion_calls++;
  return result;
}

static void dlcReport(void *context, int error, const char *operation,
                      const char *path) {
  (void)error; (void)operation; (void)path;
  ((DlcFake *)context)->report_calls++;
}

static int dlcRemove(void *context, const char *path) {
  DlcFake *fake = context;
  snprintf(fake->remove_paths[fake->remove_calls],
           sizeof(fake->remove_paths[fake->remove_calls]), "%s", path);
  int result = fake->remove_calls < fake->remove_result_count
                   ? fake->remove_results[fake->remove_calls]
                   : 0;
  fake->remove_calls++;
  return result;
}

static RefreshTransactionOps dlcOps(DlcFake *fake) {
  RefreshTransactionOps ops = { fake, dlcRename, dlcPromote, dlcReport, dlcRemove };
  return ops;
}

static int testDlcAllPromoted(void) {
  char *sources[] = { "ux0:addcont/TITLE/PCSG00001",
                      "ux0:addcont/TITLE/PCSG00002",
                      "ux0:addcont/TITLE/PCSG00003" };
  DlcFake fake = { .promotion_results = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                   .promotion_result_count = 3 };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = dlcOps(&fake);

  refreshRestoreOrPromoteDlc(&results, sources, 3, "ux0:temp/addcont", &ops,
                             &operation_names);
  CHECK(fake.promotion_calls == 3);
  CHECK(fake.rename_calls == 0);
  CHECK(results.refreshed == 3);
  CHECK(strcmp(fake.promotion_paths[0], "ux0:temp/addcont/PCSG00001") == 0);
  CHECK(strcmp(fake.promotion_paths[1], "ux0:temp/addcont/PCSG00002") == 0);
  CHECK(strcmp(fake.promotion_paths[2], "ux0:temp/addcont/PCSG00003") == 0);
  CHECK(fake.remove_calls == 3);
  CHECK(strcmp(fake.remove_paths[0], "ux0:temp/addcont/PCSG00001") == 0);
  CHECK(strcmp(fake.remove_paths[2], "ux0:temp/addcont/PCSG00003") == 0);
  return 0;
}

static int testDlcPreExistingRestoreErrorTriggersRecovery(void) {
  char *sources[] = { "ux0:addcont/TITLE/PCSG00001",
                      "ux0:addcont/TITLE/PCSG00002" };
  DlcFake fake = { 0 };
  RefreshResults results = { .restore_error = -500 };
  RefreshTransactionOps ops = dlcOps(&fake);

  refreshRestoreOrPromoteDlc(&results, sources, 2, "ux0:temp/addcont", &ops,
                             &operation_names);
  CHECK(fake.promotion_calls == 0);
  CHECK(fake.rename_calls == 2);
  CHECK(strcmp(fake.rename_sources[0], "ux0:temp/addcont/PCSG00001") == 0);
  CHECK(strcmp(fake.rename_destinations[0], "ux0:addcont/TITLE/PCSG00001") == 0);
  CHECK(strcmp(fake.rename_sources[1], "ux0:temp/addcont/PCSG00002") == 0);
  CHECK(strcmp(fake.rename_destinations[1], "ux0:addcont/TITLE/PCSG00002") == 0);
  CHECK(results.refreshed == 0);
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testDlcCascadeFromPromotionRestoreFailure(void) {
  char *sources[] = { "ux0:addcont/TITLE/PCSG00001",
                      "ux0:addcont/TITLE/PCSG00002" };
  DlcFake fake = {
    .rename_results = { -104 },
    .rename_result_count = 1,
    .promotion_results = { { -103, 0, 0 } },
    .promotion_result_count = 1,
  };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = dlcOps(&fake);

  refreshRestoreOrPromoteDlc(&results, sources, 2, "ux0:temp/addcont", &ops,
                             &operation_names);
  /* Entry 0: promoted, promotion failed (-103), restore attempted, restore
     failed (-104) → restore_error set. Entry 1: restore_error < 0 so recovery
     rename instead of promote. */
  CHECK(fake.promotion_calls == 1);
  CHECK(fake.rename_calls == 2);
  CHECK(results.restore_error == -104);
  CHECK(results.refreshed == 0);
  /* Second rename is the recovery restore for entry 1. */
  CHECK(strcmp(fake.rename_sources[1], "ux0:temp/addcont/PCSG00002") == 0);
  CHECK(strcmp(fake.rename_destinations[1], "ux0:addcont/TITLE/PCSG00002") == 0);
  /* Nothing was committed, so nothing may be deleted. */
  CHECK(fake.remove_calls == 0);
  return 0;
}

static int testDlcNullEntriesSkipped(void) {
  char *sources[] = { NULL, "ux0:addcont/TITLE/PCSG00002", NULL };
  DlcFake fake = { .promotion_results = { { 0, 0, 0 } },
                   .promotion_result_count = 1 };
  RefreshResults results = { 0 };
  RefreshTransactionOps ops = dlcOps(&fake);

  refreshRestoreOrPromoteDlc(&results, sources, 3, "ux0:temp/addcont", &ops,
                             &operation_names);
  CHECK(fake.promotion_calls == 1);
  CHECK(fake.rename_calls == 0);
  CHECK(results.refreshed == 1);
  CHECK(strcmp(fake.promotion_paths[0], "ux0:temp/addcont/PCSG00002") == 0);
  CHECK(fake.remove_calls == 1);
  CHECK(strcmp(fake.remove_paths[0], "ux0:temp/addcont/PCSG00002") == 0);
  return 0;
}

static int testDlcRecoveryRenameFailureRecordsError(void) {
  char *sources[] = { "ux0:addcont/TITLE/PCSG00001",
                      "ux0:addcont/TITLE/PCSG00002" };
  DlcFake fake = {
    .rename_results = { -601, -602 },
    .rename_result_count = 2,
  };
  RefreshResults results = { .restore_error = -500 };
  RefreshTransactionOps ops = dlcOps(&fake);

  refreshRestoreOrPromoteDlc(&results, sources, 2, "ux0:temp/addcont", &ops,
                             &operation_names);
  CHECK(fake.rename_calls == 2);
  CHECK(fake.promotion_calls == 0);
  CHECK(results.first_error == -601);
  CHECK(results.restore_error == -500);
  CHECK(fake.report_calls == 2);
  CHECK(fake.remove_calls == 0);
  return 0;
}

typedef struct {
  const int *results;
  int call_count;
  char scanned_roots[4][64];
  char skipped_roots[4][64];
  int skipped_errors[4];
  int skip_count;
  char failed_roots[4][64];
  int failed_errors[4];
  int failure_count;
} LicenseScanFake;

static int fakeLicenseScanCategory(void *context, const char *root) {
  LicenseScanFake *fake = context;
  snprintf(fake->scanned_roots[fake->call_count],
           sizeof(fake->scanned_roots[0]), "%s", root);
  return fake->results[fake->call_count++];
}

static void fakeLicenseReportSkip(void *context, const char *root, int error) {
  LicenseScanFake *fake = context;
  snprintf(fake->skipped_roots[fake->skip_count],
           sizeof(fake->skipped_roots[0]), "%s", root);
  fake->skipped_errors[fake->skip_count] = error;
  fake->skip_count++;
}

static void fakeLicenseReportError(void *context, const char *root, int error) {
  LicenseScanFake *fake = context;
  snprintf(fake->failed_roots[fake->failure_count],
           sizeof(fake->failed_roots[0]), "%s", root);
  fake->failed_errors[fake->failure_count] = error;
  fake->failure_count++;
}

static LicenseScanOps licenseScanOps(LicenseScanFake *fake) {
  LicenseScanOps ops = {
    fake,
    fakeLicenseScanCategory,
    fakeLicenseReportSkip,
    fakeLicenseReportError,
  };
  return ops;
}

static const char *const license_roots[] = {
  "ux0:license/app",
  "ux0:license/addcont",
};

static int testLicenseScanSuccessScansEveryCategory(void) {
  const int results[] = { 0, 0 };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = -1;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_COMPLETED);
  CHECK(fake.call_count == 2);
  CHECK(fake.skip_count == 0);
  CHECK(fake.failure_count == 0);
  CHECK(scan_error == 0);
  return 0;
}

static int testLicenseScanMissingAppCategoryIsSkipped(void) {
  const int results[] = { LICENSE_SCAN_NOT_FOUND, 0 };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = -1;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_COMPLETED);
  CHECK(fake.call_count == 2);
  CHECK(strcmp(fake.scanned_roots[1], "ux0:license/addcont") == 0);
  CHECK(fake.skip_count == 1);
  CHECK(strcmp(fake.skipped_roots[0], "ux0:license/app") == 0);
  CHECK(fake.skipped_errors[0] == LICENSE_SCAN_NOT_FOUND);
  CHECK(fake.failure_count == 0);
  CHECK(scan_error == 0);
  return 0;
}

static int testLicenseScanMissingAddcontCategoryIsSkipped(void) {
  const int results[] = { 0, LICENSE_SCAN_NOT_FOUND };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = -1;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_COMPLETED);
  CHECK(fake.call_count == 2);
  CHECK(fake.skip_count == 1);
  CHECK(strcmp(fake.skipped_roots[0], "ux0:license/addcont") == 0);
  CHECK(fake.failure_count == 0);
  CHECK(scan_error == 0);
  return 0;
}

static int testLicenseScanBothCategoriesMissingAreSkipped(void) {
  const int results[] = { LICENSE_SCAN_NOT_FOUND, LICENSE_SCAN_NOT_FOUND };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = -1;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_COMPLETED);
  CHECK(fake.call_count == 2);
  CHECK(fake.skip_count == 2);
  CHECK(fake.failure_count == 0);
  CHECK(scan_error == 0);
  return 0;
}

static int testLicenseScanGenuineFailureStopsAndReports(void) {
  const int permission_error = 0x8001000D; /* EACCES */
  const int results[] = { 0, permission_error };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = 0;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_FAILED);
  CHECK(fake.call_count == 2);
  CHECK(fake.skip_count == 0);
  CHECK(fake.failure_count == 1);
  CHECK(strcmp(fake.failed_roots[0], "ux0:license/addcont") == 0);
  CHECK(fake.failed_errors[0] == permission_error);
  CHECK(scan_error == permission_error);
  return 0;
}

static int testLicenseScanFailureStopsBeforeLaterCategories(void) {
  const int permission_error = 0x8001000D; /* EACCES */
  const int results[] = { permission_error, 0 };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = 0;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_FAILED);
  CHECK(fake.call_count == 1);
  CHECK(fake.failure_count == 1);
  CHECK(strcmp(fake.failed_roots[0], "ux0:license/app") == 0);
  CHECK(scan_error == permission_error);
  return 0;
}

static int testLicenseScanCancellationIsNotAnError(void) {
  const int results[] = { 1, 0 };
  LicenseScanFake fake = { .results = results };
  LicenseScanOps ops = licenseScanOps(&fake);
  int scan_error = -1;

  CHECK(licenseScanCategories(license_roots, ARRAY_SIZE(license_roots), &ops,
                              &scan_error) == LICENSE_SCAN_CANCELED);
  CHECK(fake.call_count == 1);
  CHECK(fake.skip_count == 0);
  CHECK(fake.failure_count == 0);
  CHECK(scan_error == 0);
  return 0;
}

int main(void) {
  int (*tests[])(void) = {
    testPsmContentIdParsing,
    testPsmContentIdRejectsMalformedInput,
    testStageFailureStopsPromotion,
    testPromotionSuccess,
    testStagingReusedAfterCleanup,
    testCleanupFailureKeepsSuccessCounted,
    testPromotionFailureRestores,
    testRestoreFailureBlocksReuse,
    testPostCommitCleanupFailureDoesNotRestore,
    testPreStagedDlcDoesNotStageAgain,
    testPreStagedCmaPromotionsCleanExactTitlePath,
    testPreStagedCmaCommittedErrorCleansWithoutRestore,
    testPreStagedCmaFailureRestoresWithoutCleanup,
    testErrorPriorityAndStickiness,
    testWorkBinOpenFailureDoesNothingElse,
    testWorkBinFullWriteIsCommitted,
    testWorkBinRetriesShortWrites,
    testWorkBinZeroWriteRemovesPartial,
    testWorkBinCloseFailureIsVisible,
    testWorkBinRenameFailureRemovesTemporaryFile,
    testWorkBinCleanupFailureIsSeparate,
    testDlcAllPromoted,
    testDlcPreExistingRestoreErrorTriggersRecovery,
    testDlcCascadeFromPromotionRestoreFailure,
    testDlcNullEntriesSkipped,
    testDlcRecoveryRenameFailureRecordsError,
    testLicenseScanSuccessScansEveryCategory,
    testLicenseScanMissingAppCategoryIsSkipped,
    testLicenseScanMissingAddcontCategoryIsSkipped,
    testLicenseScanBothCategoriesMissingAreSkipped,
    testLicenseScanGenuineFailureStopsAndReports,
    testLicenseScanFailureStopsBeforeLaterCategories,
    testLicenseScanCancellationIsNotAnError,
  };

  for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
    if (tests[i]() != 0)
      return 1;
  }

  printf("refresh_core: %zu tests passed\n", ARRAY_SIZE(tests));
  return 0;
}
