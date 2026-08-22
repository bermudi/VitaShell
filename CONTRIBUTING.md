# Contributing

VitaShell can rename, delete, install, mount, and promote content on real Vita
storage. Small, testable changes are preferred over broad rewrites.

## Before opening a change

1. Search existing issues and pull requests.
2. Keep diagnostic changes, behavior changes, and firmware experiments in
   separate commits.
3. Add host tests for platform-independent logic.
4. Do not test destructive paths against the only copy of user data.

Never include license files, RIF contents, account data, keys, tokens, or other
secrets in an issue, log, fixture, commit, or pull request.

## Required checks

Run the host suite:

```sh
cmake -S . -B build-host -DVITASHELL_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

When VitaSDK is available, also run:

```sh
cmake -S . -B build
cmake --build build
git diff --check
```

Report checks that could not be run. Do not describe firmware-dependent work
as fixed until it has been tested on physical hardware.

## Hardware testing

Use an exact commit, keep a fallback file manager installed, back up first, and
start with disposable content. A hardware test report should include:

- Vita or PS TV model
- firmware version
- storage and mount configuration
- exact commit
- operation performed
- expected and observed result
- sanitized diagnostic log

See the detailed [testing notes](docs/agent-context/testing.md).

## Pull requests

Explain:

- the demonstrated failure being addressed
- why the change is safe
- which failure paths were tested
- whether Vita3K or physical hardware was used
- any remaining uncertainty

Avoid generated release binaries in ordinary pull requests. Release artifacts
belong in a dedicated, reviewed release commit.
