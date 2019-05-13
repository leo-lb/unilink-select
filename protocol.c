#include <stdlib.h>

#include "queue.h"
#include "unilink.h"

struct header
{
  unsigned int flags;
  unsigned long tag;
  unsigned short type;
  unsigned short version;
  unsigned long size;
};

enum
{
  DECODE_HEADER_OK,
  DECODE_HEADER_SIZE_TOO_SMALL,
} decode_header_errors;

int
decode_header(unsigned char* buf, size_t size, struct header* out)
{
  if (size < 1 + 4 + 2 + 2 + 4) {
    return E(DECODE_HEADER_SIZE_TOO_SMALL);
  }

  out->flags = read_net_octet(&buf);
  out->tag = read_net_4_octets(&buf);
  out->type = read_net_2_octets(&buf);
  out->version = read_net_2_octets(&buf);
  out->size = read_net_4_octets(&buf);

  return DECODE_HEADER_OK;
}

struct address_block
{
  unsigned char family;
  unsigned short size;
  unsigned char* data;
};

struct announce
{
  unsigned char role;
  unsigned char address_block_count;
  struct address_block* address_blocks;
  unsigned char public_key_type;
  unsigned short public_key_size;
  unsigned char* public_key;
  unsigned short signature_size;
  unsigned char* signature;
  unsigned char master_signature_type;
  unsigned short master_signature_size;
  unsigned char* master_signature;
};

enum
{
  DECODE_ANNOUNCE_OK,
  DECODE_ANNOUNCE_SIZE_TOO_SMALL,
  DECODE_ANNOUNCE_ALLOC_FAILURE,
} decode_announce_errors;

int
decode_announce(unsigned char* buf, size_t size, struct announce* out)
{
  size_t octets_read = 0;

  if (size < 2) {
    return E(DECODE_ANNOUNCE_SIZE_TOO_SMALL);
  }

  out->role = read_net_octet(&buf);
  out->address_block_count = read_net_octet(&buf);

  octets_read += 2;

  out->address_blocks =
    calloc(out->address_block_count, sizeof *out->address_blocks);
  if (out->address_blocks == NULL) {
    return E(DECODE_ANNOUNCE_ALLOC_FAILURE);
  }

  for (size_t i = 0; i < out->address_block_count; ++i) {
    if (size < octets_read + 2) {
      return E(DECODE_HEADER_SIZE_TOO_SMALL);
    }

    unsigned short family_and_size = read_net_2_octets(&buf);

    octets_read += 2;

    out->address_blocks[i].family = family_and_size >> 12;
    out->address_blocks[i].size = family_and_size & ~(~0U << 12U);

    if (size < octets_read + out->address_blocks[i].size) {
      return E(DECODE_ANNOUNCE_SIZE_TOO_SMALL);
    }
  }
}
