#!/usr/bin/env bash
# Makes the key pair that signs Ozark OS updates (.swu files), in keys/.
#
#   ./scripts/make-keys.sh
#
# keys/swupdate-private.pem signs each update at build time; keep it secret
# and back it up. Units only install updates signed with the private key that
# matches the public key in their image (keys/swupdate-public.pem, built in as
# /etc/swupdate/public.pem). With a new key, units already in the field won't
# take updates any more: they'd need reflashing.
#
# build.sh runs this when keys/ has no key yet. Does nothing if one exists.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="$ROOT/keys"
private="$dir/swupdate-private.pem"
public="$dir/swupdate-public.pem"

if [ -f "$private" ]; then
    [ -f "$public" ] || openssl rsa -in "$private" -pubout -out "$public" 2>/dev/null
    exit 0
fi

command -v openssl >/dev/null || { echo "openssl is needed to make the update signing key" >&2; exit 1; }

mkdir -p "$dir"
chmod 700 "$dir"
(umask 077 && openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out "$private" 2>/dev/null)
openssl rsa -in "$private" -pubout -out "$public" 2>/dev/null
echo "==> Made the update signing key: $private"
echo "    Back it up. Units only take updates signed with it."
