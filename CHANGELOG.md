## [6.0.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.2...v6.0.0) (2026-10-10)

### ⚠ BREAKING CHANGES

* **app:** only --config PATH, --version, and --check-config are accepted. Move receiver setting flags to their configuration equivalents; systemd owns foreground process supervision.

* fix(config): reject output channel maps exceeding storage

* fix(config): validate setting types and default file boundaries

* fix(audio): release PulseAudio resources after failed startup

* ci: validate packaged receiver configuration without services

* fix(config): validate fixed latency before starting services

* fix(config): reject integers that cannot survive conversion

* fix(config): fail when an explicit file cannot be resolved

### Features

* **app:** introduce foreground daemon interface ([#38](https://github.com/gstn-caruso/shairport-sync/issues/38)) ([40e21db](https://github.com/gstn-caruso/shairport-sync/commit/40e21db0c8e429878f1fecfdff6c7e582ccd5d21))

### Documentation

* establish receiver migration contracts ([#30](https://github.com/gstn-caruso/shairport-sync/issues/30)) ([355c936](https://github.com/gstn-caruso/shairport-sync/commit/355c9364b7ba5467222538e766ede52b69f7dc9d))

### Tests

* **audio:** clarify sample and volume contracts ([#34](https://github.com/gstn-caruso/shairport-sync/issues/34)) ([dcfb21a](https://github.com/gstn-caruso/shairport-sync/commit/dcfb21af57fd71141c0c170d3ccac254e1e0b633))
* **audio:** expose resampler samples and ownership outcomes ([#36](https://github.com/gstn-caruso/shairport-sync/issues/36)) ([bffa071](https://github.com/gstn-caruso/shairport-sync/commit/bffa07163399a83ec73bf3005f3f8ae2a128bcde))
* **audio:** express decoder formats and failures directly ([#35](https://github.com/gstn-caruso/shairport-sync/issues/35)) ([0b0aa0d](https://github.com/gstn-caruso/shairport-sync/commit/0b0aa0d7a4a50fc46c0df3d42165f45e84a40187))
* **rtsp:** express independent dispatch scenarios ([#31](https://github.com/gstn-caruso/shairport-sync/issues/31)) ([830b235](https://github.com/gstn-caruso/shairport-sync/commit/830b235499a23efd0fa92500110de426bad33af6))
* **rtsp:** separate framing and parsing contracts ([#33](https://github.com/gstn-caruso/shairport-sync/issues/33)) ([84c26bc](https://github.com/gstn-caruso/shairport-sync/commit/84c26bc5d4980a52854926fee0f28514ea74803b))
* **session:** expose ownership and shutdown contracts ([#37](https://github.com/gstn-caruso/shairport-sync/issues/37)) ([584d2f9](https://github.com/gstn-caruso/shairport-sync/commit/584d2f9717994484720aa2b59e22545265e7cb9a))

### Continuous Integration

* suspend ARM64 builds and release artifacts ([#29](https://github.com/gstn-caruso/shairport-sync/issues/29)) ([dbe218e](https://github.com/gstn-caruso/shairport-sync/commit/dbe218e4422ee31343d44a5aaaf0ab55a0d7bf63))

## [5.7.2](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.1...v5.7.2) (2026-10-10)

### Bug Fixes

* **release:** honor conventional commit version policy ([#28](https://github.com/gstn-caruso/shairport-sync/issues/28)) ([8822f21](https://github.com/gstn-caruso/shairport-sync/commit/8822f217334fb1cf0eedbef7c7d83a84c6ba9d33))

## [5.7.1](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.0...v5.7.1) (2026-10-10)

### Code Refactoring

* **audio-format:** delegate channel mapping decisions ([#27](https://github.com/gstn-caruso/shairport-sync/issues/27)) ([8b495a7](https://github.com/gstn-caruso/shairport-sync/commit/8b495a755c757f904964d826944f56b2f3277548))
* **volume:** delegate attenuation range decisions ([#26](https://github.com/gstn-caruso/shairport-sync/issues/26)) ([fd1ca7b](https://github.com/gstn-caruso/shairport-sync/commit/fd1ca7bd333de7c000b0ea8629fc354600062408))

## [5.7.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.6.0...v5.7.0) (2026-10-10)

### Features

* **release:** publish native arm64 Debian packages alongside amd64 ([#25](https://github.com/gstn-caruso/shairport-sync/issues/25)) ([764f7b3](https://github.com/gstn-caruso/shairport-sync/commit/764f7b3b1c69810da91e8cae6ddcc275b94cf417))

## [5.6.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.5.1...v5.6.0) (2026-10-10)

### Features

* **release:** publish versioned Debian packages after validated pushes ([#23](https://github.com/gstn-caruso/shairport-sync/issues/23)) ([1438584](https://github.com/gstn-caruso/shairport-sync/commit/1438584317b0c242eb25cb13240bfcccbc88ca18))

### Bug Fixes

* **release:** pin changelog preset compatible with notes writer ([#24](https://github.com/gstn-caruso/shairport-sync/issues/24)) ([3a0fbc9](https://github.com/gstn-caruso/shairport-sync/commit/3a0fbc9a342501b24056d1fdf756e043e44aa56e))
