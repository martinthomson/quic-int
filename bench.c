// CC0 - http://creativecommons.org/publicdomain/zero/1.0/
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)

#include <sys/param.h>
#include <winsock2.h>
#define htobe16(x) htons(x)
#define be16toh(x) ntohs(x)
#define htobe32(x) htonl(x)
#define be32toh(x) ntohl(x)
#define htobe64(x) htonll(x)
#define be64toh(x) ntohll(x)
#endif // _MSC_VER

size_t integer_count = 1000;
size_t iterations = 1000;

uint64_t *integers;
size_t integers_size;
uint8_t *buffer;
size_t buffer_size;
uint64_t *decoded;

typedef enum { counting, randomized, trimmed } mode;

// Allocate a chunk of memory for integers, an encoded form of those integers,
// and the decoded values.
bool setup(mode m) {
  integers_size = sizeof(*integers) * integer_count;
  integers = malloc(integers_size);
  if (!integers) {
    return false;
  }
  // Need more space for the UTF-8 encoding.
  buffer_size = (sizeof(*integers) + 1) * integer_count;
  buffer = malloc(buffer_size);
  if (!buffer) {
    return false;
  }
  decoded = malloc(integers_size);
  if (!decoded) {
    return false;
  }

  if (m == counting) {
    for (size_t i = 0; i < integer_count; ++i) {
      integers[i] = i;
    }
    return true;
  }

  FILE *urandom = fopen("/dev/urandom", "r");
  if (!urandom) {
    return false;
  }

  size_t done = 0;
  while (done < integer_count) {
    size_t read_count = fread(integers + done, sizeof(*integers),
                              integer_count - done, urandom);
    if (!read_count) {
      fclose(urandom);
      return false;
    }
    done += read_count;
  }
  fclose(urandom);

  // Need to trim the integer values for the various encodings.
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t trim = 2;
    if (m == trimmed) {
      trim += 64 - (8 * (1 << (integers[i] >> 62)));
    }
    integers[i] >>= trim;
  }
  return true;
}

void cleanup() {
  free(integers);
  free(buffer);
  free(decoded);
}

// Tweaked timing function from the Keccak reference implementation
static uint32_t hires_time() {
  uint32_t x[2];
#ifdef _MSC_VER
  x[0] = (uint_32t)__rdtsc();
#else
  __asm__ volatile("rdtsc" : "=a"(x[0]), "=d"(x[1]));
#endif
  return x[0];
}

uint32_t benchmark_floor = 0;
#define MEASURE(_name)                                                         \
  void _run_##_name();                                                         \
  uint32_t measure_##_name() {                                                 \
    uint32_t tmin = UINT32_MAX;                                                \
    for (size_t i = 0; i < iterations; ++i) {                                  \
      uint32_t t0 = hires_time();                                              \
      _run_##_name();                                                          \
      uint32_t t1 = hires_time();                                              \
      if (tmin > t1 - t0 - benchmark_floor) {                                  \
        tmin = t1 - t0 - benchmark_floor;                                      \
      }                                                                        \
    }                                                                          \
    return tmin;                                                               \
  }                                                                            \
  inline void _run_##_name()

MEASURE(calibrate) {}

void validate(const char *name) {
  if (memcmp(integers, decoded, sizeof(*integers) * integer_count)) {
    fprintf(stderr, "%s: decoded value doesn't match original value\n", name);
    fprintf(stderr, "Integers:");
    for (size_t i = 0; i < integer_count; ++i) {
      fprintf(stderr, " %16.16llx", integers[i]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "Encoded:");
    for (size_t i = 0; i < buffer_size; ++i) {
      fprintf(stderr, " %2.2x", buffer[i]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "Decoded:");
    for (size_t i = 0; i < integer_count; ++i) {
      fprintf(stderr, " %16.16llx", decoded[i]);
    }
    fprintf(stderr, "\n");
    exit(1);
  }
}

#define ENCODE(_name) MEASURE(encode_##_name)
#define DECODE(_name) MEASURE(decode_##_name)

// This version uses memcpy, which should be fast but not at all portable.
ENCODE(memcpy) { memcpy(buffer, integers, integers_size); }

DECODE(memcpy) { memcpy(decoded, buffer, integers_size); }

// This swaps endianness, which should at least be portable.
ENCODE(endian) {
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = htobe64(integers[i]);
    memcpy(buffer + i * sizeof(v), &v, sizeof(v));
  }
}

DECODE(endian) {
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v;
    memcpy(&v, buffer + i * sizeof(v), sizeof(v));
    decoded[i] = be64toh(v);
  }
}

ENCODE(highbitbe) {
  uint8_t *c = buffer;
  uint8_t tmp[9];
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = integers[i];
    size_t j = 56;
    while (j > 0 && !(v >> j)) {
      j -= 7;
    }
    while (j > 0) {
      *c++ = 0x80 | ((v >> j) & 0x7f);
      j -= 7;
    }
    *c++ = v & 0x7f;
  }
}

DECODE(highbitbe) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = 0;
    while (*c & 0x80) {
      v |= *c++ & 0x7f;
      v <<= 7;
    }
    decoded[i] = v | *c++;
  }
}

ENCODE(highbitle) {
  uint8_t *c = buffer;
  uint8_t tmp[9];
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = integers[i];
    *c = v & 0x7f;
    v >>= 7;
    while (v) {
      *c |= 0x80;
      ++c;
      *c = v & 0x7f;
      v >>= 7;
    }
    ++c;
  }
}

DECODE(highbitle) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = 0;
    size_t j = 0;
    while (*c & 0x80) {
      v |= (uint64_t)(*c++ & 0x7f) << j;
      j += 7;
    }
    decoded[i] = v | ((uint64_t)*c++ << j);
  }
}

ENCODE(quic) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    if (integers[i] < 0x40) {
      *c++ = integers[i];
    } else if (integers[i] < (0x40 << 8)) {
      *c++ = (1U << 6) | integers[i] >> 8;
      *c++ = integers[i] & 0xff;
    } else if (integers[i] < (0x40 << 24)) {
      uint32_t v = htobe32((2UL << 30) | integers[i]);
      memcpy(c, &v, 4);
      c += 4;
    } else {
      uint64_t v = htobe64((3ULL << 62) | integers[i]);
      memcpy(c, &v, 8);
      c += 8;
    }
  }
}

DECODE(quic) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    if (*c < 0x40) {
      decoded[i] = *c++;
    } else if (*c < 0x80) {
      decoded[i] = (*c++ & 0x3f) << 8;
      decoded[i] |= *c++;
    } else if (*c < 0xc0) {
      uint32_t v;
      memcpy(&v, c, 4);
      decoded[i] = be32toh(v) & 0x3fffffffUL;
      c += 4;
    } else {
      uint64_t v;
      memcpy(&v, c, 8);
      decoded[i] = be64toh(v) & 0x3fffffffffffffffULL;
      c += 8;
    }
  }
}

// Take the stream ID type from the low two bits of the value.
ENCODE(streamid) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = integers[i];
    uint64_t type = v & 0x3;
    v >>= 2;
    if (v < 0x10) {
      *c++ = (type << 6) | v;
    } else if (v < (0x10 << 8)) {
      *c++ = (type << 6) | 0x10 | v >> 8;
      *c++ = v & 0xff;
    } else if (v < (0x10 << 24)) {
      uint32_t v32 = htobe32((type << 30) | (2UL << 28) | v);
      memcpy(c, &v32, 4);
      c += 4;
    } else {
      uint64_t v64 = htobe64((type << 62) | (3ULL << 60) | v);
      memcpy(c, &v64, 8);
      c += 8;
    }
  }
}

DECODE(streamid) {
  uint8_t *c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint8_t type = *c >> 6;
    uint8_t s = *c & 0x30;
    if (s == 0) {
      decoded[i] = *c++ & 0xf;
    } else if (s == 0x10) {
      decoded[i] = (*c++ & 0xf) << 8;
      decoded[i] |= *c++;
    } else if (s == 0x20) {
      uint32_t v;
      memcpy(&v, c, 4);
      decoded[i] = be32toh(v) & 0xfffffffUL;
      c += 4;
    } else {
      uint64_t v;
      memcpy(&v, c, 8);
      decoded[i] = be64toh(v) & 0xfffffffffffffffULL;
      c += 8;
    }
    decoded[i] <<= 2;
    decoded[i] |= type;
  }
}

#define BENCHMARK(_name)                                                       \
  do {                                                                         \
    memset(buffer, 0, buffer_size);                                            \
    memset(decoded, 0, integers_size);                                         \
    uint32_t enc = measure_encode_##_name();                                   \
    uint32_t dec = measure_decode_##_name();                                   \
    validate(#_name);                                                          \
    printf("%-12s\t%8" PRIu32 "\t%8" PRIu32 "\n", #_name ":", enc, dec);       \
  } while (0)

void usage(const char *n) {
  fprintf(stderr, "Usage: %s [t|r|c] [#integers=%zd] [#iterations=%zd]\n",
          n, integer_count, iterations);
  exit(2);
}

int main(int argc, char **argv) {
  mode m = trimmed;
  if (argc >= 2) {
    switch (argv[1][0]) {
    case 't':
      m = trimmed;
      break;
    case 'r':
      m = randomized;
      break;
    case 'c':
      m = counting;
      break;
    default:
      usage(argv[0]);
    }
  }
  if (argc >= 3) {
    char *endptr;
    integer_count = strtoull(argv[2], &endptr, 10);
    if (endptr - argv[2] != strlen(argv[2])) {
      usage(argv[0]);
    }
  }

  if (argc >= 4) {
    char *endptr;
    iterations = strtoull(argv[3], &endptr, 10);
    if (endptr - argv[3] != strlen(argv[3])) {
      usage(argv[0]);
    }
  }

  if (!setup(m)) {
    fprintf(stderr, "Unable to setup: %d\n", errno);
    exit(1);
  }
  benchmark_floor = measure_calibrate();
  printf("Encoding and decoding %d integers over %d iterations\n",
         integer_count, iterations);
  printf("--- Type ---\t Encode \t Decode\n");
  BENCHMARK(memcpy);
  BENCHMARK(endian);
  BENCHMARK(highbitbe);
  BENCHMARK(highbitle);
  BENCHMARK(quic);
  BENCHMARK(streamid);
  cleanup();
}
