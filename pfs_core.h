/* Portable PFS mount sequencing and decrypted-path helpers. */

#ifndef __PFS_CORE_H__
#define __PFS_CORE_H__

#include <stddef.h>

#define PFS_KNOWN_ID_COUNT 4

typedef enum {
  PFS_ATTEMPT_BY_ID,
  PFS_ATTEMPT_GAME_DATA
} PfsAttemptKind;

typedef struct {
  PfsAttemptKind kind;
  const char *path;
  int id;
  int result;
} PfsAttempt;

typedef int (*PfsMountByIdFn)(void *context, const char *path, int id);
typedef int (*PfsMountGameDataFn)(void *context, const char *path);
typedef void (*PfsAttemptFn)(void *context, const PfsAttempt *attempt);

typedef struct {
  void *context;
  PfsMountByIdFn mount_by_id;
  PfsMountGameDataFn mount_game_data;
  PfsAttemptFn report_attempt;
} PfsMountOps;

extern const int pfs_known_ids[PFS_KNOWN_ID_COUNT];

int pfsMountSequence(const char *path, const PfsMountOps *ops, int *read_only);

int pfsFormatAttempt(char *buffer, size_t size, const PfsAttempt *attempt);

int pfsJoinPath(char *buffer, size_t size, const char *directory,
                const char *entry);

int pfsBuildScePfsPath(char *buffer, size_t size, const char *directory,
                       const char *entry);

int pfsIsPatchDirectory(const char *directory);

typedef int (*PfsMountPathFn)(void *context, const char *path);

/*
  Mounts the direct browser path. Patch directories additionally fall back to
  ux0:app and gro0:app. Returns the final mount error when every candidate
  fails. On success, mounted_source contains the candidate that worked.
*/
int pfsMountBrowserEntry(const char *directory, const char *entry,
                         PfsMountPathFn mount_path, void *context,
                         char *mounted_source, size_t mounted_source_size);

#endif
