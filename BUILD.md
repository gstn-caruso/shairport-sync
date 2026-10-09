# Build and install

## CMake / C++26 migration build

The migration build runs alongside Autotools. The structured buffer and string
utilities use C++ behind C APIs; the other receiver sources remain C. Returned
strings retain their malloc/free ownership contract. CMake enables C++26
and verifies real standard
library support by compiling, linking and running an expected/span/format/jthread probe.
Both build systems require Clang 23.1.3 for C and C++, pinned in `.tool-versions`,
with libstdc++ 15 and C++26 without GNU extensions. They compile, link and run
the same `cmake/cpp26_probe.cpp` before building the receiver.

Service-name formatting and replacement/truncation now live in the internal
`receiver-text` C++ library. `utilities/string_utilities.hpp` accepts borrowed
`string_view` inputs and returns owning `string` values; `appendWithLimit` returns
`expected` with `TruncationError::limitTooSmall` when the suffix/ellipsis cannot
fit. `ServiceNameFormatter` owns hostname and version values and preserves the
ordered `%h`, `%H`, `%v`, `%V` expansion, last-domain removal, first-character
ASCII capitalization and 50-byte UTF-8 truncation boundary. An empty replacement
token is a no-op. C adapters obtain host/build information and contain exceptions
while preserving caller-owned `malloc` results and `NULL` failure results.
The C++ text test links only `receiver-text` in both build systems, without receiver providers;
the legacy C test continues to exercise the public adapters.

Use CMake 4.2 or newer and Ninja. The compiler does not supply the C++ library:
install GCC 15 development headers and libstdc++ 15 on the host first.

Install the asdf toolchain from the repository root:

```sh
sudo apt-get install cmake ninja-build g++-15 g++ python3
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
```

This asdf plugin builds LLVM from source; the first installation is expensive in
CPU, disk space and time. Subsequent builds reuse the installed toolchain.
CI instead downloads the official Linux x86_64 LLVM 23.1.3 binary archive,
verifies its pinned SHA-256, and registers the compiler in asdf's install directory.
It caches only Clang and its resource headers/runtimes (about 372 MB unpacked),
without building LLVM or caching its development libraries and unrelated tools.
The `.zst` archive requires `zstd --decompress --long=30` to decode its 1 GiB window.
PR updates run once; pushes run on `master`, and newer commits cancel obsolete runs.
The toolchain resolves real
compiler paths through `asdf which` from the repository, so build directories
outside the repository retain the selected version.
Install the receiver dependencies listed below, then run:

```sh
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Only Linux with AirPlay 2, PulseAudio, Avahi, FFmpeg and OpenSSL is supported.
Provider switches are rejected. `INSTALL_CONFIG_FILES=OFF` disables installing
the sample configuration. CMake installs the binary, manual and sample at the
same destinations as Autotools with matching prefix and sysconfdir settings.
Generated configuration, plist and Git version files live in the CMake build
directory. Use an out-of-tree build and run `make distclean` first if an earlier
in-tree Autotools build left `config.h` in the source directory.

## Autotools build

Install the pinned compiler and GCC 15 library described above, then the receiver
dependencies. Autotools resolves default compiler paths through `asdf which`
from the source directory, including when configuring outside the repository.
Explicit `CC` or `CXX` overrides must also select Clang 23.1.3.
On Debian/Ubuntu:

```sh
sudo apt-get install autoconf automake g++-15 g++ pkg-config libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
```

Build with the default, mandatory AirPlay 2 Linux PulseAudio stack:

```sh
autoreconf -fi
mkdir -p build/autotools
cd build/autotools
../../configure --sysconfdir=/etc
make -j2
make check
sudo make install
```

An in-tree build also works: run `./configure`, `make`, and `make check` at the repository root. Run `make distclean` before switching from an in-tree configuration to another build directory.

## Migration checks

CI runs separate CMake Release, Debug, ASan+UBSan and TSan builds, plus Autotools,
all with the same pinned compiler and library. Every build runs its contracts
and stages the binary, manual and sample configuration without starting a service.
The sanitizer jobs check instrumentation in receiver C and C++ object files.

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

Installation puts the binary under the chosen prefix and the sample configuration at `shairport-sync.conf.sample` under `sysconfdir`. Existing configuration is preserved. Copy and edit the sample deliberately, removing legacy backend, DSP and metadata-export settings.

From the repository root, `./user-service-install.sh --dry-run` previews user service installation. `./user-service-install.sh` installs and starts the unit for your current user. Its default executable path is `/usr/local/bin/shairport-sync`; edit the unit if you chose another prefix. Do not run the installer as root. NQPTP and Avahi must already be running.

`systemctl --user status shairport-sync` and `journalctl --user -u shairport-sync` show receiver status and logs. The unit does not create a system account or install a system-wide receiver service.
