#!/bin/bash
# Fetch the core from the e15, build the RPM in the SDK container, install
# on the Jolla Phone 2026 (ssh alias jp26, password login) and start it.
# usage: tools/build-and-install.sh [--no-fetch] [--no-install]
set -euo pipefail
cd "$(dirname "$0")/.."
S=/tmp/claude-1000/-home-mo-sailfish/afced4d4-4500-4c4e-93ee-d8693735723a/scratchpad
fetch=1; install=1
for a in "$@"; do case $a in --no-fetch) fetch=0;; --no-install) install=0;; esac; done

if [ $fetch = 1 ]; then
  echo "step1 fetch core $(date +%T)"
  mkdir -p lib
  scp -q mo@mo-e15-eos:sailfish-build/rustrx/core/target/aarch64-unknown-linux-gnu/release/librustrx.so lib/
  ls -la lib/librustrx.so
fi

echo "step2 mb2 build $(date +%T)"
~/sailfish/scripts/sfos-mb2-build.sh "$PWD"
rpm=$(ls -t /mnt/storage/sailfish-sdk/out/rustrx/RPMS/harbour-rustrx-[0-9]*.aarch64.rpm | head -1)
echo "rpm: $rpm"

if [ $install = 1 ]; then
  echo "step3 install on jp26 $(date +%T)"
  export SSH_ASKPASS=$S/askpass-jp26.sh SSH_ASKPASS_REQUIRE=force
  scp -F ~/sailfish/work/ssh/config -q "$rpm" jp26:/home/defaultuser/Downloads/
  cat > $S/jp26-rustrx-install.sh <<EOF
pkcon -y install-local /home/defaultuser/Downloads/$(basename "$rpm") 2>&1 | grep -E "Installed|Error|error|Fatal|already" | tail -5
rpm -q harbour-rustrx
EOF
  # the device password comes from the session's askpass helper, never from this file
  SFOS_SSH_PASSWORD=1 SFOS_PW="$($S/askpass-jp26.sh)" python3 ~/sailfish/scripts/sfos-root.py jp26 $S/jp26-rustrx-install.sh $S/jp26-rustrx-install.out
  cat $S/jp26-rustrx-install.out
fi
echo "done $(date +%T)"
