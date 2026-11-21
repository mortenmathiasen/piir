# Development

- [Source layout](#source-layout)
- [Building and testing](#building-and-testing)
- [Coverage](#coverage)
- [Continuous integration](#continuous-integration)
- [Adding a remote to irlingo](#adding-a-remote-to-irlingo)
- [Releases](#releases)

## Source layout

| Path | Contents |
|------|----------|
| `src/irlingo.c` | the command line |
| `src/config.c` | loading a remote: symbols, template, features, overrides, variants, toggle |
| `src/checksum.c` | the declarative `checksums` |
| `src/remotes.c` | finding remotes by name |
| `src/lirc.c`, `src/transmitter.c`, `src/irslinger.c` | the `lirc`, `gpiod` and `pigpio` backends |
| `src/state.c` | the toggle state file |
| `src/plugins.c`, `src/hvac_panasonic_plugin.c` | remote plugins |
| `conf/remotes/` | the remotes |
| `tools/lirc2irlingo.py` | the LIRC converter |
| `tools/ir2irlingo.py` | derives remotes from recorded codes |
| `tools/heatpumpir/` | records arduino-heatpumpir codes as captures |
| `tools/irlingo-remotes.in` | the `irlingo-remotes` command |
| `cmake/` | toolchain files, remote selection, packaging |
| `tests/` | unit and command line tests |

## Building and testing

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover the configuration loader, remote lookup, checksums, the plugins, the transmitter backend selected at configure time, the `irlingo` command line, `irlingo-remotes`, the LIRC converter and `ir2irlingo.py`, which must derive `panasonic/hvac_panasonic` again from the codes it sends. Every key of every remote in `conf/remotes/` is checked to produce a code irlingo can send, and every [test vector](remote-format.md#tests) of every remote is checked. Vectors that do not match yet are listed in `tests/known_failures.txt`.

No test transmits IR, so they are safe to run on a Pi. The backends are tested against simulated drivers and libraries: the `ioctl` and `write` calls of the `lirc` backend, libgpiod and pigpio are replaced when linking the tests.

The Python tests need only Python 3; run one directly with:
```
cd tests && PYTHONPATH=../tools python3 -m unittest test_ir2irlingo
```

`build/tests/encode_remote REMOTE.json` prints the codes irlingo builds for feature sets read from standard input, one `NAME=VALUE ...` line each, without transmitting.

When cross compiling, tests run under qemu if you add e.g. `-DCMAKE_CROSSCOMPILING_EMULATOR=$(command -v qemu-aarch64)` (package `qemu-user`).

## Coverage

```
cmake -B build-coverage -DIRLINGO_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage
gcovr -r . build-coverage --exclude 'src/parson\..*' --exclude 'tests/'
```

The converters are measured too when Python's `coverage` module is installed. CI requires 95% line coverage of the C code and of the converters.

## Continuous integration

GitHub Actions runs the tests for all three backends, with AddressSanitizer and UndefinedBehaviorSanitizer, measures coverage, and runs the tests of the `armv6`, `armv7` and `aarch64` builds under qemu. A separate workflow converts the LIRC database again with `tools/lirc2irlingo.py` and fails if `conf/remotes` differs from the result, and compares the converter with LIRC's own `irsimsend`.

## Adding a remote to irlingo

1. Put it in `conf/remotes/<manufacturer>/<name>.json`. Remotes made for irlingo have no `source` setting; files with `source` are replaced by the LIRC conversion check.
2. Add `tests`: codes recorded from the real remote (or the protocol's reference implementation), with the features they were sent for.
3. To ship it in the release packages, add it to `IRLINGO_NATIVE_REMOTES` in `cmake/remotes.cmake`.
4. Run the tests.

## Releases

Update `VERSION` in `CMakeLists.txt`, commit, and push a matching tag:
```
git tag v3.1.0
git push origin v3.1.0
```

GitHub Actions then runs all tests, builds the packages for every Raspberry Pi architecture and the archive of all remotes (`irlingo-remotes-<version>.tar.gz`), and publishes them with checksums as a GitHub release.
