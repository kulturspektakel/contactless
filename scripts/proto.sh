#!/usr/bin/env bash
set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
NANOPB="nanopb-0.4.5-macosx-x86" 
TMP_DIR=$(mktemp -d)
echo "Create temporary directory $TMP_DIR"
cd $TMP_DIR
echo "Downloading nanopb..."
curl -sO "https://jpa.kapsi.fi/nanopb/download/$NANOPB.tar.gz"
echo "Extracting nanopb..."
tar -xf "$NANOPB.tar.gz"
cd "$SCRIPT_DIR/../src/proto"
for file in *.proto; do
    [ -f "$file" ] || break
    echo "Generator for $file"
    protoc --experimental_allow_proto3_optional -o"${file%%.*}".pb $file
    "$TMP_DIR/$NANOPB/generator-bin/nanopb_generator" ${file%%.*}.pb
done
rm -rf $TMP_DIR
echo "All done."