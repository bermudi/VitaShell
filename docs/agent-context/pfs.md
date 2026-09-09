# PFS / Open Decrypted — Details

Filed from root AGENTS.md. Read when touching mount or license code.

## Current mount flow

Tries private IDs via `shellUserMountById()` in **exact order**, stop on success, then fallback:

```
0x6E → 0x12E → 0x12F → 0x3ED → sceAppMgrGameDataMount()
```

Do not reorder/add/remove IDs casually. Preserve attempt order, stop-on-success, fallback, returned mount point, and final error semantics.

Known IDs context:
- `0x6E: ux0:appmeta` — `0x12E/0x12F: trophy` — `0x3ED: ux0:user/00/savedata`
- Others like `0x3E8/0x3E9/0x3EA` (app/patch/addcont) exist but are not part of this fallback chain.

## Diagnostics

Each attempt must be independently observable. Log `source path, mount ID, returned result, successful mount point, fallback result` — e.g.:

```
Operation(path=..., id=...) returned 0xXXXXXXXX
```

Don't discard intermediate errors in dev builds, but preserve externally expected final error unless deliberately changing it. Diagnostics must not alter mount behavior.

Observed failure: final fallback can return `0x80800004`. Do not assign symbolic meaning without evidence; compare all intermediate results against a known-good title.

## klicensee / RIF key

Dormant commented code exists that derives a key via `ksceNpDrmGetRifVitaKey()`. It entered upstream already disabled and has not been shown operating in normal `Open decrypted` flow.

Treat enabling it as experimental reverse-engineering:
1. separate commit
2. document hypothesis
3. keep existing behavior as fallback
4. don't call it a fix until hardware-verified

## Logging

Dev builds log to `ux0:data/vitashell_log.txt`. Include enough context to compare good vs failing title. No large binary dumps or sensitive data.
