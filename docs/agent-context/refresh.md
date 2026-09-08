# Refresh LiveArea — Details

Filed from root AGENTS.md. Read on demand when touching promotion / staging / work.bin.

## Flow (code is source of truth)
```
Refresh LiveArea → scan ux0 content dirs → refreshNeeded() → stage/move
→ refreshApp() → ensure/restore work.bin → promoteApp()
→ scePromoterUtilityPromotePkgWithRif() → restore or finalize
→ cleanupStaged(): delete the staging copy once the promoter committed
```

The promoter **copies** its source rather than consuming it (every caller
wipes its staging dir first — see `installPackage` and upstream VitaShell),
so the staged directory must be deleted after a committed promotion.
Cleanup runs only on `REFRESH_TRANSACTION_PROMOTED` and
`REFRESH_TRANSACTION_COMMITTED_WITH_ERROR`. Never on restore paths: after a
successful restore the staging path holds the moved-back original, and after
a failed restore it holds the only copy. Without cleanup, the leftover blocks
the next staging attempt at the same path (apps/patches share one temp dir)
and every later refresh reports occupied-staging errors.

Touching this path, watch: staging collisions, rename failures, promotion/restoration/cleanup failures, cancellation, scan errors, partial writes, error priority, committed vs teardown failure.

## Safety invariants (must hold on any failure)

1. **Never destroy occupied staging dirs** — if temp location has data, fail safely, don't delete/overwrite.
2. **Failed staging → no promotion** — don't call promoter on invalid path.
3. **Failed promotion (pre-commit) → preserve original** — restore original app when possible.
4. **Failed restoration → hard stop for that staging location** — don't reuse/clear it; assume it holds user data.
5. **Post-commit cleanup failure ≠ promotion failure** — don't restore old dir over successfully promoted app.
6. **Cancellation is not an error** — keep separate.
7. **Scan errors must be observable** — never turn failed `ux0:app` open/read into `Refreshed 0 items`.

## work.bin

- Expected at `sce_sys/package/work.bin`, 512-byte RIF when present.
- Logic may: (1) preserve non-zero existing, (2) remove all-zero homebrew placeholder, (3) reconstruct missing from `ux0:license/license.db` when possible.
- Safety: treat as user metadata; atomic write via temp file → verify full 512 bytes → replace; remove failed temp file; propagate errors; never leave truncated RIF. If RIF can't be recovered, surface the condition.

## The "committed = 1" assumption

Every "don't restore after commit" decision in `refreshPromoteStaged`
(`refresh_core.c`) and `package_installer.c` flows from one flag:
`promoteAppWithStatus` / `promoteCmaWithStatus` set `committed = 1` the
moment `scePromoterUtilityPromotePkgWithRif` / `scePromoterUtilityPromoteImport`
returns `>= 0`, before teardown (`Exit`, `UnloadModuleInternal`, `unloadScePaf`).
If the promoter could return success while the install was still pending in
SceShell, a later teardown failure would be misclassified as
"committed-with-error" and the original would not be restored — even though
nothing was actually installed.

Evidence that sync=1 means "installed by the time the call returns":

- **API contract.** The vitasdk header documents the `sync` parameter of
  `scePromoterUtilityPromotePkgWithRif` / `scePromoterUtilityPromotePkg` as
  "pass 0 for asynchronous, 1 for synchronous." We pass 1. There is no
  separate `GetState`/`GetResult` dance for sync mode — those exist for the
  async path (and the `*ASync` variants added in FW 3.200).
- **`PromoteImport` is inherently synchronous.** It has no sync parameter; a
  distinct `scePromoterUtilityPromoteImportASync` was added in FW 3.200. So
  the CMA path's `committed = 1` is on firmer ground than the pkg path's.
- **Independent production usage.** pkgi/pkgj (the most widely used on-device
  package downloader) calls `PromotePkgWithRif(path, 1)`, treats `== 0` as
  fully installed, and never polls `GetState`/`GetResult`. This has held
  across firmware 3.60–3.74 on many devices for years.
- **Architecture (henkaku wiki).** ScePromoterUtil is a thin IPC wrapper
  around SceShellSvc; SceShell performs the actual file moves, app.db update,
  and LiveArea bubble. In sync mode the usermode call blocks on the IPC reply.
  The existence of `promoter_heartbeat` and the `*ASync` variants is
  consistent with: the underlying SceShell operation is async, but sync=1
  blocks the caller until it completes.

What this does **not** prove: that SceShell has fully flushed app.db to disk
by the time the sync IPC reply arrives. That is firmware-internal and cannot
be settled by source alone. The evidence moves the assumption from
"unverified" to "well-supported by API contract + independent production
usage + RE knowledge," but the final confirmation is the staged hardware
test: a Refresh LiveArea on `GBVXTST01` that yields `Refreshed 1 item` and a
visible bubble means the sync promotion committed.

Do not add a `scePromoterUtilityCheckExist` call to the promote path to
"verify" the commit — it would add a new failure mode to a safety-critical
path for no real gain. If a diagnostic is ever wanted, log it only, never
branch on it.

## NoNpDrm note

Repo has no explicit NoNpDrm integration and does **not** scan `ux0:nonpdrm/license/` during refresh. Don't add new license search paths without justifying why VitaShell should own it, how conflicts/accounts are handled, and upstream compatibility.

## Error handling

Preserve the first error that explains failure. Useful distinctions: scan failed, staging failed, work.bin recovery failed, promotion failed, restore failed, cleanup failed. Later cleanup error shouldn't erase original unless it signals worse data-integrity risk. Never collapse into generic count.

## References
- `refresh_core.c` / `package_installer.c` host-extracted logic has the testable state machine — see `tests/test_refresh_core.c`.
