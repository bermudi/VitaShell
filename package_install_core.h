/*
  Portable package-install staging ownership helpers.
*/

#ifndef __PACKAGE_INSTALL_CORE_H__
#define __PACKAGE_INSTALL_CORE_H__

typedef enum {
  PACKAGE_STAGING_UNOWNED,
  PACKAGE_STAGING_DISPOSABLE,
  PACKAGE_STAGING_MOVED_SOURCE
} PackageStagingOwnership;

typedef int (*PackageStagingRenameFn)(void *context, const char *source,
                                      const char *destination);
typedef int (*PackageStagingRemoveFn)(void *context, const char *path);

/*
  Restores a folder moved into staging. A failed restore deliberately leaves
  ownership as MOVED_SOURCE so exit cleanup cannot delete the only copy.
*/
int packageRestoreMovedSource(
    PackageStagingOwnership *ownership,
    const char *source,
    const char *staging,
    void *context,
    PackageStagingRenameFn rename_path);

/*
  Removes staging only when it contains data created by this operation or a
  copy left after a committed promotion. UNOWNED and MOVED_SOURCE are never
  removed.
*/
int packageCleanupStaging(
    PackageStagingOwnership ownership,
    const char *staging,
    void *context,
    PackageStagingRemoveFn remove_path);

#endif
