# Build and install

## CMake / C++26 build

Use CMake 4.2 or newer, Ninja, Clang 23.1.3 (pinned in `.tool-versions`)
and libstdc++ 15. CMake checks C++26 without GNU extensions by compiling and
running `cmake/cpp26_probe.cpp`. All receiver production sources, including the
entrypoint and generated plist, compile as C++26. The bundled pairing dependency
remains C behind an explicit linkage boundary; C tests exercise the receiver APIs.
CMake rejects any own production source configured to compile as C.

Install the asdf toolchain from the repository root:

```sh
sudo apt-get install cmake ninja-build g++-15 g++ python3
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
```

The asdf plugin builds LLVM from source. CI uses a checksum-verified official
binary archive; see the workflow in `.github/workflows`.
The toolchain resolves compiler paths through `asdf which` from the repository.
Install the receiver dependencies on Debian/Ubuntu, then build:

```sh
sudo apt-get install pkg-config libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Only Linux with AirPlay 2, PulseAudio, Avahi, FFmpeg and OpenSSL is supported.
Provider switches are rejected. `INSTALL_CONFIG_FILES=OFF` disables installing
the sample configuration. CMake installs the binary under the chosen prefix,
the manual under its `share/man/man1` directory, and the sample under
`CMAKE_INSTALL_SYSCONFDIR`.
Generated configuration, plist and Git version files live in the CMake build
directory. Use an out-of-tree build; remove any stale `config.h` from the source
directory before configuring. To install on the host after checking the staged
files, run `sudo cmake --install build/cmake`.

## Build checks

CI runs separate CMake Release, Debug, ASan+UBSan and TSan builds,
all with the same pinned compiler and library. Every build runs its contracts
and stages the binary, manual and sample configuration without starting a service.
The sanitizer jobs check instrumentation in receiver C++ object files.

The compiler installation must include compiler-rt runtimes for sanitizer builds;
the official binary archive used by CI includes them. Some source-built asdf
installations contain only Clang and headers and must install compiler-rt first.
Reproduce ASan+UBSan separately from TSan:

```sh
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
cmake --build build/asan --parallel 2
ctest --test-dir build/asan --output-on-failure
cmake -S . -B build/tsan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=thread -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'
cmake --build build/tsan --parallel 2
ctest --test-dir build/tsan --output-on-failure
```

The NQPTP fixtures preload an uninstrumented test-only shared library built by
the system `cc`; receiver code remains instrumented with Clang's static sanitizer
runtime. Expected startup failures still reject sanitizer diagnostics, including
leaks. Leak detection remains enabled. A TSan runtime startup failure caused by
host address-space or sandbox restrictions is failed infrastructure validation;
it does not establish the absence of data races.

Install and run NQPTP compatible with this receiver's shared-memory interface (currently SMI version 10) and Avahi as system services. Start a PulseAudio-compatible user session. PipeWire users should run `pipewire-pulse`; the native PipeWire backend is not provided.

Copy `shairport-sync.conf.sample` to `shairport-sync.conf` and edit it before
starting the receiver. When migrating an existing configuration, see
[CONFIGURATION.md](CONFIGURATION.md) for unsupported settings.

From the repository root, `./user-service-install.sh --dry-run` previews user service installation. `./user-service-install.sh` installs and starts the unit for your current user. Its default executable path is `/usr/local/bin/shairport-sync`; edit the unit if you chose another prefix. Do not run the installer as root. NQPTP and Avahi must already be running.

`systemctl --user status shairport-sync` and `journalctl --user -u shairport-sync` show receiver status and logs. The unit does not create a system account or install a system-wide receiver service.
