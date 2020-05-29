#ifndef BLITZ_CKSUM_H
#define BLITZ_CKSUM_H

#include <cstdlib>

namespace blitz
{
  uint32_t compute_checksum(const uint8_t *data, size_t size);
}

#endif
