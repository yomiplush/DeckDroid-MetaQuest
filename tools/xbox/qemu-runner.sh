#!/bin/sh
set -eu
case "${1##*/}" in
    xodus_cli-*|xodus-cli) exec proot -q /usr/bin/qemu-aarch64 "$@" ;;
    *) exec /usr/bin/qemu-aarch64 "$@" ;;
esac
