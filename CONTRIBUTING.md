# Contributing to NullNoise

Thanks for your interest in improving NullNoise. Small, focused contributions
are easiest to review. By participating, you agree to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Before you start

- Search existing issues and pull requests before opening a new one.
- For substantial changes, open an issue first to discuss the approach.
- Never commit recordings, personal audio, generated APKs, signing keys, or
  other credentials. Do not attach recordings to public issues or pull requests.
- Keep changes to the pinned `third_party` submodules deliberate and explain
  why a dependency update is needed.

## Development and validation

See the [developer guide](docs/development/README.md) for local environment
setup, builds, tests, and release workflow details. Android builds and
emulator tests run in GitHub Actions; a local Android SDK or NDK setup is
optional.

If you have a host C compiler and Python 3, run the build-independent test
suite from the repository root:

```sh
bash scripts/test-local.sh
```

This checks shell syntax, exercises permission-dialog XML parsing, and
compiles and runs the portable WAV-header tests. It does not build an APK or
require Android tooling. The same checks run before Android SDK setup in
GitHub Actions.

If you add portable audio or file-format logic, keep it testable without
Android APIs where practical and include focused tests or reproducible
validation steps.

## Pull requests

1. Create a branch for your change.
2. Make the smallest coherent change and update related documentation.
3. Run the applicable lightweight checks above.
4. Open a pull request using the project template. Describe user-visible
   behavior and note any device or emulator testing.
5. Keep the pull request focused; do not bundle unrelated formatting or
   dependency changes.

Please redact personal information from logs. Describe audio results in words
instead of sharing private recordings.
