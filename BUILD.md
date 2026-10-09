# Build and install

On Debian/Ubuntu, install the build dependencies:

```sh
sudo apt-get install autoconf automake pkg-config libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
```

Build with the default, mandatory AirPlay 2 Linux PulseAudio stack:

```sh
autoreconf -fi
mkdir build
cd build
../configure --sysconfdir=/etc
make -j2
make check
sudo make install
```

An in-tree build also works: run `./configure`, `make`, and `make check` at the repository root. Run `make distclean` before switching from an in-tree configuration to another build directory.

Install and run NQPTP compatible with this receiver's shared-memory interface (currently SMI version 10) and Avahi as system services. Start a PulseAudio-compatible user session. PipeWire users should run `pipewire-pulse`; the native PipeWire backend is not provided.

Installation puts the binary under the chosen prefix and the sample configuration at `shairport-sync.conf.sample` under `sysconfdir`. Existing configuration is preserved. Copy and edit the sample deliberately, removing legacy backend, DSP and metadata-export settings.

From the repository root, `./user-service-install.sh --dry-run` previews user service installation. `./user-service-install.sh` installs and starts the unit for your current user. Its default executable path is `/usr/local/bin/shairport-sync`; edit the unit if you chose another prefix. Do not run the installer as root. NQPTP and Avahi must already be running.

`systemctl --user status shairport-sync` and `journalctl --user -u shairport-sync` show receiver status and logs. The unit does not create a system account or install a system-wide receiver service.
