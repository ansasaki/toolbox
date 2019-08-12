/*
 * Simple test which uses GnuTLS as a crypto library for AES-CBC encryption
 *
 * The purpose is to verify that the same context initialized with an IV can be
 * used to encrypt data in parts without handling the intermediate IVs manually.
 *
 * */

#include <stdio.h>
#include <stdlib.h>
#include <gnutls/crypto.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) {

    uint8_t input[1024];
    size_t input_len = 1024;

    uint8_t output[1024];
    size_t output_len = 1024;

    uint8_t decrypted[1024];
    size_t decrypted_len = 1024;

    uint8_t key[32];
    size_t key_len = 32;

    uint8_t iv[16];
    size_t iv_len = 16;

    gnutls_cipher_hd_t enc_ctx;
    gnutls_cipher_hd_t dec_ctx;

    gnutls_datum_t key_ctx;
    gnutls_datum_t iv_ctx;

    int rv;
    int i, j;

    gnutls_global_init();

    rv = gnutls_rnd(GNUTLS_RND_KEY, key, key_len);
    if (rv != 0) {
        fprintf(stderr, "Could not generate key\n");
        goto error;
    }

    key_ctx.data = key;
    key_ctx.size = gnutls_cipher_get_key_size(GNUTLS_CIPHER_AES_256_CBC);

    rv = gnutls_rnd(GNUTLS_RND_KEY, iv, iv_len);
    if (rv != 0) {
        fprintf(stderr, "Could not generate iv\n");
        goto error;
    }

    iv_ctx.data = iv;
    iv_ctx.size = iv_len;

    for (i = 0; i < input_len; i++) {
        input[i] = i % 0x100;
    }

    rv = gnutls_cipher_init(&enc_ctx, GNUTLS_CIPHER_AES_256_CBC,
                            &key_ctx, &iv_ctx);
    if (rv != 0) {
        fprintf(stderr, "Could not initialize cipher to encrypt: %s\n",
                gnutls_strerror(rv));
        goto error;
    }

    for (j = 0; j < 32; j++) {
        rv = gnutls_cipher_encrypt2(enc_ctx,
                                    input + (j * 32), 32,
                                    output + (j * 32), 32);
        if (rv != 0) {
            fprintf(stderr, "Failed to encrypt\n");
            gnutls_cipher_deinit(enc_ctx);
            goto error;
        }
    }

    printf("encrypted:\n");
    for (i = 0; i < output_len; i++) {
        if ((i > 0) && (i % 16 == 0)) {
            printf("\n");
        }
        printf("%02x ", output[i]);
    }
    printf("\n");

    gnutls_cipher_deinit(enc_ctx);

    rv = gnutls_cipher_init(&dec_ctx, GNUTLS_CIPHER_AES_256_CBC, &key_ctx, &iv_ctx);
    if (rv != 0) {
        fprintf(stderr, "Could not initialize cipher to decrypt\n");
        goto error;
    }

    for (j = 0; j < 32; j++) {
        rv = gnutls_cipher_decrypt2(dec_ctx,
                                    output + (j * 32), 32,
                                    decrypted + (j * 32), 32);
        if (rv != 0) {
            fprintf(stderr, "Failed to decrypt\n");
            gnutls_cipher_deinit(dec_ctx);
            goto error;
        }
    }

    gnutls_cipher_deinit(dec_ctx);

    printf("decrypted:\n");
    for (i = 0; i < decrypted_len; i++) {
        if ((i > 0) && (i % 16 == 0)) {
            printf("\n");
        }
        printf("%02x ", decrypted[i]);
    }
    printf("\n");

    if (!memcmp(input, decrypted, input_len)) {
        printf("Contents of input and decrypted are the same\nSUCCESS\n");
    }
    else {
        printf("Contents of input and decrypted are different\nFAILED\n");
    }

    gnutls_global_deinit();
    return 0;

error:
    gnutls_global_deinit();
    return -1;
}
