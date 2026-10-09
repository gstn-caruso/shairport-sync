# Embedded AirPlay pairing

The receiver builds `pair.c`, `pair_fruit.c`, `pair_homekit.c` and `pair-tlv.c` for its AirPlay 2 pairing endpoints. This fork fixes the pairing crypto implementation to libgcrypt with libsodium; the receiver's cryptography also requires OpenSSL. Standalone example clients/servers and alternate pairing crypto implementations are not included.

Original authorship and license notices remain in each source file.
