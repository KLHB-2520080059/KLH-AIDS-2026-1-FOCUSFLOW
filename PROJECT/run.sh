#!/usr/bin/env bash
# One command: builds (first run only) and starts the website, then opens your browser.
set -e
cd "$(dirname "$0")"
for t in gcc make; do
  command -v $t >/dev/null 2>&1 || { echo "'$t' is missing. Install it with:  sudo apt update && sudo apt install -y build-essential"; exit 1; }
done
make -s
exec ./build/ossp-server
