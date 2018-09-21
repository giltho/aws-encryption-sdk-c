/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may not use
 * this file except in compliance with the License. A copy of the License is
 * located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <aws/cryptosdk/private/materials.h>
#include <aws/cryptosdk/private/raw_rsa_keyring.h>
#include "raw_rsa_keyring_test_vectors.h"
#include "testing.h"

static struct aws_allocator *alloc;
static struct aws_cryptosdk_keyring *kr_public_key;
static struct aws_cryptosdk_keyring *kr_private_key;
static struct aws_cryptosdk_encryption_materials *enc_mat;
static struct aws_cryptosdk_decryption_materials *dec_mat;
static struct aws_cryptosdk_decryption_request req;

static enum aws_cryptosdk_alg_id alg_ids[] = { AES_128_GCM_IV12_AUTH16_KDSHA256_SIGNONE,
                                               AES_192_GCM_IV12_AUTH16_KDSHA256_SIGNONE,
                                               AES_256_GCM_IV12_AUTH16_KDSHA256_SIGNONE };
                                            
static enum aws_cryptosdk_rsa_padding_mode rsa_padding_mode[] = { AWS_CRYPTOSDK_RSA_PKCS1,
                                                                  AWS_CRYPTOSDK_RSA_OAEP_SHA1_MGF1,
                                                                  AWS_CRYPTOSDK_RSA_OAEP_SHA256_MGF1 };

static enum aws_cryptosdk_aes_key_len data_key_len[] = { AWS_CRYPTOSDK_AES_128,
                                                         AWS_CRYPTOSDK_AES_192,
                                                         AWS_CRYPTOSDK_AES_256 };

static int copy_edks_from_enc_mat_to_dec_req() {
    TEST_ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&req.encrypted_data_keys, alloc, 1, sizeof(struct aws_cryptosdk_edk)));
    TEST_ASSERT_SUCCESS(aws_array_list_copy(&enc_mat->encrypted_data_keys, &req.encrypted_data_keys));

    return 0;
}

static int decrypt_data_key_and_verify_same_as_one_in_enc_mat() {
    TEST_ASSERT_SUCCESS(aws_cryptosdk_keyring_decrypt_data_key(kr_private_key, dec_mat, &req));
    TEST_ASSERT_ADDR_NOT_NULL(dec_mat->unencrypted_data_key.buffer);
    TEST_ASSERT_INT_EQ(dec_mat->unencrypted_data_key.len, enc_mat->unencrypted_data_key.len);
    TEST_ASSERT(!memcmp(
        dec_mat->unencrypted_data_key.buffer, enc_mat->unencrypted_data_key.buffer, dec_mat->unencrypted_data_key.len));

    return 0;
}

static int set_up_encrypt(enum aws_cryptosdk_rsa_padding_mode rsa_padding_mode, enum aws_cryptosdk_alg_id alg) {
    alloc = aws_default_allocator();
    kr_public_key = raw_rsa_keyring_tv_new(alloc, rsa_padding_mode, AWS_CRYPTOSDK_RSA_ENCRYPT);
    TEST_ASSERT_ADDR_NOT_NULL(kr_public_key);
    enc_mat = aws_cryptosdk_encryption_materials_new(alloc, alg);
    TEST_ASSERT_ADDR_NOT_NULL(enc_mat);

    return 0;
}

static int set_up_decrypt(enum aws_cryptosdk_rsa_padding_mode rsa_padding_mode, enum aws_cryptosdk_alg_id alg) {
    alloc = aws_default_allocator();
    kr_private_key = raw_rsa_keyring_tv_new(alloc, rsa_padding_mode, AWS_CRYPTOSDK_RSA_DECRYPT);
    TEST_ASSERT_ADDR_NOT_NULL(kr_private_key);
    req.alloc = alloc;
    req.alg = alg;
    dec_mat = aws_cryptosdk_decryption_materials_new(alloc, alg);
    TEST_ASSERT_ADDR_NOT_NULL(dec_mat);

    return 0;
}

static void tear_down_encrypt() {
    aws_cryptosdk_encryption_materials_destroy(enc_mat);
    aws_cryptosdk_keyring_destroy(kr_public_key);
}

static void tear_down_decrypt() {
    aws_cryptosdk_keyring_destroy(kr_private_key);
    aws_cryptosdk_decryption_materials_destroy(dec_mat);
    aws_array_list_clean_up(&req.encrypted_data_keys);
}

/**
 * Testing generate and decrypt functions bare minimum framework.
 */
int generate_encrypt_data_key() {
    for (int wrap_idx = 0; wrap_idx < sizeof(rsa_padding_mode) / sizeof(enum aws_cryptosdk_rsa_padding_mode);
         ++wrap_idx) {
        for (int alg_idx = 0; alg_idx < sizeof(alg_ids) / sizeof(enum aws_cryptosdk_alg_id); ++alg_idx) {
            TEST_ASSERT_SUCCESS(set_up_encrypt(rsa_padding_mode[wrap_idx], alg_ids[alg_idx]));
            TEST_ASSERT_SUCCESS(aws_cryptosdk_keyring_generate_data_key(kr_public_key, enc_mat));
            TEST_ASSERT_ADDR_NOT_NULL(enc_mat->unencrypted_data_key.buffer);
            TEST_ASSERT_INT_EQ(enc_mat->unencrypted_data_key.len, data_key_len[alg_idx]);

            const struct aws_cryptosdk_edk *edk = NULL;
            if (aws_array_list_get_at_ptr(&enc_mat->encrypted_data_keys, (void **)&edk, 0)) { return AWS_OP_ERR; }
            TEST_ASSERT_ADDR_NOT_NULL(&edk->enc_data_key);
            TEST_ASSERT_INT_EQ(aws_array_list_length(&enc_mat->encrypted_data_keys), 1);
            tear_down_encrypt();
        }
    }
    return 0;
}

int generate_encrypt_decrypt_data_key() {
    for (int wrap_idx = 0; wrap_idx < sizeof(rsa_padding_mode) / sizeof(enum aws_cryptosdk_rsa_padding_mode);
         ++wrap_idx) {
        for (int alg_idx = 0; alg_idx < sizeof(alg_ids) / sizeof(enum aws_cryptosdk_alg_id); ++alg_idx) {
            TEST_ASSERT_SUCCESS(set_up_encrypt(rsa_padding_mode[wrap_idx], alg_ids[alg_idx]));
            TEST_ASSERT_SUCCESS(aws_cryptosdk_keyring_generate_data_key(kr_public_key, enc_mat));
            TEST_ASSERT_ADDR_NOT_NULL(enc_mat->unencrypted_data_key.buffer);

            const struct aws_cryptosdk_alg_properties *props = aws_cryptosdk_alg_props(alg_ids[alg_idx]);
            TEST_ASSERT_INT_EQ(enc_mat->unencrypted_data_key.len, props->data_key_len);
            TEST_ASSERT_INT_EQ(aws_array_list_length(&enc_mat->encrypted_data_keys), 1);

            TEST_ASSERT_SUCCESS(set_up_decrypt(rsa_padding_mode[wrap_idx], alg_ids[alg_idx]));
            TEST_ASSERT_SUCCESS(copy_edks_from_enc_mat_to_dec_req());
            TEST_ASSERT_SUCCESS(decrypt_data_key_and_verify_same_as_one_in_enc_mat());
            tear_down_encrypt();
            tear_down_decrypt();
        }
    }
    return 0;
}
struct test_case raw_rsa_keyring_encrypt_test_cases[] = {
    { "raw_rsa_keyring", "generate_encrypt_data_key", generate_encrypt_data_key },
    { "raw_rsa_keyring", "generate_encrypt_decrypt_data_key", generate_encrypt_decrypt_data_key },
    { NULL }
};
