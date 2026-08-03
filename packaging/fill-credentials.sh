#!/usr/bin/env bash
#
# Injects the Todoist OAuth client id and secret into the packaging files.
#
# These are not secrets in any meaningful sense — see "OAuth in a distributed
# package" in the README — but they are also not something to commit to this
# repository by accident, so they live in .env (gitignored) and are stamped
# into the packaging files only when you are cutting a release.
#
# Usage:
#     packaging/fill-credentials.sh            # read from .env
#     packaging/fill-credentials.sh --check    # report status, change nothing
#     packaging/fill-credentials.sh --clear    # put the placeholders back
#
# Credentials may also come from the environment, which is what CI should do:
#     KTODO_CLIENT_ID=... KTODO_CLIENT_SECRET=... packaging/fill-credentials.sh

set -euo pipefail

cd "$(dirname "$0")/.."

FLATPAK='packaging/flatpak/io.github.timpalpant.ktodo.yml'
FLATPAK_CI='packaging/flatpak/io.github.timpalpant.ktodo.ci.yml'
PKGBUILD='packaging/aur/ktodo/PKGBUILD'
PKGBUILD_GIT='packaging/aur/ktodo-git/PKGBUILD'

ALL_FILES=("$FLATPAK" "$FLATPAK_CI" "$PKGBUILD" "$PKGBUILD_GIT")

mode="${1:-fill}"

if [[ "$mode" == "--check" ]]; then
    for f in "${ALL_FILES[@]}"; do
        # An unfilled line ends right after the "=", or holds an empty ''.
        if grep -qiE "client_(id|secret)=('')?$" "$f"; then
            echo "empty:  $f"
        else
            echo "filled: $f"
        fi
    done
    exit 0
fi

if [[ "$mode" == "--clear" ]]; then
    sed -i -E "s|^(      - -DKTODO_OAUTH_CLIENT_ID=).*|\1|; s|^(      - -DKTODO_OAUTH_CLIENT_SECRET=).*|\1|" "$FLATPAK" "$FLATPAK_CI"
    sed -i -E "s|^(_oauth_client_id=).*|\1''|; s|^(_oauth_client_secret=).*|\1''|" "$PKGBUILD" "$PKGBUILD_GIT"
    echo "Placeholders restored. Remember to do this before committing."
    exit 0
fi

# Environment wins over .env, so CI can supply them without a file on disk.
if [[ -z "${KTODO_CLIENT_ID:-}" || -z "${KTODO_CLIENT_SECRET:-}" ]]; then
    if [[ -f .env ]]; then
        # shellcheck disable=SC1091
        set -a; . ./.env; set +a
    fi
fi

: "${KTODO_CLIENT_ID:?set KTODO_CLIENT_ID, or put it in .env}"
: "${KTODO_CLIENT_SECRET:?set KTODO_CLIENT_SECRET, or put it in .env}"

# These values are inserted into sed replacement text below. Escape its
# replacement metacharacters so a valid OAuth value containing &, |, or a
# backslash is kept byte-for-byte rather than changing the package recipe.
escape_sed_replacement() {
    printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

oauth_client_id_escaped="$(escape_sed_replacement "$KTODO_CLIENT_ID")"
oauth_client_secret_escaped="$(escape_sed_replacement "$KTODO_CLIENT_SECRET")"

# Only the client id and secret are ever stamped in. The access token in .env
# is a personal credential for one account and must never reach a package.
sed -i -E \
    "s|^(      - -DKTODO_OAUTH_CLIENT_ID=).*|\1${oauth_client_id_escaped}|; \
     s|^(      - -DKTODO_OAUTH_CLIENT_SECRET=).*|\1${oauth_client_secret_escaped}|" \
    "$FLATPAK" "$FLATPAK_CI"

sed -i -E \
    "s|^(_oauth_client_id=).*|\1'${oauth_client_id_escaped}'|; \
     s|^(_oauth_client_secret=).*|\1'${oauth_client_secret_escaped}'|" \
    "$PKGBUILD" "$PKGBUILD_GIT"

echo "Stamped credentials into:"
printf '  %s\n' "${ALL_FILES[@]}"
echo
echo "These files now contain the client secret. That is expected for a"
echo "published package, but run --clear before committing to this repo."
