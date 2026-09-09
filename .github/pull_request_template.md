## What changed

<!-- Describe the demonstrated failure and the smallest change that addresses it. -->

## Safety

<!-- Explain effects on storage, staging, promotion, mounts, and cleanup. -->

- [ ] No user-data path is deleted or overwritten without established ownership.
- [ ] Failures remain observable through an error or diagnostic log.
- [ ] No secrets, license data, RIF contents, or private fixtures are included.

## Verification

- [ ] Host tests pass.
- [ ] VitaSDK build passes.
- [ ] `git diff --check` passes.
- [ ] New failure paths have tests.

Hardware status:

<!-- Source-reviewed / host-tested / build-verified / Vita3K-tested / hardware-tested -->

Exact hardware-test commit, device, and firmware:

<!-- Write "not tested" when physical hardware was not used. -->
