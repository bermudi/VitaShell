# AGENTS.md

## Project
Actively maintained VitaShell fork based on `theheroGAC/VitaShell`. The first maintenance effort targets why **Refresh LiveArea** reports `Refreshed 0 items`, why **Open decrypted** fails with `0x80800004`, and clear package-install/update safety bugs — **without risking user data**. Not a rewrite. Small, evidence-backed changes only.

## Current State
- Portable host coverage exists for refresh transactions, PFS mount ordering, package-staging ownership, atomic `work.bin` writes, PSM content-ID parsing, and DLC recovery.
- Host CI lives in `.github/workflows/host-tests.yml`; it does not replace VitaSDK, Vita3K, or hardware verification.
- Package/VPK/FTP/updater staging now uses explicit ownership. Occupied `ux0:data/pkg` is preserved; failed folder restoration must leave the staged copy untouched.
- PSM `content_id` must be exactly 48 bytes with a safe nine-character uppercase-alphanumeric title ID before it is used in a path or promoter call.
- Refresh LiveArea's `Refreshed 0 items` was traced to an unreclaimed staging copy: the promoter copies its source rather than consuming it, but nothing deleted the staging dir after a committed promotion, so the next run tripped the occupied-staging guard and refused every app (self-sustaining the symptom). Fixed in `55e80bc` — `cleanupStaged()` runs only after the promoter commits (success or committed-with-error), never on restore paths; covered by host tests (21 refresh + 12 pfs). Hardware still unverified.
- Update checks belong to `bermudi/VitaShell`, not another fork. Do not publish/update `release/` until its VPK and all version metadata are regenerated together.
- Current maintenance changes are source-reviewed, host-tested, and VitaSDK build-verified; firmware-dependent behavior remains hardware-unverified.
- Hardware test is staged on a throwaway SD: ONEMenu 3.22 copied byte-identical from the live card as fallback, disposable title `GBVXTST01` (GBVitaEX clone with patched TITLE_ID) staged at `ux0:data/vitashell-test/GBVXTST01-ready`, test VPK `VitaShell-test-b5e43ee.vpk` + 2.04 rescue VPK in `ux0:data/vitashell-test/`, `TEST-PLAN.txt` on card; host backup under `~/VitaShell-hardware-test-*`. Run one Refresh LiveArea, capture `ux0:data/vitashell_log.txt`; expect `Refreshed 1 item` and an absent `ux0:temp/app`.

## Stack
| Area | Tooling |
|------|---------|
| Language | C (VitaSDK), some C++ in kernel modules |
| Build | CMake + VitaSDK toolchain; host tests via `VITASHELL_HOST_TESTS=ON` |
| Runtime | PS Vita firmware (Promoter, AppMgr, PFS, NPDRM) |
| Emulation | Vita3K for light integration checks — not proof of hardware behavior |

## Architecture
- File manager / package installer / updater / PFS helper with kernel+user modules in `modules/`.
- Three hot paths:
  - **Refresh LiveArea:** scan `ux0:` → stage → ensure `work.bin` → promote via `scePromoterUtilityPromotePkgWithRif` → restore/cleanup.
  - **Package install/update:** exclusively claim `ux0:data/pkg` → extract or move source → generate metadata → promote/handoff → restore or guarded cleanup.
  - **Open decrypted:** try private mount IDs → fallback mount.
- Code is source of truth for file layout. See the linked context documents for detailed flows.

## Constraints & Red Lines
- **User data is sacred.** Anything touching `ux0:app/addcont/patch/psm/pspemu/license`, `work.bin`, promotion, or PFS mounts is potentially destructive. Every FS/API op can fail — code for it. Never lose/overwrite/move/partially restore user content.
- **Staging invariants** — apply to refresh, package installation, FTP promotion, and updater paths (details in `docs/agent-context/refresh.md` and `package-install.md`):
  - Never destroy an occupied staging dir to make progress.
  - Failed staging → do not call promoter.
  - Failed promotion (pre-commit) → restore original when possible.
  - Failed restore → hard stop; preserve staging because it may hold the only copy.
  - Post-commit cleanup failure ≠ promotion failure — don't roll back a committed app.
  - Remove staging only when the current operation established that it is disposable.
  - Cancellation is not an error.
  - Scan/read errors must surface — never silently become `Refreshed 0 items`.
- **Secrets never enter context.** Don't echo/read/print keys/tokens; reference via env/process only. If you see a value, stop and warn to rotate.
- **Recoverable beats gone.** Prefer `trash` over `rm`; no force-push, no dropping data/branches/volumes/dbs without explicit confirmation.

## Stable Reference Facts
Keep these — agent can't infer them from code alone:
- Promotion-ready app may carry `sce_sys/package/work.bin` (512-byte RIF). VitaShell may preserve non-zero, remove all-zero placeholder, or reconstruct missing from `ux0:license/license.db`. Writes must be atomic (temp file → verify 512 bytes → replace); no truncated RIFs.
- **Open decrypted** tries mount IDs in exact order `0x6E → 0x12E → 0x12F → 0x3ED` via `shellUserMountById()`, then fallback `sceAppMgrGameDataMount()`. Don't reorder/add/remove casually. Preserve stop-on-success and final error semantics.
- `0x80800004` is the observed fallback mount failure — don't assign symbolic meaning without evidence.
- Dev logs go to `ux0:data/vitashell_log.txt` as `Operation(path=..., id=...) returned 0x...` (no binary dumps).
- This repo does **not** scan `ux0:nonpdrm/license/` during refresh; dormant `ksceNpDrmGetRifVitaKey()` code is disabled — treat enabling as experimental (separate commit, document hypothesis, keep fallback).
- `ux0:data/pkg` is shared by folder installs, VPK extraction, FTP promotion, and self-update. Its existence is a collision, not permission to recursively delete it.
- `release/` contains tracked generated artifacts and may be stale. Updater version bytes are little-endian `(major << 24) | (minor << 16)`; for source constants `0x02.0x10`, bytes are `00 00 10 02`.
- **Reference repo lineage** (verified via `git fetch --unshallow` + `git merge-base`; full detail + reproduction in `docs/agent-context/reference-lineage.md`): the `refs/` clones are not three forks. `RealYoti/VitaShell` is an **older snapshot of `theheroGAC/VitaShell`** (merge-base = RealYoti tip `c7b597d`; theheroGAC has 123 commits RealYoti lacks, RealYoti has 0 theheroGAC lacks). `henkaku/VitaShell` is an **unrelated 2016–2017 lineage** (merge-base with the others = NONE) that predates `refresh.c`/`pfs.c`/`rif.c` — ignore it for the Refresh/Open-decrypted bugs. This repo is a fork of `theheroGAC/VitaShell` (`upstream` remote = theheroGAC; 8 commits ahead). The `refresh`/`pfs`/`rif` code was imported from TheOfficialFloW (`f07be49`) then extended (`f61fda6` "Add PSP Refresh"); `pfs.c`/`rif.c` are **byte-identical** across theheroGAC/RealYoti, so the Open-decrypted mount order and `0x80800004` are shared, not a fork split.

## Conventions
- **Evidence over assumptions.** Distinguish *demonstrated by source* vs *supported inference (firmware behavior inferred from APIs)* vs *unknown*. Don't turn guesses into facts.
- **Error handling:** preserve the first meaningful error; don't collapse to a generic count. Cleanup errors don't erase the original unless more severe.
- **Diagnostics vs behavior:** log/API-path fixes and behavior changes belong in separate commits. Experimental (mount IDs, klicensee, PFS semantics) gets its own commit.
- **Smallest fix that could work.** No opportunistic rewrites, UI churn, or new deps without concrete need. Match patterns of nearby code.
- **Repository maintenance:** keep issue/PR templates and CI aligned with actual checks. Do not automate releases or claim reproducible Vita builds until VitaSDK and its packages are immutably pinned.
- **Branch safety:** inspect local/remote topology before integrating. No force-push. Maintenance work may be ahead of the public branches; never assume an unpushed commit is on GitHub.

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

A public release additionally requires consistent source/SFO/LiveArea/update metadata, regenerated artifacts, checksums, a clean reviewed commit, and recorded physical-hardware results. See `docs/maintainer/releasing.md`.

## Further Context
- `docs/agent-context/refresh.md` — staging state machine, work.bin rules, NoNpDrm note
- `docs/agent-context/package-install.md` — package staging ownership, restore/cleanup rules, updater handoff
- `docs/agent-context/pfs.md` — mount order, diagnostics, klicensee warning
- `docs/agent-context/testing.md` — fixtures, fault injection, hardware rules
- `docs/agent-context/reference-lineage.md` — fork lineage: RealYoti = older theheroGAC snapshot, henkaku unrelated, dev = theheroGAC fork; refresh/pfs/rif origin
- `docs/maintainer/releasing.md` — release blockers, artifact checks, updater version encoding
