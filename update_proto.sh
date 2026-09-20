#!/bin/bash
# Regenerates src/data/proto/ and its headers from gradido_protocol/.
#
# The generator is pinned to the pbtools runtime vendored in third_party/: the generated code
# calls into that runtime and carries no version of its own, so the two have to be the same
# release. The version is read from the runtime header rather than written down twice, and
# pbtools is installed into a virtual environment beside this script -- nothing is added to the
# system Python, which Debian and friends refuse to let go of anyway.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
PROTO_ROOT="$REPO_ROOT/gradido_protocol/proto/gradido"
RUNTIME_HEADER="$REPO_ROOT/third_party/pbtools/pbtools.h"
VENV="$REPO_ROOT/.venv-pbtools"

# Target base directories
SRC_BASE="$REPO_ROOT/src/data/proto"
INC_BASE="$REPO_ROOT/include/gradido_blockchain_core/data/proto"

# ********** the generator, in a place of its own *******************

PBTOOLS_VERSION=$(sed -n 's/.*PBTOOLS_VERSION "\([^"]*\)".*/\1/p' "$RUNTIME_HEADER")
if [ -z "$PBTOOLS_VERSION" ]; then
    echo "no PBTOOLS_VERSION in $RUNTIME_HEADER -- the generator cannot be pinned to the runtime."
    exit 1
fi

# A venv that holds another version is not upgraded but replaced: a half matching generator is
# the one outcome that must not survive this check.
if [ ! -x "$VENV/bin/pbtools" ] || \
   [ "$("$VENV/bin/pbtools" --version 2>/dev/null)" != "$PBTOOLS_VERSION" ]; then
    echo "Installing pbtools $PBTOOLS_VERSION into .venv-pbtools ..."
    rm -rf "$VENV"
    if ! python3 -m venv "$VENV"; then
        echo "python3 -m venv failed -- on Debian and Ubuntu: apt install python3-venv"
        exit 1
    fi
    "$VENV/bin/pip" install --quiet --upgrade pip wheel
    "$VENV/bin/pip" install --quiet "pbtools==$PBTOOLS_VERSION"
fi
PBTOOLS="$VENV/bin/pbtools"

# ********** generating *******************

# Everything is generated beside the tree first and moved into place once the last file is
# through. The old folders have to go -- a renamed message would otherwise leave its file behind
# to be compiled forever, since both build systems collect whole directories -- but they go at
# the end, so an interrupted run leaves the checkout exactly as it found it. The workspace sits
# inside the repository to keep that last step a rename on one filesystem.
WORK=$(mktemp -d "$REPO_ROOT/.proto-gen.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src/gradido" "$WORK/inc/gradido"

# Function for processing a proto file
process_proto() {
    local proto_file="$1"                             # e.g., basic_types.proto
    local proto_name
    proto_name=$(basename "$proto_file" .proto)       # basic_types

    echo "Generating $proto_file ..."
    "$PBTOOLS" generate_c_source "$proto_file"

    # Move generated .c and .h (pbtools creates them in current directory)
    if [ -f "$proto_name.c" ]; then
        mv "$proto_name.c" "$WORK/src/gradido/"
    fi
    if [ -f "$proto_name.h" ]; then
        mv "$proto_name.h" "$WORK/inc/gradido/"
    fi
}

cd "$PROTO_ROOT"

# What the folder holds is read before anything is replaced. An empty glob would otherwise walk
# the loop nought times and still reach the exchange below, where the generated tree is removed
# and two empty folders take its place -- a checkout without its submodule would quietly wipe
# what it could not regenerate.
shopt -s nullglob
proto_files=(*.proto)
shopt -u nullglob

if [ ${#proto_files[@]} -eq 0 ]; then
    echo "no .proto files in $PROTO_ROOT -- is the gradido_protocol submodule checked out?"
    exit 1
fi

for proto_file in "${proto_files[@]}"; do
    process_proto "$proto_file"
done

rm -rf "$SRC_BASE" "$INC_BASE"
mkdir -p "$(dirname "$SRC_BASE")" "$(dirname "$INC_BASE")"
mv "$WORK/src" "$SRC_BASE"
mv "$WORK/inc" "$INC_BASE"
