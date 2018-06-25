/*
 *
 * Copyright (C) 2017 GlobalLogic
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "generator.h"

uint32_t attributes_aes_hmac[KM_ATTR_COUNT_AES_HMAC] = {TEE_ATTR_SECRET_VALUE};
uint32_t attributes_rsa[KM_ATTR_COUNT_RSA] = {
						TEE_ATTR_RSA_MODULUS,
						TEE_ATTR_RSA_PUBLIC_EXPONENT,
						TEE_ATTR_RSA_PRIVATE_EXPONENT,
						TEE_ATTR_RSA_PRIME1,
						TEE_ATTR_RSA_PRIME2,
						TEE_ATTR_RSA_EXPONENT1,
						TEE_ATTR_RSA_EXPONENT2,
						TEE_ATTR_RSA_COEFFICIENT};
uint32_t attributes_ec[KM_ATTR_COUNT_EC] = {
						TEE_ATTR_ECC_CURVE,
						TEE_ATTR_ECC_PUBLIC_VALUE_X,
						TEE_ATTR_ECC_PUBLIC_VALUE_Y,
						TEE_ATTR_ECC_PRIVATE_VALUE};
uint32_t attributes_ec_short[KM_ATTR_COUNT_EC - 1] = {
						TEE_ATTR_ECC_PRIVATE_VALUE,
						TEE_ATTR_ECC_PUBLIC_VALUE_X,
						TEE_ATTR_ECC_PUBLIC_VALUE_Y};

uint32_t *TA_get_attrs_list_short(const keymaster_algorithm_t algorithm,
						const bool short_list)
{
	switch (algorithm) {
	case KM_ALGORITHM_EC:
		if (short_list)
			return attributes_ec_short;
		return attributes_ec;
	case KM_ALGORITHM_RSA:
		return attributes_rsa;
	default:
		return attributes_aes_hmac;
	}
}

uint32_t *TA_get_attrs_list(const keymaster_algorithm_t algorithm)
{
	return TA_get_attrs_list_short(algorithm, false);
}

uint32_t TA_get_key_size(const keymaster_algorithm_t algorithm)
{
	switch (algorithm) {
	case KM_ALGORITHM_AES:
		/* attr_count * (size of tag + size of attribute +
		 * attribute data size) + size of algorithm + size of key size
		 */
		return KM_ATTR_COUNT_AES_HMAC *
			(2 * sizeof(uint32_t) + KM_AES_ATTR_SIZE)
			+ sizeof(algorithm) + sizeof(uint32_t);
	case KM_ALGORITHM_HMAC:
		/* Maximal HMAC key size 128 bytes */
		return KM_ATTR_COUNT_AES_HMAC *
			(2 * sizeof(uint32_t) + KM_HMAC_ATTR_SIZE)
			+ sizeof(algorithm) + sizeof(uint32_t);
	case KM_ALGORITHM_RSA:
		/* RSA  attributes for key size 2048 bits has size 256 bytes */
		return KM_ATTR_COUNT_RSA * (2 * sizeof(uint32_t) +
			KM_RSA_ATTR_SIZE) + sizeof(algorithm) +
			sizeof(uint32_t);
	case KM_ALGORITHM_EC:
		return KM_ATTR_COUNT_EC * (2 * sizeof(uint32_t) +
			KM_EC_ATTR_SIZE) +
			sizeof(algorithm) + sizeof(uint32_t);
	default:
		return 0;
	}
}

void free_attrs(TEE_Attribute *attrs, uint32_t size)
{
	if (!attrs)
		return;
	for (uint32_t i = 0; i < size; i++) {
		if (!is_attr_value(attrs[i].attributeID))
			(void)TEE_Free(attrs[i].content.ref.buffer);
	}
	(void)TEE_Free(attrs);
}

uint32_t purpose_to_mode(const keymaster_purpose_t purpose)
{
	switch (purpose) {
	case KM_PURPOSE_ENCRYPT:
		return TEE_MODE_ENCRYPT;
	case KM_PURPOSE_DECRYPT:
		return TEE_MODE_DECRYPT;
	case KM_PURPOSE_SIGN:
		return TEE_MODE_SIGN;
	case KM_PURPOSE_VERIFY:
		return TEE_MODE_VERIFY;
	case KM_PURPOSE_DERIVE_KEY:
		return TEE_MODE_DERIVE;
	default:
		return UINT32_MAX;
	}
}

bool is_attr_value(const uint32_t tag)
{
	uint32_t mask = 1 << 29;

	/* 29th bit of tag must be 1 if it is a value */
	return tag & mask;
}

keymaster_error_t TA_check_hmac_key_size(keymaster_blob_t *key_data,
				uint32_t *key_size,
				const keymaster_digest_t digest)
{
	uint32_t min = 0;
	uint32_t max = 0;
	uint8_t *buf = NULL;
	keymaster_error_t res = KM_ERROR_OK;
	TEE_OperationHandle digest_op = TEE_HANDLE_NULL;
	uint32_t digest_out_size = 64;
	uint8_t digest_out[64];

	if (key_data->data_length == 0) {
		EMSG("HMAC key zero length");
		return KM_ERROR_UNSUPPORTED_KEY_SIZE;
	}
	switch (digest) {
	case KM_DIGEST_MD5:
		min = MIN_HMAC_MD5;
		max = MAX_HMAC_MD5;
		break;
	case KM_DIGEST_SHA1:
		min = MIN_HMAC_SHA1;
		max = MAX_HMAC_SHA1;
		break;
	case KM_DIGEST_SHA_2_224:
		min = MIN_HMAC_SHA224;
		max = MAX_HMAC_SHA224;
		break;
	case KM_DIGEST_SHA_2_256:
		min = MIN_HMAC_SHA256;
		max = MAX_HMAC_SHA256;
		break;
	case KM_DIGEST_SHA_2_384:
		min = MIN_HMAC_SHA384;
		max = MAX_HMAC_SHA384;
		break;
	case KM_DIGEST_SHA_2_512:
		min = MIN_HMAC_SHA512;
		max = MAX_HMAC_SHA512;
		break;
	default:
		return KM_ERROR_INCOMPATIBLE_DIGEST;
	}
	max /= 8;
	min /= 8;

	if (key_data->data_length > max) {
		res = TA_create_digest_op(&digest_op, digest);
		if (res != KM_ERROR_OK)
			return res;
		res = TEE_DigestDoFinal(digest_op,
				key_data->data,
				key_data->data_length,
				digest_out,
				&digest_out_size);
		if (res != KM_ERROR_OK) {
			EMSG("Failed to hash HMAC key");
			return res;
		}
		TEE_MemMove(key_data->data, digest_out, digest_out_size);
		key_data->data_length = digest_out_size;
		*key_size = digest_out_size * 8;
		TEE_FreeOperation(digest_op);
	}

	if (key_data->data_length <= min) {
		buf = TEE_Malloc(min, TEE_MALLOC_FILL_ZERO);
		if (!buf) {
			EMSG("Failed to allocate memory for HMAC buffer");
			return KM_ERROR_MEMORY_ALLOCATION_FAILED;
		}
		TEE_MemMove(buf, key_data->data, key_data->data_length);
		key_data->data_length = min;
		TEE_Free(key_data->data);
		key_data->data = buf;
	}
	return KM_ERROR_OK;
}

keymaster_error_t TA_import_key(const keymaster_algorithm_t algorithm,
				const uint32_t key_size,
				uint8_t *key_material,
				const keymaster_digest_t digest,
				const TEE_Attribute *attrs_in,
				const uint32_t attrs_in_count)
{
	uint32_t type;
	uint32_t padding = 0;


	switch (algorithm) {
	case KM_ALGORITHM_AES:
		type = TEE_TYPE_AES;
		break;
	case KM_ALGORITHM_HMAC:
		switch (digest) {
		case KM_DIGEST_MD5:
			type = TEE_TYPE_HMAC_MD5;
			break;
		case KM_DIGEST_SHA1:
			type = TEE_TYPE_HMAC_SHA1;
			break;
		case KM_DIGEST_SHA_2_224:
			type = TEE_TYPE_HMAC_SHA224;
			break;
		case KM_DIGEST_SHA_2_256:
			type = TEE_TYPE_HMAC_SHA256;
			break;
		case KM_DIGEST_SHA_2_384:
			type = TEE_TYPE_HMAC_SHA384;
			break;
		case KM_DIGEST_SHA_2_512:
			type = TEE_TYPE_HMAC_SHA512;
			break;
		default:
			return KM_ERROR_INCOMPATIBLE_DIGEST;
		}
		break;
	case KM_ALGORITHM_RSA:
		type = TEE_TYPE_RSA_KEYPAIR;
		break;
	case KM_ALGORITHM_EC:
		type = TEE_TYPE_ECDSA_KEYPAIR;
		break;
	default:
		return KM_ERROR_UNSUPPORTED_ALGORITHM;
	}
	TEE_MemMove(key_material, &type, sizeof(type));
	padding += sizeof(type);
	TEE_MemMove(key_material + padding, &key_size, sizeof(key_size));
	padding += sizeof(key_size);
	for (uint32_t i = 0; i < attrs_in_count; i++) {
		TEE_MemMove(key_material + padding, &attrs_in[i].attributeID,
					sizeof(attrs_in[i].attributeID));
		padding += sizeof(attrs_in[i].attributeID);
		if (is_attr_value(attrs_in[i].attributeID)) {
			/* value */
			TEE_MemMove(key_material + padding,
					&attrs_in[i].content.value.a,
					sizeof(attrs_in[i].content.value.a));
			padding += sizeof(attrs_in[i].content.value.a);
			TEE_MemMove(key_material + padding,
					&attrs_in[i].content.value.b,
					sizeof(attrs_in[i].content.value.b));
			padding += sizeof(attrs_in[i].content.value.b);
		} else {
			/* buffer */
			TEE_MemMove(key_material + padding,
					&attrs_in[i].content.ref.length,
					sizeof(attrs_in[i].content.ref.length));
			padding += sizeof(attrs_in[i].content.ref.length);
			TEE_MemMove(key_material + padding,
					attrs_in[i].content.ref.buffer,
					attrs_in[i].content.ref.length);
			padding += attrs_in[i].content.ref.length;
		}
	}
	return KM_ERROR_OK;
}

uint32_t TA_get_curve_nist(const uint32_t key_size)
{
	switch (key_size) {
	case 192:
		return TEE_ECC_CURVE_NIST_P192;
	case 224:
		return TEE_ECC_CURVE_NIST_P224;
	case 256:
		return TEE_ECC_CURVE_NIST_P256;
	case 384:
		return TEE_ECC_CURVE_NIST_P384;
	case 521:
		return TEE_ECC_CURVE_NIST_P521;
	default:
		return UNDEFINED;
	}
}

keymaster_error_t TA_generate_key(const keymaster_algorithm_t algorithm,
					const uint32_t key_size,
					uint8_t *key_material,
					const keymaster_digest_t digest,
					const uint64_t rsa_public_exponent)
{
	TEE_ObjectHandle obj_h = TEE_HANDLE_NULL;
	TEE_Result res;
	uint32_t padding = 0;
	uint32_t *attributes;
	uint32_t attr_count;
	uint32_t attr_size;
	uint32_t type;
	uint32_t a;
	uint32_t b;
	uint32_t curve;
	uint8_t buffer[KM_MAX_ATTR_SIZE];
	uint8_t *buf_pe = NULL;
	uint64_t be_pe;
	TEE_Attribute *attrs_in = NULL;
	uint32_t attrs_in_count = 0;

	switch (algorithm) {
	case KM_ALGORITHM_AES:
		attributes = attributes_aes_hmac;
		attr_count = KM_ATTR_COUNT_AES_HMAC;
		type = TEE_TYPE_AES;
		break;
	case KM_ALGORITHM_HMAC:
		attributes = attributes_aes_hmac;
		attr_count = KM_ATTR_COUNT_AES_HMAC;
		switch (digest) {
		case KM_DIGEST_MD5:
			type = TEE_TYPE_HMAC_MD5;
			break;
		case KM_DIGEST_SHA1:
			type = TEE_TYPE_HMAC_SHA1;
			break;
		case KM_DIGEST_SHA_2_224:
			type = TEE_TYPE_HMAC_SHA224;
			break;
		case KM_DIGEST_SHA_2_256:
			type = TEE_TYPE_HMAC_SHA256;
			break;
		case KM_DIGEST_SHA_2_384:
			type = TEE_TYPE_HMAC_SHA384;
			break;
		case KM_DIGEST_SHA_2_512:
			type = TEE_TYPE_HMAC_SHA512;
			break;
		default:
			return KM_ERROR_UNSUPPORTED_DIGEST;
		}
		break;
	case KM_ALGORITHM_RSA:
		attributes = attributes_rsa;
		attr_count = KM_ATTR_COUNT_RSA;
		type = TEE_TYPE_RSA_KEYPAIR;
		EMSG("%s %d", __func__, __LINE__);
		attrs_in = TEE_Malloc(sizeof(TEE_Attribute),
							TEE_MALLOC_FILL_ZERO);
		if (!attrs_in) {
			EMSG("Failed to allocate memory for attributes");
			res = KM_ERROR_MEMORY_ALLOCATION_FAILED;
			goto gk_out;
		}
		attrs_in_count = 1;
		EMSG("%s %d", __func__, __LINE__);
		buf_pe = TEE_Malloc(sizeof(rsa_public_exponent),
							TEE_MALLOC_FILL_ZERO);
		if (!buf_pe) {
			EMSG("Failed to allocate memory for public exponent");
			res = KM_ERROR_MEMORY_ALLOCATION_FAILED;
			goto gk_out;
		}
		be_pe = TEE_U64_TO_BIG_ENDIAN(rsa_public_exponent);
		EMSG("%s %d", __func__, __LINE__);
		TEE_MemMove(buf_pe, &be_pe, sizeof(rsa_public_exponent));
		EMSG("%s %d", __func__, __LINE__);
		TEE_InitRefAttribute(attrs_in,
					TEE_ATTR_RSA_PUBLIC_EXPONENT,
					(void *) buf_pe,
					sizeof(rsa_public_exponent));
		EMSG("%s %d", __func__, __LINE__);
		break;
	case KM_ALGORITHM_EC:
		attributes = attributes_ec;
		attr_count = KM_ATTR_COUNT_EC;
		type = TEE_TYPE_ECDSA_KEYPAIR;
		attrs_in = TEE_Malloc(sizeof(TEE_Attribute),
							TEE_MALLOC_FILL_ZERO);
		if (!attrs_in) {
			EMSG("Failed to allocate memory for attributes");
			res = KM_ERROR_MEMORY_ALLOCATION_FAILED;
			goto gk_out;
		}
		attrs_in_count = 1;
		curve = TA_get_curve_nist(key_size);
		if (curve == UNDEFINED) {
			EMSG("Failed to get curve nist");
			res = KM_ERROR_UNSUPPORTED_KEY_SIZE;
			goto gk_out;
		}
		TEE_InitValueAttribute(attrs_in,
				TEE_ATTR_ECC_CURVE,
				curve, 0);
		break;
	default:
		return KM_ERROR_UNSUPPORTED_ALGORITHM;
	}
	EMSG("%s %d", __func__, __LINE__);
	res = TEE_AllocateTransientObject(type, key_size, &obj_h);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to allocate transient object, res=%x", res);
		goto gk_out;
	}
	EMSG("%s %d", __func__, __LINE__);
	EMSG("key_size = %u, sizeof(attrs_in) = %zu, attrs_in_count = %u",
			key_size, sizeof(attrs_in), attrs_in_count);
	res = TEE_GenerateKey(obj_h, key_size, attrs_in, attrs_in_count);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to generate key via TEE_GenerateKey, res = %x", res);
		/* Convert error code to Android style */
		if (res == TEE_ERROR_NOT_SUPPORTED)
			res = KM_ERROR_UNSUPPORTED_KEY_SIZE;
		goto gk_out;
	}

	EMSG("%s %d", __func__, __LINE__);
	TEE_MemMove(key_material, &type, sizeof(type));
	padding += sizeof(type);
	EMSG("%s %d", __func__, __LINE__);
	TEE_MemMove(key_material + padding, &key_size, sizeof(key_size));
	padding += sizeof(key_size);
	for (uint32_t i = 0; i < attr_count; i++) {
		attr_size = KM_MAX_ATTR_SIZE;
		EMSG("%s %d %u", __func__, __LINE__, i);
		TEE_MemMove(key_material + padding, attributes + i,
						sizeof(attributes[i]));
		padding += sizeof(attributes[i]);
		if (is_attr_value(attributes[i])) {
			/* value */
			EMSG("%s %d %u", __func__, __LINE__, i);
			res = TEE_GetObjectValueAttribute(obj_h,
						attributes[i], &a, &b);
			if (res != TEE_SUCCESS) {
				EMSG("Failed to get value attribute, res = %x", res);
				break;
			}
			EMSG("%s %d %u", __func__, __LINE__, i);
			TEE_MemMove(key_material + padding, &a, sizeof(a));
			padding += sizeof(a);
			EMSG("%s %d %u", __func__, __LINE__, i);
			TEE_MemMove(key_material + padding, &b, sizeof(b));
			padding += sizeof(b);
		} else {
			/* buffer */
			EMSG("%s %d %u", __func__, __LINE__, i);
			res = TEE_GetObjectBufferAttribute(obj_h,
					attributes[i], buffer, &attr_size);
			if (res != TEE_SUCCESS) {
				EMSG("Failed to get buffer attribute %x, res = %x",
								attributes[i], res);
				break;
			}
			EMSG("%s %d %u", __func__, __LINE__, i);
			TEE_MemMove(key_material + padding,
					&attr_size, sizeof(attr_size));
			padding += sizeof(attr_size);
			EMSG("%s %d %u", __func__, __LINE__, i);
			TEE_MemMove(key_material + padding, buffer, attr_size);
			padding += attr_size;
		}
	}
gk_out:
	if (obj_h != TEE_HANDLE_NULL)
		TEE_FreeTransientObject(obj_h);
	free_attrs(attrs_in, attrs_in_count);

	return res;
}

keymaster_error_t TA_check_hmac_key(const uint32_t type, uint32_t *key_size)
{
	uint32_t min = 0;
	uint32_t max = 0;

	if (key_size == 0)
		return KM_ERROR_UNSUPPORTED_KEY_SIZE;
	switch (type) {
	case TEE_TYPE_HMAC_MD5:
		min = MIN_HMAC_MD5;
		max = MAX_HMAC_MD5;
		break;
	case TEE_TYPE_HMAC_SHA1:
		min = MIN_HMAC_SHA1;
		max = MAX_HMAC_SHA1;
		break;
	case TEE_TYPE_HMAC_SHA224:
		min = MIN_HMAC_SHA224;
		max = MAX_HMAC_SHA224;
		break;
	case TEE_TYPE_HMAC_SHA256:
		min = MIN_HMAC_SHA256;
		max = MAX_HMAC_SHA256;
		break;
	case TEE_TYPE_HMAC_SHA384:
		min = MIN_HMAC_SHA384;
		max = MAX_HMAC_SHA384;
		break;
	case TEE_TYPE_HMAC_SHA512:
		min = MIN_HMAC_SHA512;
		max = MAX_HMAC_SHA512;
		break;
	default:
		return KM_ERROR_INCOMPATIBLE_DIGEST;
	}
	if (*key_size > max)
		return KM_ERROR_UNSUPPORTED_KEY_SIZE;
	if (*key_size < min)
		*key_size = min;
	return KM_ERROR_OK;
}

keymaster_error_t TA_restore_key(uint8_t *key_material,
				const keymaster_key_blob_t *key_blob,
				uint32_t *key_size, uint32_t *type,
				TEE_ObjectHandle *obj_h,
				keymaster_key_param_set_t *params_t)
{
	uint32_t padding = 0;
	uint32_t attrs_count = 0;
	uint32_t tag;
	uint32_t a;
	uint32_t b;
	uint32_t attr_size;
	uint8_t *buf;
	TEE_Attribute *attrs = NULL;
	keymaster_algorithm_t algorithm;
	keymaster_error_t res = KM_ERROR_OK;

	if (!key_material) {
		EMSG("Failed to allocate memory for key_material");
		return KM_ERROR_MEMORY_ALLOCATION_FAILED;
	}
	TEE_MemMove(key_material, key_blob->key_material,
					key_blob->key_material_size);
	res = TA_decrypt(key_material, key_blob->key_material_size);
	if (res != KM_ERROR_OK) {
		EMSG("Failed to decript key blob");
		goto out_rk;
	}
	TEE_MemMove(type, key_material, sizeof(*type));
	padding += sizeof(*type);
	switch (*type) {
	case TEE_TYPE_AES:
		attrs_count = KM_ATTR_COUNT_AES_HMAC;
		algorithm = KM_ALGORITHM_AES;
		break;
	case TEE_TYPE_RSA_KEYPAIR:
		attrs_count = KM_ATTR_COUNT_RSA;
		algorithm = KM_ALGORITHM_RSA;
		break;
	case TEE_TYPE_ECDSA_KEYPAIR:
		attrs_count = KM_ATTR_COUNT_EC;
		algorithm = KM_ALGORITHM_EC;
		break;
	default: /* HMAC */
		attrs_count = KM_ATTR_COUNT_AES_HMAC;
		algorithm = KM_ALGORITHM_HMAC;
	}
	attrs = TEE_Malloc(attrs_count * sizeof(TEE_Attribute),
						TEE_MALLOC_FILL_ZERO);
	if (!attrs) {
		EMSG("Failed to allocate memory for attributes array");
		res = KM_ERROR_MEMORY_ALLOCATION_FAILED;
		goto out_rk;
	}
	TEE_MemMove(key_size, key_material + padding, sizeof(*key_size));
	padding += sizeof(*key_size);
	for (uint32_t i = 0; i < attrs_count; i++) {
		TEE_MemMove(&tag, key_material + padding, sizeof(tag));
		padding += sizeof(tag);
		if (is_attr_value(tag)) {
			/* value */
			TEE_MemMove(&a, key_material + padding, sizeof(a));
			padding += sizeof(a);
			TEE_MemMove(&b, key_material + padding, sizeof(b));
			padding += sizeof(b);
			TEE_InitValueAttribute(attrs + i, tag, a, b);
		} else {
			/* buffer */
			TEE_MemMove(&attr_size, key_material + padding,
							sizeof(attr_size));
			padding += sizeof(attr_size);
			/* will be freed when parameters array is destroyed */
			buf = TEE_Malloc(attr_size, TEE_MALLOC_FILL_ZERO);
			if (!buf) {
				res = KM_ERROR_MEMORY_ALLOCATION_FAILED;
				EMSG("Failed to allocate memory for attribute");
				/*
				 * If error occurs, attrs_count should be equal i,
				 * because free_attrs will try to free memory for elements,
				 * which didn't allocate.
				 */
				attrs_count = i;
				goto out_rk;
			}
			TEE_MemMove(buf, key_material + padding, attr_size);
			padding += attr_size;
			TEE_InitRefAttribute(attrs + i, tag, buf, attr_size);
		}
	}
	if (algorithm == KM_ALGORITHM_HMAC) {
		res = TA_check_hmac_key(*type, key_size);
		if (res != KM_ERROR_OK) {
			EMSG("HMAC key checking failed res = %x", res);
			goto out_rk;
		}
	}
	res = TEE_AllocateTransientObject(*type, *key_size, obj_h);
	if (res != TEE_SUCCESS) {
		EMSG("Error TEE_AllocateTransientObject res = %x type = %x",
								 res, *type);
		goto out_rk;
	}
	res = TEE_PopulateTransientObject(*obj_h, attrs, attrs_count);
	if (res != TEE_SUCCESS) {
		EMSG("Error TEE_PopulateTransientObject res = %x", res);
		goto out_rk;
	}
	/* offset from array begin where parameters are stored */
	padding = TA_get_key_size(algorithm);
	TA_deserialize_param_set(key_material + padding, NULL,
						params_t, false, &res);
	if (res != KM_ERROR_OK)
		goto out_rk;
	TA_add_origin(params_t, KM_ORIGIN_UNKNOWN, false);
out_rk:
	free_attrs(attrs, attrs_count);

	return res;
}

keymaster_error_t TA_create_operation(TEE_OperationHandle *operation,
					const TEE_ObjectHandle obj_h,
					const keymaster_purpose_t purpose,
					const keymaster_algorithm_t algorithm,
					const uint32_t key_size,
					const keymaster_blob_t nonce,
					const keymaster_digest_t digest,
					const keymaster_block_mode_t op_mode,
					const keymaster_padding_t padding,
					const uint32_t mac_length)
{
	TEE_Result res = TEE_SUCCESS;
	TEE_ObjectInfo info;
	uint32_t algo;
	uint32_t mode = purpose_to_mode(purpose);

	if (mode == UINT32_MAX) {
		EMSG("Can not find mode for such purpose");
		return KM_ERROR_UNSUPPORTED_PURPOSE;
	}
	switch (algorithm) {
	case (KM_ALGORITHM_AES):
		switch (op_mode) {
		case KM_MODE_ECB:
			/* KM_PAD_PKCS7 will be done
			 * before or after operation
			 */
			algo = TEE_ALG_AES_ECB_NOPAD;
			break;
		case KM_MODE_CBC:
			/* KM_PAD_PKCS7 will be done
			 * before or after operation
			 */
			algo = TEE_ALG_AES_CBC_NOPAD;
			break;
		case KM_MODE_CTR:
			algo = TEE_ALG_AES_CTR;
			break;
		default:/* KM_MODE_GCM */
			algo = TEE_ALG_AES_GCM;
		}
		break;
	case (KM_ALGORITHM_RSA):
		switch (padding) {
		case KM_PAD_RSA_PKCS1_1_5_SIGN:
			switch (digest) {
			case KM_DIGEST_MD5:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_MD5;
				break;
			case KM_DIGEST_SHA1:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_SHA1;
				break;
			case KM_DIGEST_SHA_2_224:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_SHA224;
				break;
			case KM_DIGEST_SHA_2_256:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_SHA256;
				break;
			case KM_DIGEST_SHA_2_384:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_SHA384;
				break;
			case KM_DIGEST_SHA_2_512:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5_SHA512;
				break;
			case KM_DIGEST_NONE:
				algo = TEE_ALG_RSASSA_PKCS1_V1_5;
				break;
			default:
				EMSG("Unsupported by RSA PKCS digest");
				return KM_ERROR_UNSUPPORTED_DIGEST;
			}
			break;
		case KM_PAD_RSA_PSS:
			switch (digest) {
			case KM_DIGEST_MD5:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_MD5;
				break;
			case KM_DIGEST_SHA1:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA1;
				break;
			case KM_DIGEST_SHA_2_224:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA224;
				break;
			case KM_DIGEST_SHA_2_256:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA256;
				break;
			case KM_DIGEST_SHA_2_384:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA384;
				break;
			case KM_DIGEST_SHA_2_512:
				algo = TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA512;
				break;
			case KM_DIGEST_NONE:
				EMSG("Incompatible by RSA PSS digest");
				return KM_ERROR_INCOMPATIBLE_DIGEST;
			default:
				EMSG("Unsupported by RSA PSS digest");
				return KM_ERROR_UNSUPPORTED_DIGEST;
			}
			break;
		case KM_PAD_RSA_PKCS1_1_5_ENCRYPT:
			/* digest is not required */
			algo = TEE_ALG_RSAES_PKCS1_V1_5;
			break;
		case KM_PAD_RSA_OAEP:
			switch (digest) {
			case KM_DIGEST_MD5:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_MD5;
				break;
			case KM_DIGEST_SHA1:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA1;
				break;
			case KM_DIGEST_SHA_2_224:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA224;
				break;
			case KM_DIGEST_SHA_2_256:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA256;
				break;
			case KM_DIGEST_SHA_2_384:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA384;
				break;
			case KM_DIGEST_SHA_2_512:
				algo = TEE_ALG_RSAES_PKCS1_OAEP_MGF1_SHA512;
				break;
			default:
				EMSG("Unsupported by RSA AOEP digest");
				return KM_ERROR_UNSUPPORTED_DIGEST;
			}
			break;
		default:/* KM_PAD_NONE */
			algo = TEE_ALG_RSA_NOPAD;
			if (purpose == KM_PURPOSE_SIGN)
				mode = TEE_MODE_DECRYPT;
			else if (purpose == KM_PURPOSE_VERIFY)
				mode = TEE_MODE_ENCRYPT;
		}
		break;
	case (KM_ALGORITHM_EC):
		switch (key_size) {
		case 192:
			algo = TEE_ALG_ECDSA_P192;
			break;
		case 224:
			algo = TEE_ALG_ECDSA_P224;
			break;
		case 256:
			algo = TEE_ALG_ECDSA_P256;
			break;
		case 384:
			algo = TEE_ALG_ECDSA_P384;
			break;
		case 521:
			algo = TEE_ALG_ECDSA_P521;
			break;
		default:
			EMSG("Unsupported key size for EC");
			return KM_ERROR_UNSUPPORTED_KEY_SIZE;
		}
		break;
	case (KM_ALGORITHM_HMAC):
		switch (digest) {
		case KM_DIGEST_MD5:
			algo = TEE_ALG_HMAC_MD5;
			break;
		case KM_DIGEST_SHA1:
			algo = TEE_ALG_HMAC_SHA1;
			break;
		case KM_DIGEST_SHA_2_224:
			algo = TEE_ALG_HMAC_SHA224;
			break;
		case KM_DIGEST_SHA_2_256:
			algo = TEE_ALG_HMAC_SHA256;
			break;
		case KM_DIGEST_SHA_2_384:
			algo = TEE_ALG_HMAC_SHA384;
			break;
		case KM_DIGEST_SHA_2_512:
			algo = TEE_ALG_HMAC_SHA512;
			break;
		default:
			EMSG("Unsupported digest for HMAC key");
			return KM_ERROR_UNSUPPORTED_DIGEST;
		}
		mode = TEE_MODE_MAC;
		break;
	default:
		EMSG("Unsupported algorithm");
		return KM_ERROR_UNSUPPORTED_ALGORITHM;
	}
	TEE_GetObjectInfo1(obj_h, &info);
	res = TEE_AllocateOperation(operation, algo, mode, info.maxKeySize);
	if (res != TEE_SUCCESS) {
		EMSG("Error TEE_AllocateOperation maxKeySize=%d", info.maxKeySize);
		goto out_co;
	}
	res = TEE_SetOperationKey(*operation, obj_h);
	if (res != TEE_SUCCESS) {
		EMSG("Error TEE_SetOperationKey");
		goto out_co;
	}
	switch (algorithm) {
	case (KM_ALGORITHM_AES):
		if (op_mode == KM_MODE_GCM) {
			TEE_AEInit(*operation, nonce.data,
					nonce.data_length,
					mac_length, 0, 0);
		} else {
			TEE_CipherInit(*operation,
					nonce.data,
					nonce.data_length);
		}
		break;
	case (KM_ALGORITHM_RSA):
	case (KM_ALGORITHM_EC):
		/* Nothing to do */
		break;
	case (KM_ALGORITHM_HMAC):
		TEE_MACInit(*operation, NULL, 0);
		break;
	default:
		EMSG("Unsupported algorithm");
		res = KM_ERROR_UNSUPPORTED_ALGORITHM;
		goto out_co;
	}
out_co:
	return res;
}

keymaster_error_t TA_create_digest_op(TEE_OperationHandle *digest_op,
					const keymaster_digest_t digest)
{
	uint32_t algo;
	uint32_t res = TEE_SUCCESS;

	switch (digest) {
	case KM_DIGEST_MD5:
		algo = TEE_ALG_MD5;
		break;
	case KM_DIGEST_SHA1:
		algo = TEE_ALG_SHA1;
		break;
	case KM_DIGEST_SHA_2_224:
		algo = TEE_ALG_SHA224;
		break;
	case KM_DIGEST_SHA_2_256:
		algo = TEE_ALG_SHA256;
		break;
	case KM_DIGEST_SHA_2_384:
		algo = TEE_ALG_SHA384;
		break;
	case KM_DIGEST_SHA_2_512:
		algo = TEE_ALG_SHA512;
		break;
	case KM_DIGEST_NONE:
		return res;
	default:
		EMSG("Unsupported digest");
		return KM_ERROR_UNSUPPORTED_DIGEST;
	}
	res = TEE_AllocateOperation(digest_op, algo, TEE_MODE_DIGEST, 0);
	if (res != TEE_SUCCESS) {
		EMSG("Error on TEE_AllocateOperation (%x)", res);
		return KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
	}
	return KM_ERROR_OK;
}
