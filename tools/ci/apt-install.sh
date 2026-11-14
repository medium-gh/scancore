#!/usr/bin/env bash
# apt-get install on a CI runner: tools/ci/apt-install.sh <package>...
# Runners now and then get an Ubuntu mirror that crawls (27 Oct 2026: 22
# minutes for what takes 40 seconds). Each try has a time limit; a failed or
# slow try moves to the main Ubuntu archive and tries again. On 11 Nov 2026
# the Azure mirror's name stopped resolving altogether; the runner images
# reach it through mirror+file:/etc/apt/apt-mirrors.txt, so the move has to
# rewrite that list too, not only the sources.
set -u

use_main_archive() {
  local f
  for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    [ -f "$f" ] || continue
    sudo sed -i -E \
      -e 's#https?://azure\.archive\.ubuntu\.com#http://archive.ubuntu.com#g' \
      -e 's#mirror\+file:/etc/apt/apt-mirrors\.txt/?#http://archive.ubuntu.com/ubuntu/#g' \
      "$f"
  done
  if [ -f /etc/apt/apt-mirrors.txt ]; then
    echo "http://archive.ubuntu.com/ubuntu/" | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
  fi
}

for attempt in 1 2 3; do
  if timeout 240 sudo apt-get -q -o Acquire::Retries=2 -o Acquire::http::Timeout=20 update &&
     timeout 600 sudo apt-get -q -y -o Acquire::Retries=2 -o Acquire::http::Timeout=20 install "$@"; then
    exit 0
  fi
  echo "::warning::apt-get try $attempt failed or took too long; trying again from archive.ubuntu.com"
  # A try stopped halfway can leave packages unpacked but not set up.
  sudo dpkg --configure -a || true
  use_main_archive
  sleep 5
done
echo "::error::apt-get install failed three times: $*"
exit 1
