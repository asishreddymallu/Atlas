# Atlas RISC-V Simulator

Atlas is a RISC-V computer architecture teaching platform: an assembly editor,
assembler and simulator with a single-cycle model and four five-stage pipeline
variants (with and without hazard detection and forwarding), a live processor
diagram, a pipeline timing grid, configurable caches, memory and register
views, and a profiler. It supports the RV64 I, M, F and D extensions.

## Build

Requirements: CMake 3.19+, Ninja, a C++20 compiler and Qt 6.9 or newer
(Core, Widgets, Network, Concurrent).

```bash
cmake -S . -B build -G Ninja && cmake --build build
# or
./scripts/build.sh
```

This produces `build/Atlas` (GUI) and `build/atlas-cli` (command line).

## Run

```bash
./build/Atlas                         # GUI
./build/atlas-cli program.s --vm rv5-h-f   # headless batch run; see docs/CLI.md
```

## Test

```bash
cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure   # serially; see docs/TESTING.md
# or
./scripts/test.sh
```

## Suggest reordering

Editor ribbon > Schedule > Suggest reordering proposes a faster order for
independent instructions, shown only when a measured run on the selected
five-stage processor proves it gives the same result in fewer cycles. See
[docs/scheduler.md](docs/scheduler.md).

Reordering scheduler by Nandan Nihal (CS25BTECH11014).

## Licence and attribution

MIT; see [LICENSE.md](LICENSE.md). Atlas is a fork of an earlier project; see
[NOTICE.md](NOTICE.md).
