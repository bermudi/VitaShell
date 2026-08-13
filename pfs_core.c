/* Portable PFS mount sequencing and decrypted-path helpers. */

#include "pfs_core.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

const int pfs_known_ids[PFS_KNOWN_ID_COUNT] = {
  0x6E,
  0x12E,
  0x12F,
  0x3ED,
};

static void reportAttempt(const PfsMountOps *ops, PfsAttemptKind kind,
                          const char *path, int id, int result) {
  if (ops->report_attempt == NULL)
    return;

  PfsAttempt attempt = { kind, path, id, result };
  ops->report_attempt(ops->context, &attempt);
}

int pfsMountSequence(const char *path, const PfsMountOps *ops, int *read_only) {
  for (size_t i = 0; i < PFS_KNOWN_ID_COUNT; i++) {
    int result = ops->mount_by_id(ops->context, path, pfs_known_ids[i]);
    reportAttempt(ops, PFS_ATTEMPT_BY_ID, path, pfs_known_ids[i], result);
    if (result >= 0) {
      *read_only = 0;
      return result;
    }
  }

  int result = ops->mount_game_data(ops->context, path);
  reportAttempt(ops, PFS_ATTEMPT_GAME_DATA, path, 0, result);
  if (result >= 0)
    *read_only = 1;
  return result;
}

int pfsFormatAttempt(char *buffer, size_t size, const PfsAttempt *attempt) {
  if (attempt->kind == PFS_ATTEMPT_BY_ID) {
    return snprintf(buffer, size,
                    "PFS mount: shellUserMountById(id=0x%X) returned "
                    "0x%08X; path=%s\n",
                    (unsigned int)attempt->id,
                    (unsigned int)attempt->result,
                    attempt->path);
  }

  return snprintf(buffer, size,
                  "PFS mount: sceAppMgrGameDataMount returned 0x%08X; "
                  "path=%s\n",
                  (unsigned int)attempt->result,
                  attempt->path);
}

int pfsJoinPath(char *buffer, size_t size, const char *directory,
                const char *entry) {
  size_t directory_length = strlen(directory);
  int needs_separator = directory_length > 0 &&
                        directory[directory_length - 1] != '/';
  int result = snprintf(buffer, size, "%s%s%s", directory,
                        needs_separator ? "/" : "", entry);
  if (result < 0 || (size_t)result >= size)
    return -1;
  return 0;
}

int pfsBuildScePfsPath(char *buffer, size_t size, const char *directory,
                       const char *entry) {
  char entry_path[1024];
  if (pfsJoinPath(entry_path, sizeof(entry_path), directory, entry) < 0)
    return -1;
  return pfsJoinPath(buffer, size, entry_path, "sce_pfs");
}

int pfsIsPatchDirectory(const char *directory) {
  size_t length = strlen(directory);
  while (length > 0 && directory[length - 1] == '/')
    length--;

  return (length == strlen("ux0:patch") &&
          strncasecmp(directory, "ux0:patch", length) == 0) ||
         (length == strlen("grw0:patch") &&
          strncasecmp(directory, "grw0:patch", length) == 0);
}

static int mountCandidate(const char *directory, const char *entry,
                          PfsMountPathFn mount_path, void *context,
                          char *mounted_source, size_t mounted_source_size) {
  if (pfsJoinPath(mounted_source, mounted_source_size, directory, entry) < 0)
    return -1;
  return mount_path(context, mounted_source);
}

int pfsMountBrowserEntry(const char *directory, const char *entry,
                         PfsMountPathFn mount_path, void *context,
                         char *mounted_source, size_t mounted_source_size) {
  int result = mountCandidate(directory, entry, mount_path, context,
                              mounted_source, mounted_source_size);
  if (result >= 0 || !pfsIsPatchDirectory(directory))
    return result;

  result = mountCandidate("ux0:app", entry, mount_path, context,
                          mounted_source, mounted_source_size);
  if (result >= 0)
    return result;

  return mountCandidate("gro0:app", entry, mount_path, context,
                        mounted_source, mounted_source_size);
}
