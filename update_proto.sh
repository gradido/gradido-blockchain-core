#!/bin/bash
if ! command -v pbtools  >/dev/null 2>&1; then
    echo "pbtools is missing, please install it with 'pip install pbtools' first. Needs Python."
    exit 1
fi
# cd gradido_protocol
# pbtools generate_c_source proto/gradido/basic_types.proto
# mv basic_types.c ../src/data/proto/gradido/
# mv basic_types.h ../include/gradido_blockchain_core/data/proto/gradido/

# Base directory (where gradido_protocol is located)
PROTO_ROOT="gradido_protocol/proto/gradido"
cd "$PROTO_ROOT" || exit 1

# Target base directories
SRC_BASE="../../../src/data/proto/gradido"
INC_BASE="../../../include/gradido_blockchain_core/data/proto/gradido"

# Function for processing a proto file
process_proto() {
    local proto_file="$1"          # e.g., proto/gradido/basic_types.proto
    local proto_dir=$(dirname "$proto_file")          # proto/gradido
    local proto_name=$(basename "$proto_file" .proto) # basic_types

    # Extract relative path below proto/ (e.g., gradido)
    local rel_path="${proto_dir#proto/}"   # Removes 'proto/' from beginning -> gradido or hiero/...

    # Target folders for .c and .h (same folder structure under SRC_BASE and INC_BASE)
    local target_src_dir="$SRC_BASE/$rel_path"
    local target_inc_dir="$INC_BASE/$rel_path"

    mkdir -p "$target_src_dir" "$target_inc_dir"

    echo "Generating $proto_file ..."
    pbtools generate_c_source "$proto_file"

    # Move generated .c and .h (pbtools creates them in current directory)
    if [ -f "$proto_name.c" ]; then
        mv "$proto_name.c" "$target_src_dir/"
    fi
    if [ -f "$proto_name.h" ]; then
        mv "$proto_name.h" "$target_inc_dir/"
    fi
}

# Start with Clean workspace
rm -rf src/data/proto
rm -rf include/gradido_blockchain_core/data/proto

# Find all .proto files in proto/gradido and proto/hiero
for proto_file in *.proto; do
    if [ -f "$proto_file" ]; then
        process_proto "$proto_file"
    else
        echo "No .proto files found in $proto_file (placeholder did not expand)."
    fi
done

cd ..
