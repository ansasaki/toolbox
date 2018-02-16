#!/bin/bash

# Setup test server and client with certificates signed by a mini CA.
# The client keys are also stored in a SoftHSM device.

# This script is intended to be "sourced".
# This will setup all the keys and run the test server:
#
# $ source setup-test-server.sh

# You can kill the server and erase the generated files using the finish_server
#
# $ finish_server

# You can setup a new server and run again using setup_server
#
# $ setup_server

function setup_server {
    export TESTDIR=/tmp/wget-pkcs11-test.$$

    # Create temporary directories
    mkdir -p $TESTDIR
    mkdir -p $TESTDIR/ca
    mkdir -p $TESTDIR/server
    mkdir -p $TESTDIR/client
    mkdir -p $TESTDIR/db

    # Create SoftHSM device
    cat >$TESTDIR/softhsm.conf <<EOF
directories.tokendir = $TESTDIR/db
objectstore.backend = file
EOF

    export SOFTHSM2_CONF=$TESTDIR/softhsm.conf
    softhsm2-util --init-token --free --label test --so-pin 1234 --pin 1234

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
    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
        --load-privkey "$TESTDIR/client/client.key" --label test \
        --login --set-pin=1234
    p11tool --provider /usr/lib64/pkcs11/libsofthsm2.so --write \
        --load-certificate "$TESTDIR/client/client.crt" --label test \
        --login --set-pin=1234

    # Start server
    gnutls-serv --x509certfile=$TESTDIR/server/server.crt \
        --x509keyfile=$TESTDIR/server/server.key \
        --x509cafile=$TESTDIR/ca/ca.crt \
        --require-client-cert &
    export SERVERPID=$!

    tput setaf 2
    #echo "Remember to kill the server (pid $SERVERPID)"
    echo "End server calling \"finish_server\""
    tput setaf 1
    echo "Or manually kill it (pid $SERVERPID) and remove $TESTDIR"
    tput sgr 0
}

# Cleanup
function finish_server {
    rm -rf $TESTDIR
    kill $SERVERPID
    unset TESTDIR
    unset SERVERPID
    unset SOFTHSM2_CONF
}

if [ -z ${TESTDIR+x} ]
then
    setup_server
else
    echo "Server already running (pid $SERVERPID)"
    echo "Test directory: $TESTDIR"
fi

