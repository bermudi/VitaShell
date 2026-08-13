/*
  Portable refresh transaction helpers shared by VitaShell and host tests.
*/

#ifndef __REFRESH_CORE_H__
#define __REFRESH_CORE_H__

#include <stddef.h>
#include <stdint.h>

#define REFRESH_WORK_BIN_SIZE 512

typedef struct {
  int refreshed;
  int first_error;
  int first_promotion_error;
  int first_work_bin_error;
  int restore_error;
} RefreshResults;

typedef struct {
  int error;
  int work_bin_error;
  int committed;
} RefreshPromotionResult;

typedef enum {
  REFRESH_TRANSACTION_BLOCKED,
  REFRESH_TRANSACTION_STAGE_FAILED,
  REFRESH_TRANSACTION_PROMOTED,
  REFRESH_TRANSACTION_COMMITTED_WITH_ERROR,
  REFRESH_TRANSACTION_RESTORED,
  REFRESH_TRANSACTION_RESTORE_FAILED
} RefreshTransactionResult;

typedef int (*RefreshRenameFn)(void *context, const char *source, const char *destination);
typedef RefreshPromotionResult (*RefreshPromoteFn)(void *context, const char *path);
typedef void (*RefreshErrorFn)(void *context, int error, const char *operation,
                               const char *path);

typedef struct {
  void *context;
  RefreshRenameFn rename_path;
  RefreshPromoteFn promote;
  RefreshErrorFn report_error;
} RefreshTransactionOps;

typedef struct {
  const char *staging;
  const char *promotion;
  const char *restore;
  const char *work_bin;
} RefreshOperationNames;

int refreshReportedError(const RefreshResults *results);

RefreshTransactionResult refreshStageAndPromote(
    RefreshResults *results,
    const char *source,
    const char *staging,
    const RefreshTransactionOps *ops,
    const RefreshOperationNames *names);

RefreshTransactionResult refreshPromoteStaged(
    RefreshResults *results,
    const char *source,
    const char *staging,
    const RefreshTransactionOps *ops,
    const RefreshOperationNames *names);

int refreshRestoreStaged(
    RefreshResults *results,
    const char *source,
    const char *staging,
    const RefreshTransactionOps *ops,
    const RefreshOperationNames *names);

typedef int (*RefreshOpenFn)(void *context, const char *path);
typedef int (*RefreshWriteFn)(void *context, int fd, const void *buffer, size_t size);
typedef int (*RefreshCloseFn)(void *context, int fd);
typedef int (*RefreshRemoveFn)(void *context, const char *path);

typedef struct {
  void *context;
  RefreshOpenFn open_file;
  RefreshWriteFn write_file;
  RefreshCloseFn close_file;
  RefreshRemoveFn remove_file;
  RefreshRenameFn rename_path;
} RefreshWorkBinOps;

/*
  Writes a RIF to a temporary sibling and renames it into place only after all
  bytes have been written and the descriptor has closed successfully.

  Returns the primary write/close/rename error. A failure to remove the
  temporary partial file is returned separately through cleanup_error.
*/
int refreshWriteWorkBin(
    const char *path,
    const char *temporary_path,
    const uint8_t rif[REFRESH_WORK_BIN_SIZE],
    int short_write_error,
    const RefreshWorkBinOps *ops,
    int *cleanup_error);

#endif
