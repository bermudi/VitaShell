# Reference repo lineage — theheroGAC / RealYoti / henkaku / dev

Filed from root AGENTS.md. Read on demand when comparing forks, tracing the
origin of the `refresh`/`pfs`/`rif` code, or deciding which reference repo is
relevant to a bug.

## TL;DR
- The three "forks" are **not three independent forks**. `RealYoti` is an
  **older snapshot of `theheroGAC`**; `henkaku` is an **unrelated, older
  lineage**; the dev repo (`bermudi/VitaShell`) is a **fork of `theheroGAC`**,
  8 commits ahead.
- For the Refresh LiveArea / Open-decrypted bugs: `henkaku` is irrelevant
  (predates the code). Compare against `theheroGAC` (== dev's base). The
  open-decrypted mount logic in `pfs.c`/`rif.c` is **byte-identical** across
  hero/yoti, so `0x80800004` behavior is shared, not a fork divergence.
- The two `refresh.c` changes that differ between hero and yoti are **already in
  the dev base** (inherited from `theheroGAC`) — so they are not the open bug
  unless `0777` itself is wrong (unproven).

## How this was established (reproducible)
1. Cloned all three with `--depth 1` into `refs/` (shallow → no shared history).
2. `git fetch --unshallow` each → full history (hero 142, yoti 19, henkaku 369
   commits).
3. Built a scratch combined repo `refs/_map` with all three + dev as remotes;
   used `git merge-base` and `git rev-list --count A --not B`.
4. `git diff --no-index` for working-tree file maps; `sha256sum` to confirm
   `pfs.c`/`rif.c`/`refresh.c` identity across forks.

## Lineage
```
78be994  (root: "Initial commit" 2022-06-30, import of TheOfficialFloW/VitaShell)
  └─ ... ─ c7b597d  RealYoti tip            (2024-09-11 "Fix build helpers")  ← RealYoti stops
               └─ ... 123 commits ... ─ 4695f35  theheroGAC tip (2025-11-29 "Merge PR #11 ...")
                                    └─ ... 8 commits ... ─ 55e80bc  dev tip (bermudi, "Clean up staging...")

henkaku: SEPARATE lineage — root fd8e238 (2016-08-06 "Update v0.7") … tip 4fa583c (2017-05-12).
         merge-base(henkaku, *) = NONE.
```

## Per-repo facts (demonstrated via git)
| Repo | Remote | Commits | Root | Tip |
|------|--------|---------|------|-----|
| theheroGAC | github.com/theheroGAC/VitaShell | 142 | 78be994 (2022-06-30) | 4695f35 (2025-11-29) |
| RealYoti | github.com/RealYoti/VitaShell | 19 | 78be994 (2022-06-30) | c7b597d (2024-09-11) |
| henkaku | github.com/henkaku/VitaShell | 369 | fd8e238 (2016-08-06) | 4fa583c (2017-05-12) |
| dev (bermudi) | origin bermudi/VitaShell, upstream theheroGAC | 8 ahead of hero | — | 55e80bc |

## Conclusions (labeled: demonstrated vs inference)
- **[DEMONSTRATED]** `merge-base(theheroGAC, RealYoti) = c7b597d` (== RealYoti
  tip). `theheroGAC` has 123 commits not in `RealYoti`; `RealYoti` has 0 not in
  `theheroGAC`. ⇒ RealYoti is a strict prefix / older snapshot of theheroGAC,
  not a divergent fork.
- **[DEMONSTRATED]** `merge-base(henkaku, any) = NONE`. henkaku's history
  (2016–2017) shares no commits with hero/yoti (root is a 2022 import of
  TheOfficialFloW). henkaku predates `refresh.c`/`pfs.c`/`rif.c`; irrelevant to
  these bugs.
- **[DEMONSTRATED]** `merge-base(theheroGAC, dev) = 4695f35` (== theheroGAC
  tip). dev = theheroGAC + 8 bermudi commits. dev `refresh.c` differs from
  theheroGAC by **507 lines**, `package_installer.c` by **127** (bermudi's
  rework of the staging/promote path).
- **[DEMONSTRATED]** Origin of refresh/pfs/rif: imported at `f07be49`
  ("Import https://github.com/TheOfficialFloW/VitaShell/commit/a7a9d5d…"), then
  `f61fda6` "Add PSP Refresh" (2023-07-01). Shared by hero & yoti.
- **[DEMONSTRATED]** `pfs.c`, `pfs.h`, `rif.c`, `rif.h` are **byte-identical**
  (sha256) between theheroGAC and RealYoti ⇒ Open-decrypted mount order
  `0x6E→0x12E→0x12F→0x3ED` (per root AGENTS.md) is shared; `0x80800004` is not a
  fork divergence.
- **[INFERENCE]** The two `refresh.c` changes in the hero-vs-yoti delta are
  already present in dev (inherited), so they are not the *current* open bug —
  unless `0777` itself is incorrect (unproven):
  - `605b523` "fix warning": `long unk0;` → `char *unk0 = NULL;` in
    `refreshNeeded()` (eboot-sig verify that decides if an app needs refresh).
  - `4efbc7d` "v2.15": staging temp dirs `sceIoMkdir(..., 0006)` → `0777`
    ("proper USB visibility permissions").

## theheroGAC-vs-RealYoti delta commits touching the refresh/promote path
These 10 commits are in theheroGAC but not RealYoti (i.e. post-`c7b597d`). All
are inherited by dev:
`4efbc7d` v2.15 · `e73335a` v2.12 · `1589fbf` v2.11 · `de4de25` update v2.09 ·
`16a8bc4` update v2.09 · `0aea473` another bug fix · `800c1e0` update v2.08 ·
`605b523` fix warning · `99753ef` fix · `03ea7b0` update package_installer.c

Notable promote-path suspect:
- `03ea7b0` "update package_installer.c": rewrote `loadScePaf`/`unloadScePaf` to
  pass `NULL` instead of result buffers into
  `sceSysmoduleLoadModuleInternalWithArg`/`Unload…` (the PAF sysmodule promotion
  depends on). Worth vetting — if the firmware writes through that pointer,
  promotion could fail silently → "Refreshed 0 items".

## Reproduce / re-verify
```bash
cd /home/daniel/build/VitaShell/refs
# unshallow all three (one-time)
git -C theheroGAC-VitaShell fetch --unshallow
git -C RealYoti-VitaShell     fetch --unshallow
git -C henkaku-VitaShell      fetch --unshallow

# in a combined repo (refs/_map) with hero/yoti/henkaku/dev remotes:
git merge-base theheroGAC/master RealYoti/master   # -> c7b597d
git merge-base henkaku/master   theheroGAC/master   # -> NONE
git rev-list --count theheroGAC/master --not RealYoti/master   # -> 123

# identity of pfs/rif across forks
sha256sum theheroGAC-VitaShell/pfs.c RealYoti-VitaShell/pfs.c
sha256sum theheroGAC-VitaShell/rif.c RealYoti-VitaShell/rif.c
```

## Scratch / disposal
- `refs/_map`: local combined repo (all four refs) for cross-repo merge-base /
  diff. `trash refs/_map` when no longer needed.
- `refs/theheroGAC-VitaShell`, `refs/RealYoti-VitaShell`,
  `refs/henkaku-VitaShell`: shallow-then-unshallowed local clones; not canonical
  (canonical = GitHub remotes above). `refs/` is gitignored in the dev repo.
