#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/err.h>

static void display_openssl_errors(int l)
{
    const char *file;
    char buf[120];
    int e, line;

    if (ERR_peek_error() == 0)
        return;
    fprintf(stderr, "At main.c:%d:\n", l);

    while ((e = ERR_get_error_line(&file, &line))) {
        ERR_error_string(e, buf);
        fprintf(stderr, "- SSL %s: %s:%d\n", buf, file, line);
    }
}

int main(int argc, char *argv[]) {

    ENGINE *engine;
    EVP_PKEY *pkey;

    EVP_MD_CTX *md;

    unsigned char signature[256];
    unsigned int sig_len = 0, i;

    if (argc < 2){
        printf("Too few arguments\n");
        printf("Please provide the key\n");
        return 0;
    }

    ENGINE_load_builtin_engines();

    engine = ENGINE_by_id("pkcs11");

    if (engine == NULL) {
        printf("Could not get engine\n");
        display_openssl_errors(__LINE__);
        return 1;
    }
    printf("Engine got\n");
/*
   if (!ENGINE_set_default(engine, ENGINE_METHOD_ALL)) {
        printf("Could not set as default\n");
        display_openssl_errors(__LINE__);
        ENGINE_free(engine);
        return 1;
   }
// */
//*
    if (!ENGINE_init(engine)) {
        printf("Could not initialize engine\n");
        display_openssl_errors(__LINE__);
        return 1;
    }
    printf("Engine initialized\n");
// */

    pkey = ENGINE_load_private_key(engine, argv[1], 0, 0);

//*
    ENGINE_finish(engine);
    ENGINE_free(engine);
// */
    if (pkey == NULL) {
        printf("Could not load key\n");
        display_openssl_errors(__LINE__);
        return 1;
    }

    printf("Pkey loaded!\n");


/*
    ENGINE_finish(engine);
// */

    //ENGINE_free(engine);

/*
    fork();
    fork();
// */
    md = EVP_MD_CTX_new();

    if (md == NULL) {
        printf("md null\n");
        return 1;
    }

    if (!EVP_SignInit(md, EVP_sha256())){
        printf("sign init failed\n");
        display_openssl_errors(__LINE__);
        return 1;
    }

    EVP_SignUpdate(md, "message", 7);

    EVP_SignFinal(md, signature, &sig_len, pkey);

    EVP_PKEY_free(pkey);

    EVP_MD_CTX_free(md);

    printf("signature generated: ");
    for (i = 0; i < sig_len; i++){
        printf("%02X", signature[i]);
    }
    printf("\n");

    return 0;
}
