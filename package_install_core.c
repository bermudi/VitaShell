/*
  Portable package-install staging ownership helpers.
*/

#include "package_install_core.h"

int packageRestoreMovedSource(
    PackageStagingOwnership *ownership,
    const char *source,
    const char *staging,
    void *context,
    PackageStagingRenameFn rename_path) {
  if (*ownership != PACKAGE_STAGING_MOVED_SOURCE)
    return 0;

  int error = rename_path(context, staging, source);
  if (error >= 0)
    *ownership = PACKAGE_STAGING_UNOWNED;
  return error;
}

int packageCleanupStaging(
    PackageStagingOwnership ownership,
    const char *staging,
    void *context,
    PackageStagingRemoveFn remove_path) {
  if (ownership != PACKAGE_STAGING_DISPOSABLE)
    return 0;
  return remove_path(context, staging);
}
