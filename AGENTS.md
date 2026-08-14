# AGENTS.md

## Project
VitaShell fork to diagnose why **Refresh LiveArea** reports `Refreshed 0 items` and why **Open decrypted** fails with `0x80800004`, then fix clear bugs with better diagnostics — **without risking user data**. Not a rewrite. Small, evidence-backed changes only.

## Stack
| Area | Tooling |
|------|---------|
| Language | C (VitaSDK), some C++ in kernel modules |
| Build | CMake + VitaSDK toolchain; host tests via `VITASHELL_HOST_TESTS=ON` |
| Runtime | PS Vita firmware (Promoter, AppMgr, PFS, NPDRM) |
| Emulation | Vita3K for light integration checks — not proof of hardware behavior |

## Architecture
- File manager / package installer / PFS helper with kernel+user modules in `modules/`.
- Two hot paths: **Refresh LiveArea** (scan `ux0:` → stage → ensure `work.bin` → promote via `scePromoterUtilityPromotePkgWithRif` → restore/cleanup) and **Open decrypted** (try private mount IDs → fallback mount).
- Code is source of truth for file layout. See `docs/agent-context/refresh.md` and `docs/agent-context/pfs.md` for detailed flows.

## Constraints & Red Lines
- **User data is sacred.** Anything touching `ux0:app/addcont/patch/psm/pspemu/license`, `work.bin`, promotion, or PFS mounts is potentially destructive. Every FS/API op can fail — code for it. Never lose/overwrite/move/partially restore user content.
- **Refresh invariants** — must hold on any failure (details in `docs/agent-context/refresh.md`):
  - Never destroy an occupied staging dir to make progress.
  - Failed staging → do not call promoter.
  - Failed promotion (pre-commit) → restore original when possible.
  - Failed restore → hard stop for that staging location.
  - Post-commit cleanup failure ≠ promotion failure — don't roll back a committed app.
  - Cancellation is not an error.
  - Scan errors must surface — never silently become `Refreshed 0 items`.
- **Secrets never enter context.** Don't echo/read/print keys/tokens; reference via env/process only. If you see a value, stop and warn to rotate.
- **Recoverable beats gone.** Prefer `trash` over `rm`; no force-push, no dropping data/branches/volumes/dbs without explicit confirmation.

## Stable Reference Facts
Keep these — agent can't infer them from code alone:
- Promotion-ready app may carry `sce_sys/package/work.bin` (512-byte RIF). VitaShell may preserve non-zero, remove all-zero placeholder, or reconstruct missing from `ux0:license/license.db`. Writes must be atomic (temp file → verify 512 bytes → replace); no truncated RIFs.
- **Open decrypted** tries mount IDs in exact order `0x6E → 0x12E → 0x12F → 0x3ED` via `shellUserMountById()`, then fallback `sceAppMgrGameDataMount()`. Don't reorder/add/remove casually. Preserve stop-on-success and final error semantics.
- `0x80800004` is the observed fallback mount failure — don't assign symbolic meaning without evidence.
- Dev logs go to `ux0:data/vitashell_log.txt` as `Operation(path=..., id=...) returned 0x...` (no binary dumps).
- This repo does **not** scan `ux0:nonpdrm/license/` during refresh; dormant `ksceNpDrmGetRifVitaKey()` code is disabled — treat enabling as experimental (separate commit, document hypothesis, keep fallback).

## Conventions
- **Evidence over assumptions.** Distinguish *demonstrated by source* vs *supported inference (firmware behavior inferred from APIs)* vs *unknown*. Don't turn guesses into facts.
- **Error handling:** preserve the first meaningful error; don't collapse to a generic count. Cleanup errors don't erase the original unless more severe.
- **Diagnostics vs behavior:** log/API-path fixes and behavior changes belong in separate commits. Experimental (mount IDs, klicensee, PFS semantics) gets its own commit.
- **Smallest fix that could work.** No opportunistic rewrites, UI churn, or new deps without concrete need. Match patterns of nearby code.

## Workflow
```bash
# host tests (platform-independent logic first)
cmake -S . -B build-host -DVITASHELL_HOST_TESTS=ON
cmake --build build-host && ctest --test-dir build-host

# Vita build (requires $VITASDK)
cmake -S . -B build && cmake --build build

# hygiene before hardware test
git status; git log -1 --oneline; git diff --check  # working tree clean, no new warnings
```
- Fault-inject every FS op (rename/mkdir/remove/open/read/write/close/enum/promote/mount) — fail Nth op and rerun. Verify with path/size/hash manifest before/after on a **disposable copy** of a real `ux0:` tree. Never the user's only backup.
- Vita3K for basic regressions only. Hardware is last — one question per build, keep original shell + fallback manager (ONEMenu/VitaDeploy), backup first, use disposable title, compare good vs failing title. See `docs/agent-context/testing.md`.

## Quality Bar
Done means you can state: what bug/failure mode it addresses, what source evidence justified it, what invariants it preserves, what tests (including fault-injection) cover it, and whether Vita3K/hardware verification is still needed. For firmware-dependent behavior, prefer: *"Source-reviewed, host-tested, build-verified. Hardware unverified."* over *"Fixed."*

## Further Context
- `docs/agent-context/refresh.md` — staging state machine, work.bin rules, NoNpDrm note
- `docs/agent-context/pfs.md` — mount order, diagnostics, klicensee warning
- `docs/agent-context/testing.md` — fixtures, fault injection, hardware rules
