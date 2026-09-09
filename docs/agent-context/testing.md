# Testing — Details

Filed from root AGENTS.md.

## Host tests first

Extract platform-independent logic and test on host before Vita hardware:
refresh staging state machine, promotion success/failure, restoration, cleanup, temp-dir collisions, directory scanning, cancellation, error priority, work.bin atomicity, PFS mount ordering, path construction, fallback behavior.

Run: `cmake -S . -B build-host -DVITASHELL_HOST_TESTS=ON && cmake --build build-host && ctest --test-dir build-host`

## Fault injection

Systematically fail each FS/API operation (rename, mkdir, remove, open, read, write, close, dir enum, promotion, mount/unmount) — ideally fail the Nth op in sequence and rerun for every N. After each test, verify filesystem invariants.

## Fixtures & integrity

- Test against disposable copy of real `ux0:` layout (app/patch/addcont/license/psm/pspemu). Never use user's only backup.
- For destructive tests, manifest paths/sizes/hashes before execution and compare after. Key question: *could this path eat or alter user content?*

## Vita3K

Useful for VPK launches, menus, path validity, basic regressions, emulated `ux0:` exercise. Not proof of hardware behavior (AppMgr/promoter/NPDRM/PFS may differ).

## Physical Vita (last resort)

Before recommending hardware test: host + fault-injection pass, VitaSDK build clean, `git diff --check` clean, behavior vs diagnostic commits separated, exact commit recorded.

Rules when testing:
1. known exact commit, keep original shell available
2. ensure fallback manager (ONEMenu/VitaDeploy) remains usable
3. backup user data first
4. prefer disposable title before valuable content
5. compare known-good vs problematic title
6. collect logs immediately after reproduction
7. one question per build, no unrelated experiments

Record: `git status; git log -1 --oneline; git diff --check` — working tree clean.

## Build verification

Minimum before claiming done: host tests, VitaSDK build, `git diff --check`. No new warnings. If a check can't run, say so explicitly. For firmware-dependent behavior, say "source-reviewed, host-tested, build-verified, hardware unverified" — not "Fixed."
