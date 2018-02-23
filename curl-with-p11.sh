#!/bin/bash

# This test setup a server and a client with certificates signed by a mini CA.
# The client keys are also stored in a SoftHSM device.

# The test runs the test server and calls curl using the client's credentials
# stored in the SoftHSM

export TESTDIR=/tmp/curl-pkcs11-test.$$

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
openssl req -new -newkey rsa:2048 -x509 -days 1 -nodes -keyout \
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

# Try to connect using curl

# Using PKCS#11 URI for key and certificate
../curl/src/curl --key "pkcs11:token=test;object=test;type=private;pin-value=1234" \
    -E "pkcs11:token=test;object=test;type=cert" \
    --cacert "$TESTDIR/ca/ca.crt" \
    --output "$TESTDIR/out" https://localhost:5556
RV=$?

if [ $RV != 0 ];
then
    echo curl returned $RV trying PKCS#11 URI for key and cert
    kill $SERVERPID
    unset TESTDIR
    unset SERVERPID
    unset SOFTHSM2_CONF
    exit $RV
fi

# Using PEM file for key and PKCS#11 URI for certificate
../curl/src/curl --key "$TESTDIR/client/client.key" \
    -E "pkcs11:token=test;object=test;type=cert" \
    --cacert "$TESTDIR/ca/ca.crt" \
    --output "$TESTDIR/out" https://localhost:5556
RV=$?

if [ $RV != 0 ];
then
    echo curl returned $RV trying PEM key and PKCS#11 URI for cert
    kill $SERVERPID
    unset TESTDIR
    unset SERVERPID
    unset SOFTHSM2_CONF
    exit $RV
fi

# Using PEM file for key and certificate
../curl/src/curl --key "$TESTDIR/client/client.key" \
    -E "$TESTDIR/client/client.crt" \
    --cacert "$TESTDIR/ca/ca.crt" \
    --output "$TESTDIR/out" https://localhost:5556
RV=$?

if [ $RV != 0 ];
then
    echo curl returned $RV trying PEM key and cert
    kill $SERVERPID
    unset TESTDIR
    unset SERVERPID
    unset SOFTHSM2_CONF
    exit $RV
fi

# Using PKCS#11 URI for key and PEM file for certificate
../curl/src/curl \
    --key "pkcs11:token=test;object=test;type=private;pin-value=1234" \
    -E "$TESTDIR/client/client.crt" \
    --cacert "$TESTDIR/ca/ca.crt" \
    --output "$TESTDIR/out" https://localhost:5556
RV=$?

if [ $RV != 0 ];
then
    echo curl returned $RV trying PKCS#11 URI for key and PEM cert
    kill $SERVERPID
    unset TESTDIR
    unset SERVERPID
    unset SOFTHSM2_CONF
    exit $RV
fi


# Cleanup
rm -rf $TESTDIR
kill $SERVERPID
unset TESTDIR
unset SERVERPID
unset SOFTHSM2_CONF

echo curl returned $RV
exit $RV
