#!/bin/sh
exec proot -q /usr/bin/qemu-aarch64 "$@"
