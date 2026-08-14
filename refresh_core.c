/*
  Portable refresh transaction helpers shared by VitaShell and host tests.
*/

#include "refresh_core.h"

#include <stdio.h>
#include <string.h>

static void reportError(const RefreshTransactionOps *ops, int error,
                        const char *operation, const char *path) {
  if (error < 0 && ops->report_error != NULL)
    ops->report_error(ops->context, error, operation, path);
}

static void recordGeneralError(RefreshResults *results, int error) {
  if (error < 0 && results->first_error == 0)
    results->first_error = error;
}

static void recordPromotion(RefreshResults *results,
                            const RefreshPromotionResult *promotion) {
  if (promotion->work_bin_error < 0 && results->first_work_bin_error == 0)
    results->first_work_bin_error = promotion->work_bin_error;

  if (promotion->error < 0 && results->first_promotion_error == 0)
    results->first_promotion_error = promotion->error;
}

int refreshReportedError(const RefreshResults *results) {
  if (results->restore_error < 0)
    return results->restore_error;
  if (results->first_promotion_error < 0)
    return results->first_promotion_error;
  if (results->first_error < 0)
    return results->first_error;
  if (results->first_work_bin_error < 0)
    return results->first_work_bin_error;
  return 0;
}

int refreshRestoreStaged(RefreshResults *results, const char *source,
                         const char *staging, const RefreshTransactionOps *ops,
                         const RefreshOperationNames *names) {
  int restore_error = ops->rename_path(ops->context, staging, source);
  if (restore_error < 0) {
    reportError(ops, restore_error, names->restore, staging);
    recordGeneralError(results, restore_error);
    if (results->restore_error == 0)
      results->restore_error = restore_error;
  }
  return restore_error;
}

RefreshTransactionResult refreshPromoteStaged(
    RefreshResults *results, const char *source, const char *staging,
    const RefreshTransactionOps *ops, const RefreshOperationNames *names) {
  if (results->restore_error < 0)
    return REFRESH_TRANSACTION_BLOCKED;

  RefreshPromotionResult promotion = ops->promote(ops->context, staging);
  recordPromotion(results, &promotion);

  if (promotion.work_bin_error < 0)
    reportError(ops, promotion.work_bin_error, names->work_bin, staging);

  if (promotion.error >= 0) {
    results->refreshed++;
    return REFRESH_TRANSACTION_PROMOTED;
  }

  reportError(ops, promotion.error, names->promotion, staging);
  recordGeneralError(results, promotion.error);

  /* The package manager has committed the install. Restoring is now unsafe. */
  if (promotion.committed) {
    results->refreshed++;
    return REFRESH_TRANSACTION_COMMITTED_WITH_ERROR;
  }

  if (refreshRestoreStaged(results, source, staging, ops, names) < 0)
    return REFRESH_TRANSACTION_RESTORE_FAILED;

  return REFRESH_TRANSACTION_RESTORED;
}

RefreshTransactionResult refreshStageAndPromote(
    RefreshResults *results, const char *source, const char *staging,
    const RefreshTransactionOps *ops, const RefreshOperationNames *names) {
  if (results->restore_error < 0)
    return REFRESH_TRANSACTION_BLOCKED;

  int stage_error = ops->rename_path(ops->context, source, staging);
  if (stage_error < 0) {
    reportError(ops, stage_error, names->staging, source);
    recordGeneralError(results, stage_error);
    return REFRESH_TRANSACTION_STAGE_FAILED;
  }

  return refreshPromoteStaged(results, source, staging, ops, names);
}

int refreshWriteWorkBin(
    const char *path, const char *temporary_path,
    const uint8_t rif[REFRESH_WORK_BIN_SIZE], int short_write_error,
    const RefreshWorkBinOps *ops, int *cleanup_error) {
  int error = 0;
  int fd;
  size_t written = 0;

  if (cleanup_error != NULL)
    *cleanup_error = 0;

  fd = ops->open_file(ops->context, temporary_path);
  if (fd < 0)
    return fd;

  while (written < REFRESH_WORK_BIN_SIZE) {
    int chunk = ops->write_file(ops->context, fd, rif + written,
                                REFRESH_WORK_BIN_SIZE - written);
    if (chunk < 0) {
      error = chunk;
      break;
    }
    if (chunk == 0) {
      error = short_write_error;
      break;
    }
    if ((size_t)chunk > REFRESH_WORK_BIN_SIZE - written) {
      error = short_write_error;
      break;
    }
    written += (size_t)chunk;
  }

  int close_error = ops->close_file(ops->context, fd);
  if (error == 0 && close_error < 0)
    error = close_error;

  if (error == 0) {
    int rename_error = ops->rename_path(ops->context, temporary_path, path);
    if (rename_error < 0)
      error = rename_error;
  }

  if (error < 0) {
    int remove_error = ops->remove_file(ops->context, temporary_path);
    if (remove_error < 0 && cleanup_error != NULL)
      *cleanup_error = remove_error;
  }

  return error;
}

int refreshRestoreOrPromoteDlc(
    RefreshResults *results,
    char **sources,
    int count,
    const char *staging_prefix,
    const RefreshTransactionOps *ops,
    const RefreshOperationNames *names) {
  for (int i = 0; i < count; i++) {
    if (sources[i] == NULL)
      continue;

    const char *base = strrchr(sources[i], '/');
    base = base ? base + 1 : sources[i];

    char staging[REFRESH_PATH_MAX];
    int written = snprintf(staging, sizeof(staging), "%s/%s", staging_prefix, base);
    if (written < 0 || (size_t)written >= sizeof(staging)) {
      reportError(ops, -1, names->staging, sources[i]);
      recordGeneralError(results, -1);
      continue;
    }

    if (results->restore_error < 0) {
      int restore_res = ops->rename_path(ops->context, staging, sources[i]);
      if (restore_res < 0) {
        reportError(ops, restore_res, names->restore, staging);
        recordGeneralError(results, restore_res);
      }
    } else {
      refreshPromoteStaged(results, sources[i], staging, ops, names);
    }
  }

  return 0;
}
