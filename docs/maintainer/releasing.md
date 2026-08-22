# Release Process

No release should be published from the current `release/` directory without
regenerating and verifying every artifact. The committed files predate the
current maintenance work and their version metadata is inconsistent.

## Release blockers

- Physical-hardware verification is incomplete.
- VitaSDK and its packages are not yet pinned reproducibly.
- The application, LiveArea template, changelog, SFO, and `version.bin` do not
  currently share one generated version source.
- The updater and main application artifacts must be built and tested together.

## Required release sequence

1. Start from a clean, reviewed commit on the default branch.
2. Run host tests, a clean VitaSDK build, and `git diff --check`.
3. Complete the hardware matrix using disposable content and record the exact
   commit, devices, and firmware versions.
4. Set one release version everywhere and regenerate all artifacts.
5. Verify the VPK contents, SFO version, LiveArea text, updater payload, and
   four-byte update version independently.
6. Install the candidate VPK on hardware and test a no-update check plus one
   update from the previous supported version.
7. Publish the source tag, VPK, checksum, changelog, and hardware status
   together.

## Update version encoding

`network_update.c` reads a little-endian `uint32_t`. The in-memory value is:

```
(major << 24) | (minor << 16)
```

For source version `2.16`, where the source constants are hexadecimal `0x02`
and `0x10`, the file bytes must therefore be:

```
00 00 10 02
```

Do not update `release/version.bin` ahead of the matching VPK. Older clients
would otherwise be offered stale or mismatched application binaries.

## Update ownership

Development builds check the `bermudi/VitaShell` release directory. This avoids
silently replacing the maintained fork with another fork. Until a verified
release is published there with a higher correctly encoded version, the update
check must remain a no-op.
