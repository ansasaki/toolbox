#!/bin/bash

# Setup test keys and certificates signed by a mini CA.
# The client keys are stored in a SoftHSM device.

# This script is intended to be "sourced".
# This will setup all the keys:
#
# $ source setup-softhsm-rsa.sh

# You can erase the generated files using the teardown function:
#
# $ teardown

# You can setup new keys using the setup function:
#
# $ setup

function setup {
    export TESTDIR=`mktemp -d`

    # Create temporary directories
    mkdir -p $TESTDIR
    mkdir -p $TESTDIR/tokens
    mkdir -p $TESTDIR/ca
    mkdir -p $TESTDIR/server
    mkdir -p $TESTDIR/client
    mkdir -p $TESTDIR/db

    # Create SoftHSM configuration file
    cat >$TESTDIR/softhsm.conf <<EOF
directories.tokendir = $TESTDIR/db
objectstore.backend = file
EOF

    export SOFTHSM2_CONF=$TESTDIR/softhsm.conf

    softhsm2-util --init-token --label softhsm --free --pin 1234 --so-pin 1234 

    # Generate CA key pair and self-signed certificate
    openssl req -new -newkey rsa:4096 -x509 -days 1 -nodes -keyout \
        "$TESTDIR/ca/ca.key" -out "$TESTDIR/ca/ca.crt" -sha256 -subj="/CN=testCA/"

    # Generate server key pair and CSR
    openssl req -new -newkey rsa:2048 -days 1 -nodes -keyout \
        "$TESTDIR/server/server.key" -out "$TESTDIR/server/server.csr" \
        -subj="/CN=localhost/"

    # Sign server certificate
    openssl x509 -req -in "$TESTDIR/server/server.csr" \
        -CAkey "$TESTDIR/ca/ca.key" -CA "$TESTDIR/ca/ca.crt" \
        -out "$TESTDIR/server/server.crt" -CAserial "$TESTDIR/ca/ca.srl" \
        -CAcreateserial

    # Generate client key pair and CSR
    openssl req -new -newkey rsa:2048 -days 1 -nodes -keyout \
        "$TESTDIR/client/client.key" -out "$TESTDIR/client/client.csr" \
        -subj="/CN=client/"

    # Sign client certificate
    openssl x509 -req -in "$TESTDIR/client/client.csr" \
        -CAkey "$TESTDIR/ca/ca.key" -CA "$TESTDIR/ca/ca.crt" \
        -out "$TESTDIR/client/client.crt" -CAserial "$TESTDIR/ca/ca.srl" \
        -extensions usr_cert

    # Import key and certificate to the token
#    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
#        --load-privkey "$TESTDIR/client/client.key" --label test \
#        --login --set-pin=1234
#    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
#        --load-certificate "$TESTDIR/client/client.crt" --label test \
#        --login --set-pin=1234
    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
        --load-privkey "$TESTDIR/server/server.key" --label test \
        --login --set-pin=1234 "pkcs11:token=softhsm"
    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
        --load-certificate "$TESTDIR/server/server.crt" --label test \
        --login --set-pin=1234 "pkcs11:token=softhsm"

    p11tool --list-all --login "pkcs11:token=softhsm" \
        --set-pin=1234

    echo Done!
}

# Cleanup
function teardown {
    rm -rf $TESTDIR
    unset TESTDIR
    unset SOFTHSM2_CONF
}

setup
