#!/bin/sh
# Cross-build librustrx.so for SailfishOS (aarch64, glibc >= 2.38) on a Linux
# host with rustup and cargo-zigbuild. Run from core/. Output:
# target/aarch64-unknown-linux-gnu/release/librustrx.so
set -eu
export PATH="$HOME/.cargo/bin:$HOME/sailfish-build/tools/zig:$PATH"
# lld emits no build-id by default; rpmbuild's debuginfo pass wants one.
export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-Wl,--build-id=sha1"
# --locked: Cargo.lock is part of the tree; a build that would change it fails.
# Toolchain used for the releases: rustc 1.99.1, zig 0.17.1, cargo-zigbuild.
exec cargo zigbuild --locked --release --target aarch64-unknown-linux-gnu.2.38 "$@"
