/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_test_support.h"

#include <string.h>

static enum c34_file_io_result memory_size(void *context, uint64_t *size)
{
    struct c34f_memory_substrate *substrate = context;

    if (substrate == NULL || size == NULL) {
        return C34_FILE_IO_FAILURE;
    }
    *size = substrate->size;
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result memory_resize(void *context, uint64_t size)
{
    struct c34f_memory_substrate *substrate = context;

    if (substrate == NULL || size > C34_FILE_IMAGE_BYTES) {
        return C34_FILE_IO_FAILURE;
    }
    substrate->size = size;
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result memory_read(
    void *context,
    uint64_t offset,
    uint8_t *bytes,
    size_t length
)
{
    struct c34f_memory_substrate *substrate = context;

    if (substrate == NULL || bytes == NULL || offset > substrate->size ||
        length > substrate->size - offset) {
        return C34_FILE_IO_FAILURE;
    }
    memcpy(bytes, &substrate->working[offset], length);
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result memory_write(
    void *context,
    uint64_t offset,
    const uint8_t *bytes,
    size_t length
)
{
    struct c34f_memory_substrate *substrate = context;

    if (substrate == NULL || bytes == NULL || offset > substrate->size ||
        length > substrate->size - offset) {
        return C34_FILE_IO_FAILURE;
    }
    memcpy(&substrate->working[offset], bytes, length);
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result memory_barrier(void *context)
{
    struct c34f_memory_substrate *substrate = context;

    if (substrate == NULL) {
        return C34_FILE_IO_FAILURE;
    }
    memcpy(substrate->stable, substrate->working, substrate->size);
    ++substrate->barriers;
    return substrate->cut_after_barrier != 0 &&
                   substrate->barriers == substrate->cut_after_barrier ?
               C34_FILE_IO_CUT : C34_FILE_IO_OK;
}

static const struct c34_file_substrate_ops memory_ops = {
    .version = C34_FILE_FORMAT_VERSION,
    .size = sizeof(struct c34_file_substrate_ops),
    .reserved = 0,
    .size_bytes = memory_size,
    .resize = memory_resize,
    .read = memory_read,
    .write = memory_write,
    .barrier = memory_barrier,
};

void c34f_memory_substrate_init(struct c34f_memory_substrate *substrate)
{
    memset(substrate, 0, sizeof(*substrate));
}

struct c34_file_substrate c34f_memory_substrate_provider(
    struct c34f_memory_substrate *substrate
)
{
    struct c34_file_substrate provider;

    provider.ops = &memory_ops;
    provider.context = substrate;
    return provider;
}

void c34f_memory_substrate_restart_image(
    const struct c34f_memory_substrate *source,
    struct c34f_memory_substrate *restart
)
{
    memset(restart, 0, sizeof(*restart));
    restart->size = source->size;
    memcpy(restart->working, source->stable, source->size);
    memcpy(restart->stable, source->stable, source->size);
}

uint64_t c34f_test_payload_digest(
    const uint8_t main[C34F_MAIN_BYTES],
    const uint8_t oob[C34F_OOB_BYTES]
)
{
    uint64_t hash = c34f_hash_bytes(UINT64_C(1469598103934665603), main,
                                    C34F_MAIN_BYTES);

    return c34f_hash_bytes(hash, oob, C34F_OOB_BYTES);
}

int c34f_test_bind_program(
    struct c34_file_media *media,
    uint64_t identity,
    struct fwlab_nfc_ppa ppa,
    const uint8_t main[C34F_MAIN_BYTES],
    const uint8_t oob[C34F_OOB_BYTES],
    struct fwlab_nfc_operation_token *inner
)
{
    struct c34_physical_txn_provider provider =
        c34_file_txn_provider(media);
    struct c34_physical_binding binding;

    memset(&binding, 0, sizeof(binding));
    binding.version = C34_PHYSICAL_TXN_VERSION;
    binding.size = sizeof(binding);
    binding.physical_op_id = identity;
    binding.commit_sequence = identity;
    binding.inner.instance_nonce = UINT64_C(0xc34f0000);
    binding.inner.operation_uid = identity;
    binding.inner.controller_epoch = 1;
    binding.inner.generation = (uint32_t)identity;
    binding.outer.command.instance_nonce = UINT64_C(0xc34f0000);
    binding.outer.command.command_uid = identity;
    binding.outer.command.controller_epoch = 1;
    binding.outer.command.slot_generation = 1;
    binding.mutation.word[0] = identity;
    binding.mutation.word[1] = identity ^ UINT64_C(0x55aa);
    binding.ppa = ppa;
    binding.payload_digest = c34f_test_payload_digest(main, oob);
    binding.main_length = C34F_MAIN_BYTES;
    binding.oob_length = C34F_OOB_BYTES;
    binding.operation_kind = FWLAB_NFC_PROGRAM_EXECUTE;
    *inner = binding.inner;
    return provider.ops->bind(provider.context, &binding) ==
           C34_PHYSICAL_TXN_OK;
}

int c34f_test_program_page(
    struct c34_file_media *media,
    uint64_t identity,
    uint8_t fill,
    struct c34_physical_receipt *receipt
)
{
    struct fwlab_nand_media nand = c34_file_nand_media(media);
    struct c34_physical_txn_provider physical =
        c34_file_txn_provider(media);
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nand_media_result result;
    struct fwlab_nfc_ppa ppa;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];

    memset(&ppa, 0, sizeof(ppa));
    memset(main, fill, sizeof(main));
    memset(oob, (uint8_t)(fill ^ 0xffu), sizeof(oob));
    return c34f_test_bind_program(media, identity, ppa, main, oob, &inner) &&
           nand.ops->program(
               nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
               sizeof(main), sizeof(oob), FWLAB_NFC_INTEGRITY_COMPLETE,
               &result) == FWLAB_NFC_API_OK &&
           physical.ops->receipt(physical.context, &inner, receipt) ==
               C34_PHYSICAL_TXN_OK;
}
