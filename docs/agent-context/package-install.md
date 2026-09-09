# Package Installation — Staging Safety

Filed from root AGENTS.md. Read when touching folder/VPK installation, the
updater, `ux0:data/pkg`, `makeHeadBin()`, or promoter cleanup.

## Shared staging path

Folder installs, VPK extraction, FTP promotion, and self-update all use:

```
ux0:data/pkg
```

An occupied path is never disposable by assumption. It may contain the only
copy of a folder preserved after a failed restoration. Claim staging with an
exclusive mkdir (archive/update data) or rename (folder install); if that
fails, report the collision and leave the path untouched.

## Ownership states

`package_install_core.c` tracks three states:

- `PACKAGE_STAGING_UNOWNED`: this operation has no right to remove the path.
- `PACKAGE_STAGING_DISPOSABLE`: temporary data created by this operation, or a
  staging copy left after the promoter committed.
- `PACKAGE_STAGING_MOVED_SOURCE`: a user-selected folder was moved here and may
  be the only copy.

Exit cleanup removes only `DISPOSABLE`. A failed restore remains
`MOVED_SOURCE`, so cleanup must be skipped. A successful restore becomes
`UNOWNED`. Once promotion commits, the staged folder becomes `DISPOSABLE`;
post-commit teardown errors must not trigger restoration.

Treat an already-absent staging path as successful post-commit cleanup because
nothing remains to remove. Other stat/removal errors remain observable in
`ux0:data/vitashell_log.txt`.

## The "promotion committed" assumption

"Once promotion commits, the staged folder becomes `DISPOSABLE`; post-commit
teardown errors must not trigger restoration" rests on the same assumption as
the refresh path: `promoteAppWithStatus` sets `committed = 1` the moment
`scePromoterUtilityPromotePkgWithRif(path, 1)` returns `>= 0`. See
`docs/agent-context/refresh.md` → "The 'committed = 1' assumption" for the
full evidence chain (API contract, `PromoteImport` sync semantics,
independent pkgi/pkgj usage, SceShellSvc IPC architecture) and what it does
and does not prove. Hardware confirmation is the staged Refresh LiveArea
test on `GBVXTST01`.

## Updater handoff

The updater first promotes the small `VSUPDATER` application from disposable
staging and cleans that staging. It then exclusively claims the same path and
extracts the downloaded VitaShell package. On successful extraction,
`VSUPDATER` owns that path; VitaShell must not remove it during thread exit.

## Required failure tests

Fault-inject: occupied staging, archive open/extract/close, generated metadata
write, promotion before and after commit, restoration, cleanup, and updater
handoff. The key assertion is that failed restoration causes zero remove calls
against staging.
