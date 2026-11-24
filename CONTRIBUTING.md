# Contributing to Atlas

Thanks for contributing!

## Before you start

Read:
- [AGENTS.md](AGENTS.md): the project's rules, toolchain and verified baseline (it applies to people as much as to agents);
- [docs/coding_convention.md](docs/coding_convention.md): naming and formatting. Do not reformat `.ui` files;
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): where things are;
- [docs/EXTENDING.md](docs/EXTENDING.md): recipes for adding an instruction, processor model, replacement policy, Cache Lab preset, theme, ribbon action or verification checker.

## How to work

- **One feature per branch.** Keep each logical change on its own topic branch.
- **One commit per working change.** Every commit builds and passes the tests, and its message says what changed and why.
- **Do not change simulator core logic in UI work** (the RVSS and five-stage models, the cache hierarchy, assembler semantics) unless that is the task.
- **Ask before adding a dependency.** Atlas needs Qt 6 and CMake, vendors Lua, and fetches GoogleTest for the tests. Anything new is a decision for the maintainer.
- **New code is warning-free** under `-Wall -Wextra`.
- **Naming.** Use `Atlas` for namespaces, targets and user-facing text. The upstream project's name appears only in `LICENSE.md`, `NOTICE.md`, the changes document `NOTICE.md` links to, and `docs/audit/**`. Its filename may also be written in the submission scripts and the docs index (AGENTS.md, rule 6).
- **No destructive git commands** (`push --force`, `reset --hard`, `clean`) on shared work.
- **A test that has to change is a decision, not a side effect.** Say which test and why in the commit message.

## Build and test

```bash
cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
# or
./scripts/test.sh                 # prints: total / passed / failed
# or, with the Linux presets
cmake --preset linux-test && cmake --build --preset linux-test && ctest --preset linux-test
```

**Run the suite serially.** Never use `ctest -j`: the GUI tests share `test/vm_state/temp.asm` and fail at random in parallel (known_issues 150, [docs/TESTING.md](docs/TESTING.md)). Run ctest through `--test-dir` (or the presets), not the test binary directly: tests open `../examples/...`, and CTest gives them the right working directory.

## Independent verification

`scripts/run_verification.sh` builds the probes in `build-verify` and runs every independent checker in `verification/`:
- the ISA, hazard/cycle, `li` and L1 cache sweeps;
- the assembler corpus;
- the committed-table check;
- the negative controls.

It compares their summaries with `verification/expected_summary.txt`. It takes about 17 minutes.

```bash
scripts/run_verification.sh            # build the probes and run everything
scripts/run_verification.sh --no-build # reuse build-verify
scripts/run_verification.sh --record   # after an INTENDED change: accept the new summary
```

Run it after any change to the assembler, the processor models or the caches. Use `--record` only after reading `verification/out/summary.diff` and being sure the change is intended. Never use it to make a failure go away. See [verification/README.md](verification/README.md) for what each checker proves, and [verification/RESULTS.md](verification/RESULTS.md) for the last full run.

## Cutting a release

The packaging workflow (`.github/workflows/release.yml`) runs on every push to `final/package` or `final/submission`, and from the Actions tab (workflow_dispatch). Each run:
- builds the Linux AppImage and tar.gz on ubuntu-22.04 and the Windows zip on windows-latest;
- smoke-checks them with `scripts/check_package.sh` / `scripts/check_package.ps1`;
- uploads them as artifacts.

To publish a release:
1. Check that the last run on the commit you want to release passed (`gh run list --workflow release.yml`).
2. Tag that commit and push the tag:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. The tag's run builds and checks the packages again, then publishes them to a GitHub Release named after the tag.

The version in the file names is `atlasVersion()`: the CMake project version plus the commit hash, with `+` written as `-`. For example, `Atlas-1.0.0-19c549e-x86_64.AppImage`. Bump `project(... VERSION ...)` in `CMakeLists.txt` before tagging a new version.

## Known issues

Found a defect you are not fixing? Add a numbered entry to [docs/audit/known_issues.md](docs/audit/known_issues.md) with the file and line, the evidence, and why it is not fixed. [docs/FUTURE_WORK.md](docs/FUTURE_WORK.md) groups the open ones by area.
