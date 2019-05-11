#include "queue.h"
#include "unilink.h"

struct header
{
  unsigned int flags : 8;
  unsigned long tag : 32;
  unsigned short type : 16;
  unsigned short version : 16;
  unsigned long size : 32;
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
  unsigned char family : 4;
  unsigned short size : 12;
  unsigned char* data;
};

struct announce
{
  unsigned char role : 8;
  unsigned char address_block_count : 8;
  struct address_block* address_blocks;
  unsigned char public_key_type : 4;
  unsigned short public_key_size : 12;
  unsigned char* public_key;
  unsigned short signature_size : 16;
  unsigned char* signature;
  unsigned char master_signature_type : 8;
  unsigned short master_signature_size : 16;
  unsigned char* master_signature;
};

enum
{
  DECODE_ANNOUNCE_OK,
  DECODE_ANNOUNCE_SIZE_TOO_SMALL,
} decode_announce_errors;

int
decode_announce(unsigned char* buf, size_t size, struct announce* out)
{
  if (size < 2) {
    return E(DECODE_ANNOUNCE_SIZE_TOO_SMALL);
  }

  /* if () */
}
