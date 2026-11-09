# Rustrx

A native Matrix client for Sailfish OS: a Silica interface on top of
[matrix-rust-sdk](https://github.com/matrix-org/matrix-rust-sdk), the library
behind Element X. The SDK does the protocol work (sync, end-to-end encryption,
key backup, device verification); Rustrx is the phone side.

The name mushes Rust and Matrix together. The icon is a rainbow flag.

## What it does

- Log in with password or single sign-on, everything on one page, errors
  shown inline, the fields kept.
- Two panels, Rooms and People, in the tab view Jolla uses for Settings.
  Invitations and favourites sit on top, the rest is ordered by activity.
- Conversations with text, replies, reactions, images, files, stickers,
  location and state events; a push-up menu to take a photo or send an
  image or file.
- Device verification by emoji comparison, incoming or started from
  Settings; restoring encrypted history with the recovery key; key backup.
- Notifications while the app runs. Background sync is a later step.

## How it is built

`core/` is a Rust crate that wraps matrix-rust-sdk behind three C functions
(`core/src/lib.rs`). Commands go in as JSON, events come back as JSON on one
callback. It is cross-compiled for aarch64 with `cargo-zigbuild`
(`core/build-aarch64.sh`), because the Rust shipped in the Sailfish SDK is too
old for the SDK, and placed at `lib/librustrx.so`.

The rest is an ordinary Sailfish application (qmake, `rpm/`, `qml/`,
`src/`). The C++ in `src/` is the bridge from the callback to the GUI thread,
plus two list models. It is built with the Sailfish Platform SDK and installs
the core next to its data in `/usr/share/harbour-rustrx/lib`.

Because of the bundled library the package is for Chum and OpenRepos, not the
Jolla Store.

## Status

0.2.0, the first release. Written in one evening for a Jolla Phone 2026 that
had no working native Matrix client, and used on it since. Tested against
one account on one homeserver so far; SSO login is untested, QR login is
not in yet, and there are no notifications while the app is closed.

## Install

RPMs are attached to the GitHub releases. Rustrx bundles its core library,
so it is a Chum or OpenRepos app, not a Jolla Store one.

## Authorship

The program was written by an LLM (Claude), directed and tested by the
author on a real device. Every commit carries an LLMGD disclosure line; the
grading scheme is described at [moWerk/llmgd-specs](https://github.com/moWerk/llmgd-specs).

## Licence

GPL-3.0-or-later. matrix-rust-sdk is Apache-2.0.
