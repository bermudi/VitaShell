/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW
  Copyright (C) 2017, VitaSmith
  Copyright (C) 2018, TheRadziu
  Copyright (C) 2020, SilicaAndPina

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

#include <psp2/vshbridge.h>
#include "main.h"
#include "init.h"
#include "io_process.h"
#include "refresh.h"
#include "refresh_core.h"
#include "package_installer.h"
#include "sfo.h"
#include "file.h"
#include "message_dialog.h"
#include "language.h"
#include "utils.h"
#include "rif.h"
#include "pfs.h"
#include "pbp.h"

// Note: The promotion process is *VERY* sensitive to the directories used below
// Don't change them unless you know what you are doing!
#define APP_TEMP "ux0:temp/app"
#define DLC_TEMP "ux0:temp/addcont"
#define PATCH_TEMP "ux0:temp/patch"
#define PSM_TEMP "ux0:temp/game"
#define THEME_TEMP "ux0:temp/theme"
#define PSP_TEMP "ux0:pspemu/temp/game"

#define MAX_DLC_PER_TITLE 1024

int isCustomHomebrew(const char* path) {
  uint32_t work[RIF_SIZE/4];

  if (ReadFile(path, work, sizeof(work)) != sizeof(work))
    return 0;

  for (int i = 0; i < sizeof(work) / sizeof(uint32_t); i++)
    if (work[i] != 0)
      return 0;

  return 1;
}

int refreshNeeded(const char *app_path, const char* content_type) {
  char appmeta_path[MAX_PATH_LENGTH];
  char appmeta_param[MAX_PATH_LENGTH];
  char sfo_path[MAX_PATH_LENGTH];
  int mounted_appmeta;
  char titleid[12], contentid[50], appver[8];
  
  if(strcmp(content_type,"psm") == 0) 
  {
    char contentid_path[MAX_PATH_LENGTH];
    void *cidFile = NULL;
    
    //Initalize buffers
    memset(titleid,0,12);
    memset(contentid,0,50);
    memset(appver,0,8);
    
    snprintf(contentid_path, MAX_PATH_LENGTH, "%s/RW/System/content_id", app_path);
    
    // Get content id
    int contentid_size = allocateReadFile(contentid_path, &cidFile);
    if(contentid_size != 48) //Check if valid contentid file
      return 0;
  
    // Get title id from content id
    strncpy(titleid,cidFile+7,9);
    strncpy(contentid,cidFile,49);
    
    
    free(cidFile);
  }
  else if(strcmp(content_type, "psp") == 0) {
      // read eboot.pbp
      char ebootpbp_path[MAX_PATH_LENGTH];
      
      // Initalize buffers
      memset(titleid,0,12);
      memset(contentid,0,50);
      memset(appver,0,8);
  
      snprintf(ebootpbp_path, MAX_PATH_LENGTH, "%s/EBOOT.PBP", app_path);
      
      // the vita actually uses the folder name as the title id in PSP case
      // this is also important for e.g cloning trick
      char* app_directory = getFilename(app_path);
      if(TITLEID_FMT_CHECK(app_directory)){
        if(app_directory != NULL) free(app_directory);
        return 0;
      }
      strncpy(titleid, app_directory, 11);
      free(app_directory);	  
      snprintf(ebootpbp_path, MAX_PATH_LENGTH, "%s/EBOOT.PBP", app_path);
      
      int pbp_type = get_pbp_type(ebootpbp_path);
      if(pbp_type == PBP_TYPE_UNKNOWN)
        return 0;
      
      // Get content_id
      if(!get_pbp_content_id(ebootpbp_path, contentid)) 
        return 0;
      
      // Get param.sfo
      void *sfo_buffer = NULL;
      int sfo_size = get_pbp_sfo(ebootpbp_path, &sfo_buffer);
      if(sfo_size <= 0)
        return 0;
      
	  // always use real disc id from param.sfo for PS1 titles because
	  // if a ps1 game is installed to the wrong directory, will give
	  // "Failed to open the memory card" error message.
	  if(pbp_type == PBP_TYPE_PSISOIMG || pbp_type == PBP_TYPE_PSTITLEIMG)
        getSfoString(sfo_buffer, "DISC_ID", titleid, sizeof(titleid));
	
      getSfoString(sfo_buffer, "APP_VER", appver, sizeof(appver));
        
      // ps1 do not have APP_VER
      if(strcmp(appver, "") == 0)
        strcpy(appver, "01.00");

      // free sfo_buffer
      if(sfo_buffer != NULL)
        free(sfo_buffer);
  }
  else {  
    // Read param.sfo
    snprintf(sfo_path, MAX_PATH_LENGTH, "%s/sce_sys/param.sfo", app_path);
    
    void *sfo_buffer = NULL;
    int sfo_size = allocateReadFile(sfo_path, &sfo_buffer);
    if (sfo_size < 0)
      return 0;

    // Get title and content ids
    
    getSfoString(sfo_buffer, "TITLE_ID", titleid, sizeof(titleid));
    getSfoString(sfo_buffer, "CONTENT_ID", contentid, sizeof(contentid));
    getSfoString(sfo_buffer, "APP_VER", appver, sizeof(appver));

    // Free sfo buffer
    free(sfo_buffer);
  }
  
  
  // Check if app or dlc exists
  if(((strcmp(content_type, "app") == 0) || (strcmp(content_type, "dlc") == 0)) && (checkAppExist(titleid))) {
    char rif_name[48];
    char rif_path[MAX_PATH_LENGTH];
  
    uint64_t aid;
    sceRegMgrGetKeyBin("/CONFIG/NP", "account_id", &aid, sizeof(uint64_t));

    // Check if bounded rif file exits
    _sceNpDrmGetRifName(rif_name, aid);
    if (strcmp(content_type, "app") == 0)
      snprintf(rif_path, MAX_PATH_LENGTH, "ux0:license/app/%s/%s", titleid, rif_name);
    else if (strcmp(content_type, "dlc") == 0)
      snprintf(rif_path, MAX_PATH_LENGTH, "ux0:license/addcont/%s/%s/%s", titleid, &contentid[20], rif_name);
    if (checkFileExist(rif_path))
      return 0;
  
    // Check if fixed rif file exits
    _sceNpDrmGetFixedRifName(rif_name, 0);
    if (strcmp(content_type, "app") == 0)
      snprintf(rif_path, MAX_PATH_LENGTH, "ux0:license/app/%s/%s", titleid, rif_name);
    else if (strcmp(content_type, "dlc") == 0)
      snprintf(rif_path, MAX_PATH_LENGTH, "ux0:license/addcont/%s/%s/%s", titleid, &contentid[20], rif_name);
    if (checkFileExist(rif_path))
      return 0;
  
 }
  // Check if patch for installed app exists
  else if (strcmp(content_type, "patch") == 0) {
    if (!checkAppExist(titleid))
      return 0;
  if (checkFileExist(sfo_path)) {
    void *sfo_buffer = NULL;
      snprintf(appmeta_path, MAX_PATH_LENGTH, "ux0:appmeta/%s", titleid);
      pfsUmount();
      if(pfsMount(appmeta_path)<0)
        return 0;
      //Now read it
    snprintf(appmeta_param, MAX_PATH_LENGTH, "ux0:appmeta/%s/param.sfo", titleid);
      int sfo_size = allocateReadFile(appmeta_param, &sfo_buffer);
      if (sfo_size < 0)
        return sfo_size;
      char promoted_appver[8];
      getSfoString(sfo_buffer, "APP_VER", promoted_appver, sizeof(promoted_appver));
      pfsUmount();
    //Finally compare it
    if (strcmp(appver, promoted_appver) == 0)
      return 0;
    }
  }
  // license not needed to promote psp or psm contents
  else if((strcmp(content_type, "psm") == 0 || strcmp(content_type, "psp") == 0) && checkAppExist(titleid)) { 
    if(strcmp(content_type, "psp") == 0) {
      char eboot_signature[0x200];
	  
      // get path to eboot.pbp
      char ebootpbp_path[MAX_PATH_LENGTH];
      snprintf(ebootpbp_path, MAX_PATH_LENGTH, "%s/EBOOT.PBP", app_path);
      
      // get path to __sce_ebootpbp
     char sce_ebootpbp[MAX_PATH_LENGTH];
      snprintf(sce_ebootpbp, MAX_PATH_LENGTH, "%s/__sce_ebootpbp", app_path);
            
      // check EBOOT.PBP exists
      if(getFileSize(ebootpbp_path) < 0)
        return 0;
      
      int sce_ebootpbp_exist = (getFileSize(sce_ebootpbp) >= 0);
    
      // verify __sce_ebootpbp
      if(sce_ebootpbp_exist) {
        int read_sz = ReadFile(sce_ebootpbp, eboot_signature, 0x200);
        
		char *unk0 = NULL;
		int verify = _vshNpDrmEbootSigVerify(ebootpbp_path, eboot_signature, &unk0);
		
        if(verify < 0) // if signature is invalid, then needs refresh
          return 1;
        
        return 0;
      }
	  else {
        return 1;
	  }
    }
    return 0;
  }
  return 1;
}

static int openWorkBin(void *context, const char *path) {
  (void)context;
  return sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

static int writeWorkBin(void *context, int fd, const void *buffer, size_t size) {
  (void)context;
  return sceIoWrite(fd, buffer, size);
}

static int closeWorkBin(void *context, int fd) {
  (void)context;
  return sceIoClose(fd);
}

static int removeWorkBin(void *context, const char *path) {
  (void)context;
  return sceIoRemove(path);
}

static int renameWorkBin(void *context, const char *source, const char *destination) {
  (void)context;
  return sceIoRename(source, destination);
}

static const RefreshWorkBinOps work_bin_ops = {
  NULL,
  openWorkBin,
  writeWorkBin,
  closeWorkBin,
  removeWorkBin,
  renameWorkBin,
};

RefreshPromotionResult refreshApp(const char *app_path) {
  char work_bin_path[MAX_PATH_LENGTH];
  int res;

  RefreshPromotionResult result = { 0, 0, 0 };

  snprintf(work_bin_path, MAX_PATH_LENGTH, "%s/sce_sys/package/work.bin", app_path);

  // Remove work.bin for custom homebrews
  if (isCustomHomebrew(work_bin_path)) {
    res = sceIoRemove(work_bin_path);
    if (res < 0)
      result.work_bin_error = res;
  } else if (!checkFileExist(work_bin_path)) {
    // If available, restore work.bin from license.db
    void *sfo_buffer = NULL;
    char sfo_path[MAX_PATH_LENGTH], contentid[50];
    snprintf(sfo_path, MAX_PATH_LENGTH, "%s/sce_sys/param.sfo", app_path);
    int sfo_size = allocateReadFile(sfo_path, &sfo_buffer);
    if (sfo_size <= 0) {
      result.work_bin_error = (sfo_size < 0) ? sfo_size : VITASHELL_ERROR_INVALID_MAGIC;
    } else {
      res = getSfoString(sfo_buffer, "CONTENT_ID", contentid, sizeof(contentid));
      if (res < 0) {
        result.work_bin_error = res;
      } else {
        uint8_t *rif = query_rif(LICENSE_DB, contentid);
        if (rif == NULL) {
          result.work_bin_error = VITASHELL_ERROR_NOT_FOUND;
        } else {
          char temporary_path[MAX_PATH_LENGTH];
          snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", work_bin_path);
          sceIoRemove(temporary_path);
          int cleanup_error = 0;
          result.work_bin_error = refreshWriteWorkBin(
              work_bin_path, temporary_path, rif, VITASHELL_ERROR_INTERNAL,
              &work_bin_ops, &cleanup_error);
          if (cleanup_error < 0)
            debugPrintf("Refresh LiveArea: partial work.bin cleanup failed for %s: 0x%08X\n",
                        temporary_path, cleanup_error);
          free(rif);
        }
      }
    }
    free(sfo_buffer);
  }

  if (result.work_bin_error < 0)
    debugPrintf("Refresh LiveArea: work.bin unavailable for %s: 0x%08X\n",
                app_path, result.work_bin_error);

  // Promote vita app/vita dlc/vita patch (if needed)
  PromoteAppResult promotion = promoteAppWithStatus(app_path);
  result.error = promotion.error;
  result.committed = promotion.committed;
  if (result.error < 0)
    debugPrintf("Refresh LiveArea: promotion failed for %s: 0x%08X%s\n",
                app_path, result.error,
                result.committed ? " after the package was committed" : "");
  return result;
}

// target_type should be either SCE_S_IFREG for files or SCE_S_IFDIR for directories
int parse_dir_with_callback(int target_type, const char* path, void(*callback)(void*, const char*, const char*), void* data) {
  SceUID dfd = sceIoDopen(path);
  if (dfd < 0)
    return dfd;

  int res = 0;
  do {
    SceIoDirent dir;
    memset(&dir, 0, sizeof(SceIoDirent));

    res = sceIoDread(dfd, &dir);
    if (res > 0 && (dir.d_stat.st_mode & SCE_S_IFMT) == target_type) {
      callback(data, path, dir.d_name);
      if (cancelHandler()) {
        closeWaitDialog();
        setDialogStep(DIALOG_STEP_CANCELED);
        sceIoDclose(dfd);
        return 1;
      }
    }
  } while (res > 0);

  int close_res = sceIoDclose(dfd);
  if (res < 0)
    return res;
  return close_res;
}

typedef struct {
  int refresh_pass;
  int count;
  int processed;
  int canceled;
  RefreshResults results;
} refresh_data_t;

typedef struct {
  refresh_data_t *refresh_data;
  char* list[MAX_DLC_PER_TITLE];
  int list_size;
} dlc_data_t;

typedef struct {
  int copy_pass;
  int count;
  int processed;
  int copied;
  int cur_depth;
  int max_depth;
  uint8_t* rif;
} license_data_t;

static void recordRefreshError(refresh_data_t *refresh_data, int error, const char *operation,
                               const char *path) {
  if (error >= 0)
    return;

  debugPrintf("Refresh LiveArea: %s failed for %s: 0x%08X\n", operation, path, error);
  if (refresh_data->results.first_error == 0)
    refresh_data->results.first_error = error;
}

static int renameRefreshPath(void *context, const char *source, const char *destination) {
  (void)context;
  return sceIoRename(source, destination);
}

static RefreshPromotionResult promoteRefreshPath(void *context, const char *path) {
  (void)context;
  return refreshApp(path);
}

static void logRefreshError(void *context, int error, const char *operation,
                            const char *path) {
  (void)context;
  debugPrintf("Refresh LiveArea: %s failed for %s: 0x%08X\n", operation, path, error);
}

static const RefreshTransactionOps refresh_ops = {
  NULL,
  renameRefreshPath,
  promoteRefreshPath,
  logRefreshError,
};

static const RefreshOperationNames app_operations = {
  "staging rename",
  "promotion",
  "restore rename",
  "work.bin preparation",
};

static const RefreshOperationNames dlc_operations = {
  "DLC staging rename",
  "DLC promotion",
  "DLC restore rename",
  "DLC work.bin preparation",
};

static void stageAndRefresh(refresh_data_t *refresh_data, const char *source,
                            const char *staging) {
  refreshStageAndPromote(&refresh_data->results, source, staging,
                         &refresh_ops, &app_operations);
}

void app_callback(void* data, const char* dir, const char* subdir) {
  refresh_data_t *refresh_data = (refresh_data_t*)data;
  char path[MAX_PATH_LENGTH];

  if (strcasecmp(subdir, vitashell_titleid) == 0)
    return;

  if (refresh_data->refresh_pass) {
    if (refresh_data->results.restore_error < 0) {
      SetProgress(++refresh_data->processed, refresh_data->count);
      return;
    }

    snprintf(path, MAX_PATH_LENGTH, "%s/%s", dir, subdir);
    if (refreshNeeded(path, "app")) {
      // Move the directory to temp for installation
      if (checkFolderExist(APP_TEMP))
        recordRefreshError(refresh_data, SCE_ERROR_ERRNO_EEXIST,
                           "occupied staging directory", APP_TEMP);
      else
        stageAndRefresh(refresh_data, path, APP_TEMP);
    }
    SetProgress(++refresh_data->processed, refresh_data->count);
  } else {
    refresh_data->count++;
  }
}

void dlc_callback_inner(void* data, const char* dir, const char* subdir) {
  dlc_data_t *dlc_data = (dlc_data_t*)data;
  char path[MAX_PATH_LENGTH];

  // Ignore  "sce_sys" and "sce_pfs" directories
  if (strncasecmp(subdir, "sce_", 4) == 0)
    return;

  if (dlc_data->refresh_data->refresh_pass) {
    snprintf(path, MAX_PATH_LENGTH, "%s/%s", dir, subdir);
    if (dlc_data->list_size < MAX_DLC_PER_TITLE)
      dlc_data->list[dlc_data->list_size++] = strdup(path);
  } else {
    dlc_data->refresh_data->count++;
  }
}

void dlc_callback_outer(void* data, const char* dir, const char* subdir) {
  refresh_data_t *refresh_data = (refresh_data_t*)data;
  dlc_data_t dlc_data;
  dlc_data.refresh_data = refresh_data;
  dlc_data.list_size = 0;
  char path[MAX_PATH_LENGTH];

  // Get the title's dlc subdirectories
  int len = snprintf(path, sizeof(path), "%s/%s", dir, subdir);
  int scan_res = parse_dir_with_callback(SCE_S_IFDIR, path, dlc_callback_inner, &dlc_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data->canceled = 1;
    else
      recordRefreshError(refresh_data, scan_res, "DLC directory scan", path);
    for (int i = 0; i < dlc_data.list_size; i++)
      free(dlc_data.list[i]);
    return;
  }

  if (refresh_data->refresh_pass) {
    if (refresh_data->results.restore_error < 0) {
      for (int i = 0; i < dlc_data.list_size; i++)
        free(dlc_data.list[i]);
      return;
    }

    // For dlc, the process happens in two phases to avoid promotion errors:
    // 1. Move all dlc that require refresh out of addcont/title_id
    // 2. Refresh the moved dlc_data
    for (int i = 0; i < dlc_data.list_size; i++) {
      if (refreshNeeded(dlc_data.list[i], "dlc")) {
        snprintf(path, MAX_PATH_LENGTH, DLC_TEMP "/%s", &dlc_data.list[i][len + 1]);
        if (checkFolderExist(path)) {
          recordRefreshError(refresh_data, SCE_ERROR_ERRNO_EEXIST,
                             "occupied DLC staging directory", path);
          free(dlc_data.list[i]);
          dlc_data.list[i] = NULL;
          SetProgress(++refresh_data->processed, refresh_data->count);
          continue;
        }
        int res = sceIoRename(dlc_data.list[i], path);
        if (res < 0) {
          recordRefreshError(refresh_data, res, "DLC staging rename", dlc_data.list[i]);
          free(dlc_data.list[i]);
          dlc_data.list[i] = NULL;
          SetProgress(++refresh_data->processed, refresh_data->count);
        }
      } else {
        free(dlc_data.list[i]);
        dlc_data.list[i] = NULL;
        SetProgress(++refresh_data->processed, refresh_data->count);
      }
    }

    // Now that the dlc we need are out of addcont/title_id, refresh them.
    // If one restore fails, restore every remaining staged DLC instead of
    // promoting or deleting anything else.
    for (int i = 0; i < dlc_data.list_size; i++) {
      if (dlc_data.list[i] != NULL) {
        refreshRestoreOrPromoteDlc(&refresh_data->results, &dlc_data.list[i],
                                   1, DLC_TEMP, &refresh_ops,
                                   &dlc_operations);
        SetProgress(++refresh_data->processed, refresh_data->count);
        free(dlc_data.list[i]);
      }
    }
  }
}

void patch_callback(void* data, const char* dir, const char* subdir) {
  refresh_data_t *refresh_data = (refresh_data_t*)data;
  char path[MAX_PATH_LENGTH];

  if (refresh_data->refresh_pass) {
    if (refresh_data->results.restore_error < 0) {
      SetProgress(++refresh_data->processed, refresh_data->count);
      return;
    }

    snprintf(path, MAX_PATH_LENGTH, "%s/%s", dir, subdir);
    if (refreshNeeded(path, "patch")) {
      // Move the directory to temp for installation
      if (checkFolderExist(PATCH_TEMP))
        recordRefreshError(refresh_data, SCE_ERROR_ERRNO_EEXIST,
                           "occupied staging directory", PATCH_TEMP);
      else
        stageAndRefresh(refresh_data, path, PATCH_TEMP);
    }
    SetProgress(++refresh_data->processed, refresh_data->count);
  } else {
    refresh_data->count++;
  }
}

void psp_callback(void* data, const char* dir, const char* subdir) {
  refresh_data_t *refresh_data = (refresh_data_t*)data;
  char path[MAX_PATH_LENGTH];

  if (refresh_data->refresh_pass) {
      if (refresh_data->results.restore_error < 0) {
        SetProgress(++refresh_data->processed, refresh_data->count);
        return;
      }

      snprintf(path, MAX_PATH_LENGTH, "%s/%s", dir, subdir);
      if (refreshNeeded(path, "psp")) {
        char contentid[0x30];
        
        char sce_ebootpbp[MAX_PATH_LENGTH];
        char eboot_pbp[MAX_PATH_LENGTH];
        char license_rif[MAX_PATH_LENGTH];
        
        snprintf(eboot_pbp, MAX_PATH_LENGTH, "%s/EBOOT.PBP", path);
        snprintf(sce_ebootpbp, MAX_PATH_LENGTH, "%s/__sce_ebootpbp", path);
		
		// get pbp type
		int pbp_type = get_pbp_type(eboot_pbp);
        if(pbp_type != PBP_TYPE_UNKNOWN) {
		  
          // cache current __sce_ebootpbp signature file
          void* sce_ebootpbp_sig_data = NULL;
          int sce_ebootpbp_sz = allocateReadFile(sce_ebootpbp, &sce_ebootpbp_sig_data);
		  		  
          
          if(get_pbp_content_id(eboot_pbp, contentid)) {
              // create directories
              char promote_psp_folder[MAX_PATH_LENGTH];
              char promote_psp_game_folder[MAX_PATH_LENGTH];
              char promote_psp_license_folder[MAX_PATH_LENGTH];

              char promote_license_rif[MAX_PATH_LENGTH];
              char promote_game_folder[MAX_PATH_LENGTH];

              snprintf(promote_psp_folder, MAX_PATH_LENGTH, "%s/PSP", PSP_TEMP);
              snprintf(promote_psp_game_folder, MAX_PATH_LENGTH, "%s/PSP/GAME", PSP_TEMP);
              snprintf(promote_psp_license_folder, MAX_PATH_LENGTH, "%s/PSP/LICENSE", PSP_TEMP);

              snprintf(promote_license_rif, MAX_PATH_LENGTH, "%s/PSP/LICENSE/%s.rif", PSP_TEMP, contentid);

              void *sfo_buffer = NULL;
              int sfo_size = get_pbp_sfo(eboot_pbp, &sfo_buffer);

            if(sfo_size >= 0) {

              char discid[12];

              getSfoString(sfo_buffer, "DISC_ID", discid, sizeof(discid));

              // maintain compatiblity with psp bubble cloning, and other tricks
              // use folder name as disc id, *only* on npumdimg
              if(pbp_type == PBP_TYPE_NPUMDIMG)
                strncpy(discid, subdir, sizeof(discid)-1);

              // ensure its installing PS1 to the correct folder ..
              // if ps1 installed to incorrect folder, will give
              // 'cannot open the memory card' error message
              snprintf(promote_game_folder, MAX_PATH_LENGTH, "%s/PSP/GAME/%s", PSP_TEMP, discid);
              sceClibPrintf("promote_game_folder: %s\n", promote_game_folder);
              sceClibPrintf("game_folder: %s\n", path);

              // get current rif location
              snprintf(license_rif, MAX_PATH_LENGTH, "ux0:/pspemu/PSP/LICENSE/%s.rif", contentid);

              // create the promote directories with proper permissions for USB visibility

              sceIoMkdir("ux0:pspemu", 0777);
              sceIoMkdir("ux0:pspemu/temp", 0777);
              sceIoMkdir(PSP_TEMP, 0777);
              sceIoMkdir(promote_psp_folder, 0777);
              sceIoMkdir(promote_psp_game_folder, 0777);
              sceIoMkdir(promote_psp_license_folder, 0777);
              
              // copy the rif to the promote location
              int res = copyFile(license_rif, promote_license_rif, NULL);
              
              if(res < 0) { // no rif found?
                // generate fake psp license
                SceNpDrmLicense license;
                memset(&license, 0x00, sizeof(SceNpDrmLicense));
                license.account_id = 0x0123456789ABCDEFLL;
                memset(license.ecdsa_signature, 0xFF, 0x28);
                strncpy(license.content_id, contentid, 0x30);
                WriteFile(promote_license_rif, &license, offsetof(SceNpDrmLicense, flags));
              }
              
              // promote will fail if __sce_ebootpbp signature file is invalid (or for another account)
              // so we have to generate a new one ..
              sceIoRemove(sce_ebootpbp);
              
              int eboot_gen = gen_sce_ebootpbp(path, discid);
              
              // move path to promote folder
              int stage_res = sceIoRename(path, promote_game_folder);
              if (stage_res < 0) {
                recordRefreshError(refresh_data, stage_res, "PSP staging rename", path);
              } else {
                PromoteAppResult promotion = promoteCmaWithStatus(
                    PSP_TEMP, discid, SCE_PKG_TYPE_PSP);

                sceClibPrintf("eboot_gen: %x, promote %x\n", eboot_gen, promotion.error);

                if (promotion.error == 0 || promotion.committed) {
                  refresh_data->results.refreshed++;
                  if (promotion.error < 0) {
                    if (refresh_data->results.first_promotion_error == 0)
                      refresh_data->results.first_promotion_error = promotion.error;
                    recordRefreshError(refresh_data, promotion.error,
                                       "PSP post-promotion cleanup", promote_game_folder);
                  }
                } else {
                  if (refresh_data->results.first_promotion_error == 0)
                    refresh_data->results.first_promotion_error = promotion.error;
                  recordRefreshError(refresh_data, promotion.error, "PSP promotion", promote_game_folder);
                  int restore_res = sceIoRename(promote_game_folder, path);
                  recordRefreshError(refresh_data, restore_res, "PSP restore rename", promote_game_folder);
                  if (restore_res < 0) {
                    if (refresh_data->results.restore_error == 0)
                      refresh_data->results.restore_error = restore_res;
                  } else {
                    removePath(PSP_TEMP, NULL); // delete what was created
                  }
                }
              }
              
              // if eboot signature generation was unsuccessful, write original signature back
              if(eboot_gen < 0) {
                if(sce_ebootpbp_sz > 0)
                  WriteFile(sce_ebootpbp, sce_ebootpbp_sig_data, sce_ebootpbp_sz); // Restore __sce_ebootpbp on error
              }
            }

            if(sfo_buffer != NULL)
              free(sfo_buffer);
          
          }
          
          if(sce_ebootpbp_sig_data != NULL)
            free(sce_ebootpbp_sig_data);
          
         }
	  }
    SetProgress(++refresh_data->processed, refresh_data->count);
  } else {
    refresh_data->count++;
  }

}

void psm_callback(void* data, const char* dir, const char* subdir) {
  refresh_data_t *refresh_data = (refresh_data_t*)data;
  char path[MAX_PATH_LENGTH];

  if (refresh_data->refresh_pass) {
    if (refresh_data->results.restore_error < 0) {
      SetProgress(++refresh_data->processed, refresh_data->count);
      return;
    }

    snprintf(path, MAX_PATH_LENGTH, "%s/%s", dir, subdir);
    if (refreshNeeded(path, "psm")) {        
      char contentid_path[MAX_PATH_LENGTH];
      snprintf(contentid_path, MAX_PATH_LENGTH, "%s/RW/System/content_id", path);
      
      char titleid[12];
      void *cidFile = NULL;
      
      // Initalize Bufer
      memset(titleid,0,12);
  
      // Get content id
      allocateReadFile(contentid_path, &cidFile);
  
      // Get title id from content id
      strncpy(titleid,cidFile+7,9);
      
      //free buffers
      free(cidFile);
      
      
      // Get promote path
      char promote_path[MAX_PATH_LENGTH];
      snprintf(promote_path,MAX_PATH_LENGTH,"%s/%s",PSM_TEMP, titleid);
  
      // Move the directory to temp for installation
      if (checkFolderExist(promote_path)) {
        recordRefreshError(refresh_data, SCE_ERROR_ERRNO_EEXIST,
                           "occupied PSM staging directory", promote_path);
        SetProgress(++refresh_data->processed, refresh_data->count);
        return;
      }
      int stage_res = sceIoRename(path, promote_path);
      if (stage_res < 0) {
        recordRefreshError(refresh_data, stage_res, "PSM staging rename", path);
      } else {
        // Finally call promote
        PromoteAppResult promotion = promoteCmaWithStatus(
            PSM_TEMP, titleid, SCE_PKG_TYPE_PSM);
        if (promotion.error == 0 || promotion.committed) {
          refresh_data->results.refreshed++;
          if (promotion.error < 0) {
            if (refresh_data->results.first_promotion_error == 0)
              refresh_data->results.first_promotion_error = promotion.error;
            recordRefreshError(refresh_data, promotion.error,
                               "PSM post-promotion cleanup", promote_path);
          }
        } else {
          if (refresh_data->results.first_promotion_error == 0)
            refresh_data->results.first_promotion_error = promotion.error;
          recordRefreshError(refresh_data, promotion.error, "PSM promotion", promote_path);
          int restore_res = sceIoRename(promote_path, path);
          recordRefreshError(refresh_data, restore_res, "PSM restore rename", promote_path);
          if (restore_res < 0 && refresh_data->results.restore_error == 0)
            refresh_data->results.restore_error = restore_res;
        }
      }
    }
    SetProgress(++refresh_data->processed, refresh_data->count);
  } else {
    refresh_data->count++;
  }
}

int refresh_thread(SceSize args, void *argp)  {
  SceUID thid = -1;
  refresh_data_t refresh_data = { 0 };
  
  // Lock power timers
  powerLock();

  // Set progress to 0%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 0);
  sceKernelDelayThread(DIALOG_WAIT); // Needed to see the percentage

  // Count all content before starting the progress worker.
  int scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:app", app_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:app");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:addcont", dlc_callback_outer, &refresh_data);
  if (refresh_data.canceled || scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else if (scan_res < 0)
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:addcont");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:patch", patch_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:patch");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:psm", psm_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:psm");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:pspemu/PSP/GAME", psp_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:pspemu/PSP/GAME");
    goto FINISH;
  }

  // Update thread
  thid = createStartUpdateThread(refresh_data.count, 0);

  // Make sure we have the temp directories we need with proper USB visibility permissions
  sceIoMkdir("ux0:temp", 0777);
  sceIoMkdir("ux0:pspemu", 0777);
  sceIoMkdir("ux0:pspemu/temp", 0777);
  sceIoMkdir(DLC_TEMP, 0777);
  sceIoMkdir(PSM_TEMP, 0777);
  sceIoMkdir(PSP_TEMP, 0777);
  refresh_data.refresh_pass = 1;

  // Refresh all content, preserving the exact directory-scan error.
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:app", app_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:app");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:addcont", dlc_callback_outer, &refresh_data);
  if (refresh_data.canceled || scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else if (scan_res < 0)
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:addcont");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:patch", patch_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:patch");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:psm", psm_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:psm");
    goto FINISH;
  }
  scan_res = parse_dir_with_callback(SCE_S_IFDIR, "ux0:pspemu/PSP/GAME", psp_callback, &refresh_data);
  if (scan_res != 0) {
    if (scan_res > 0)
      refresh_data.canceled = 1;
    else
      recordRefreshError(&refresh_data, scan_res, "directory scan", "ux0:pspemu/PSP/GAME");
    goto FINISH;
  }

  sceIoRmdir(DLC_TEMP);
  sceIoRmdir(PATCH_TEMP);
  sceIoRmdir(PSM_TEMP);
  sceIoRmdir(PSP_TEMP);

FINISH:
  if (refresh_data.canceled)
    goto CLEANUP;

  // Set progress to 100%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 100);
  sceKernelDelayThread(COUNTUP_WAIT);

  // Close
  closeWaitDialog();

  int reported_error = refreshReportedError(&refresh_data.results);

  if (reported_error < 0) {
    char refreshed_message[128];
    char error_message[128];
    snprintf(refreshed_message, sizeof(refreshed_message), language_container[REFRESHED], refresh_data.results.refreshed);
    snprintf(error_message, sizeof(error_message), language_container[ERROR], reported_error);
    infoDialog("%s\n%s", refreshed_message, error_message);
  } else {
    infoDialog(language_container[REFRESHED], refresh_data.results.refreshed);
  }

CLEANUP:
  if (thid >= 0)
    sceKernelWaitThreadEnd(thid, NULL, NULL);

  // Unlock power timers
  powerUnlock();
  
  
  return sceKernelExitDeleteThread(0);
}

// Note: This is currently not optimized AT ALL.
// Ultimately, we want to use a single transaction and avoid trying to
// re-insert rifs that are already present.
void license_file_callback(void* data, const char* dir, const char* file) {
  license_data_t *license_data = (license_data_t*)data;
  char path[MAX_PATH_LENGTH];

  // Ignore non rif content
  if ((strlen(file) < 4) || (strcasecmp(&file[strlen(file) - 4], ".rif") != 0))
    return;
  if (license_data->copy_pass) {
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0777);
    if (fd > 0) {
      int read = sceIoRead(fd, license_data->rif, RIF_SIZE);
      if (read == RIF_SIZE) {
        if (insert_rif(LICENSE_DB, license_data->rif) == 0)
          license_data->copied++;
      }
      sceIoClose(fd);
    }
    SetProgress(++license_data->processed, license_data->count);
  } else {
    license_data->count++;
  }
}

void license_dir_callback(void* data, const char* dir, const char* subdir) {
  license_data_t *license_data = (license_data_t*)data;
  char path[MAX_PATH_LENGTH];

  snprintf(path, sizeof(path), "%s/%s", dir, subdir);
  if (++license_data->cur_depth == license_data->max_depth)
    parse_dir_with_callback(SCE_S_IFREG, path, license_file_callback, data);
  else
    parse_dir_with_callback(SCE_S_IFDIR, path, license_dir_callback, data);
  license_data->cur_depth--;
}

int license_thread(SceSize args, void *argp) {
  SceUID thid = -1;
  license_data_t license_data = { 0, 0, 0, 0, 0, 1, malloc(RIF_SIZE) };

  if (license_data.rif == NULL)
    goto EXIT;

  // Lock power timers
  powerLock();

  // Set progress to 0%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 0);
  sceKernelDelayThread(DIALOG_WAIT); // Needed to see the percentage

  // NB: ux0:license access requires elevated permisions
  if (parse_dir_with_callback(SCE_S_IFDIR, "ux0:license/app", license_dir_callback, &license_data) < 0)
    goto EXIT;
  license_data.max_depth++;
  if (parse_dir_with_callback(SCE_S_IFDIR, "ux0:license/addcont", license_dir_callback, &license_data) < 0)
    goto EXIT;

  // Update thread
  thid = createStartUpdateThread(license_data.count, 0);

  // Create the DB if needed
  SceUID fd = sceIoOpen(LICENSE_DB, SCE_O_RDONLY, 0777);
  if (fd > 0) {
    sceIoClose(fd);
  } else if (create_db(LICENSE_DB, LICENSE_DB_SCHEMA) != 0) {
    goto EXIT;
  }

  // Insert the licenses
  license_data.copy_pass = 1;
  license_data.max_depth = 1;
  if (parse_dir_with_callback(SCE_S_IFDIR, "ux0:license/app", license_dir_callback, &license_data) < 0)
    goto EXIT;
  license_data.max_depth++;
  if (parse_dir_with_callback(SCE_S_IFDIR, "ux0:license/addcont", license_dir_callback, &license_data) < 0)
    goto EXIT;

  // Set progress to 100%
  sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 100);
  sceKernelDelayThread(COUNTUP_WAIT);

  // Close
  closeWaitDialog();

  infoDialog(language_container[IMPORTED_LICENSES], license_data.copied);

EXIT:
  if (thid >= 0)
    sceKernelWaitThreadEnd(thid, NULL, NULL);

  // Unlock power timers
  powerUnlock();

  free(license_data.rif);
  return sceKernelExitDeleteThread(0);
}
