// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef FWLAB_C21_STANDALONE_FUZZ
#include <stdio.h>
#endif

#include "c21_wire.h"
#include "golden_vectors.h"

static void fwlab_c21_decode_all(const uint8_t *data, size_t size)
{
	struct fwlab_c21_request request;
	struct fwlab_c21_result result;
	struct fwlab_c21_state_snapshot state;

	(void)fwlab_c21_decode_request(data, size, &request);
	(void)fwlab_c21_decode_result(data, size, &result);
	(void)fwlab_c21_decode_state(data, size, &state);
}

static void fwlab_c21_mutate_and_decode(const unsigned char *golden,
					const uint8_t *data, size_t size,
					unsigned int salt)
{
	unsigned char mutated[FWLAB_C21_RECORD_SIZE];
	size_t mutations = size < 32 ? size : 32;
	size_t index;
	size_t offset;

	memcpy(mutated, golden, sizeof(mutated));
	for (index = 0; index < mutations; index++) {
		offset = ((size_t)data[index] + index * 17U + salt) %
			 sizeof(mutated);
		mutated[offset] ^= (unsigned char)(data[size - index - 1] |
						     1U);
	}
	fwlab_c21_decode_all(mutated, sizeof(mutated));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	fwlab_c21_decode_all(data, size);
	fwlab_c21_mutate_and_decode(fwlab_c21_request_golden, data, size, 3);
	fwlab_c21_mutate_and_decode(fwlab_c21_result_golden, data, size, 11);
	fwlab_c21_mutate_and_decode(fwlab_c21_state_golden, data, size, 23);
	return 0;
}

#ifdef FWLAB_C21_STANDALONE_FUZZ
int main(void)
{
	uint8_t data[128];
	uint32_t random_state = 0xc21a1001U;
	size_t iteration;
	size_t index;
	size_t size;

	for (iteration = 0; iteration < 10000; iteration++) {
		random_state = random_state * 1664525U + 1013904223U;
		size = random_state % (sizeof(data) + 1U);
		for (index = 0; index < size; index++) {
			random_state = random_state * 1664525U + 1013904223U;
			data[index] = (uint8_t)(random_state >> 24);
		}
		(void)LLVMFuzzerTestOneInput(data, size);
	}
	printf("C2.1 deterministic wire fuzz smoke: PASS (10000 inputs)\n");
	return 0;
}
#endif
