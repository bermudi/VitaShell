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
  int promotion_calls;
  int report_calls;
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
  if (strcmp(path, "stage") != 0) {
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

static const RefreshOperationNames operation_names = {
  "stage", "promote", "restore", "work.bin"
};

static RefreshTransactionOps transactionOps(TransactionFake *fake) {
  RefreshTransactionOps ops = { fake, fakeRename, fakePromote, fakeReport };
  return ops;
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
  CHECK(fd == fake->open_result);
  CHECK(buffer == fake->expected_rif + fake->expected_offsets[fake->write_calls]);
  CHECK(size == fake->expected_sizes[fake->write_calls]);
  int result = fake->writes[fake->write_calls];
  fake->write_calls++;
  return result;
}

static int fakeClose(void *context, int fd) {
  WorkFake *fake = context;
  CHECK(fd == fake->open_result);
  fake->close_calls++;
  return fake->close_result;
}

static int fakeRemove(void *context, const char *path) {
  WorkFake *fake = context;
  CHECK(strcmp(path, "work.bin.tmp") == 0);
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
  return 0;
}

int main(void) {
  int (*tests[])(void) = {
    testStageFailureStopsPromotion,
    testPromotionSuccess,
    testPromotionFailureRestores,
    testRestoreFailureBlocksReuse,
    testPostCommitCleanupFailureDoesNotRestore,
    testPreStagedDlcDoesNotStageAgain,
    testErrorPriorityAndStickiness,
    testWorkBinOpenFailureDoesNothingElse,
    testWorkBinFullWriteIsCommitted,
    testWorkBinRetriesShortWrites,
    testWorkBinZeroWriteRemovesPartial,
    testWorkBinCloseFailureIsVisible,
    testWorkBinRenameFailureRemovesTemporaryFile,
    testWorkBinCleanupFailureIsSeparate,
  };

  for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
    if (tests[i]() != 0)
      return 1;
  }

  printf("refresh_core: %zu tests passed\n", ARRAY_SIZE(tests));
  return 0;
}
