/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "pfs.h"
#include "pfs_core.h"
#include "utils.h"

/*
  SceAppMgr mount IDs:
  0x64: ux0:picture
  0x65: ur0:user/00/psnfriend
  0x66: ur0:user/00/psnmsg
  0x69: ux0:music
  0x6E: ux0:appmeta
  0xC8: ur0:temp/sqlite
  0xCD: ux0:cache
  0x12E: ur0:user/00/trophy/data/sce_trop
  0x12F: ur0:user/00/trophy/data
  0x3E8: ux0:app, vs0:app, gro0:app
  0x3E9: ux0:patch
  0x3EB: ?
  0x3EA: ux0:addcont
  0x3EC: ux0:theme
  0x3ED: ux0:user/00/savedata
  0x3EE: ur0:user/00/savedata
  0x3EF: vs0:sys/external
  0x3F0: vs0:data/external
*/

char pfs_mounted_path[MAX_PATH_LENGTH];
char pfs_mount_point[MAX_MOUNT_POINT_LENGTH];
int read_only;

typedef struct {
  ShellMountIdArgs args;
  char klicensee[0x10];
} PfsMountContext;

static int mountById(void *context, const char *path, int id) {
  PfsMountContext *mount_context = context;
  mount_context->args.path = path;
  mount_context->args.id = id;
  return shellUserMountById(&mount_context->args);
}

static int mountGameData(void *context, const char *path) {
  (void)context;
  return sceAppMgrGameDataMount(path, 0, 0, pfs_mount_point);
}

static void logMountAttempt(void *context, const PfsAttempt *attempt) {
  (void)context;
  char message[MAX_PATH_LENGTH + 128];
  pfsFormatAttempt(message, sizeof(message), attempt);
  debugPrintf("%s", message);
}

int pfsMount(const char *path) {
  PfsMountContext context;
  memset(&context, 0, sizeof(context));

/*
  char work_path[MAX_PATH_LENGTH];
  char license_buf[0x200];
  snprintf(work_path, MAX_PATH_LENGTH, "%ssce_sys/package/work.bin", path);
  if (ReadFile(work_path, license_buf, sizeof(license_buf)) == sizeof(license_buf)) {
    int res = shellUserGetRifVitaKey(license_buf, context.klicensee);
    debugPrintf("read license: 0x%08X\n", res);
  }
*/
  context.args.process_titleid = VITASHELL_TITLEID;
  context.args.desired_mount_point = NULL;
  context.args.klicensee = context.klicensee;
  context.args.mount_point = pfs_mount_point;

  PfsMountOps ops = {
    &context,
    mountById,
    mountGameData,
    logMountAttempt,
  };
  return pfsMountSequence(path, &ops, &read_only);
}

int pfsUmountIfMounted() {
  if (pfs_mount_point[0] == 0)
    return 0;

  return pfsUmount();
}

int pfsUmount() {
  if (pfs_mount_point[0] == 0)
    return -1;

  int res = sceAppMgrUmount(pfs_mount_point);
  if (res >= 0) {
    memset(pfs_mount_point, 0, sizeof(pfs_mount_point));
    memset(pfs_mounted_path, 0, sizeof(pfs_mounted_path));
  }

  return res;
}