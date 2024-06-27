#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 PACKAGE" >&2
    exit 1
fi

result="0"
for i in aarch64 ppc64le s390x x86_64; do
    pushd $i
    echo "$i"
    echo "running annocheck -v --ignore-unknown $(ls | grep $1 | grep -v debuginfo | head -1) --debug-rpm $(ls | grep $1 | grep debuginfo |head -1)"
    annocheck $(ls | grep $1 | grep -v debuginfo | head -1) --debug-rpm $(ls | grep $1 | grep debuginfo |head -1) -v
    if [ "$?" != "0" ]; then
        result="1"
    fi
    popd
done

if [ "$result" != "0" ]; then
    echo Overall for all arches: FAIL
else
    echo Overall for all arches: PASS
fi

exit $result
