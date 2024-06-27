#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 TARGET PACKAGE" >&2
    exit 1
fi

build=$(brew latest-build $1 $2 --quiet | awk '{print $1; exit}')
echo "Downloading packages from build $build"

for i in aarch64 ppc64le s390x x86_64; do
    mkdir $i
    pushd $i
    brew download-build $build --debuginfo --arch $i
    popd
done
