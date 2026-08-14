# Refresh LiveArea — Details

Filed from root AGENTS.md. Read on demand when touching promotion / staging / work.bin.

## Flow (code is source of truth)
```
Refresh LiveArea → scan ux0 content dirs → refreshNeeded() → stage/move
→ refreshApp() → ensure/restore work.bin → promoteApp()
→ scePromoterUtilityPromotePkgWithRif() → restore or finalize
```

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

## NoNpDrm note

Repo has no explicit NoNpDrm integration and does **not** scan `ux0:nonpdrm/license/` during refresh. Don't add new license search paths without justifying why VitaShell should own it, how conflicts/accounts are handled, and upstream compatibility.

## Error handling

Preserve the first error that explains failure. Useful distinctions: scan failed, staging failed, work.bin recovery failed, promotion failed, restore failed, cleanup failed. Later cleanup error shouldn't erase original unless it signals worse data-integrity risk. Never collapse into generic count.

## References
- `refresh_core.c` / `package_installer.c` host-extracted logic has the testable state machine — see `tests/test_refresh_core.c`.
