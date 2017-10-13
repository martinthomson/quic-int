// CC0 - http://creativecommons.org/publicdomain/zero/1.0/
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)

#include <winsock2.h>
#include <sys/param.h>
#define htobe16(x) htons(x)
#define be16toh(x) ntohs(x)
#define htobe32(x) htonl(x)
#define be32toh(x) ntohl(x)
#define htobe64(x) htonll(x)
#define be64toh(x) ntohll(x)
#endif // _MSC_VER

size_t integer_count = 10000;
const size_t iterations = 5000;

uint64_t* integers;
size_t integers_size;
uint8_t* buffer;
size_t buffer_size;
uint64_t* decoded;

typedef enum {
  counting, randomized, trimmed
} mode;

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

  FILE* urandom = fopen("/dev/urandom", "r");
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
#define MEASURE(_name) \
  void _run_##_name(); \
  uint32_t measure_##_name() { \
    uint32_t tmin = UINT32_MAX; \
    for (size_t i = 0; i < iterations; ++i) { \
      uint32_t t0 = hires_time(); \
      _run_##_name(); \
      uint32_t t1 = hires_time(); \
      if (tmin > t1 - t0 - benchmark_floor) { \
        tmin = t1 - t0 - benchmark_floor; \
      } \
    } \
    printf(#_name ": %d cycles\n", tmin); \
    return tmin; \
  } \
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
ENCODE(memcpy) {
  memcpy(buffer, integers, integers_size);
}

DECODE(memcpy) {
  memcpy(decoded, buffer, integers_size);
}

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
  uint8_t* c = buffer;
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
  uint8_t* c = buffer;
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
  uint8_t* c = buffer;
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
  uint8_t* c = buffer;
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

static const uint64_t quic_types[4] = {
  0ULL, 1ULL << 14, 2ULL << 30, 3ULL << 62
};

ENCODE(quic) {
  uint8_t* c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t v = integers[i];
    uint64_t s;
    if (integers[i] >> 30) {
      s = 3;
    } else if (integers[i] >> 14) {
      s = 2;
    } else if (integers[i] >> 6) {
      s = 1;
    } else {
      s = 0;
    }
    size_t l = 1 << s;
    v = htobe64(integers[i] | quic_types[s]);
    memcpy(c, ((uint8_t *)&v) + sizeof(v) - l, l);
    c += l;
  }
}

static const uint64_t quic_type_mask[4] = {
  ~0ULL, ~(1ULL << 14), ~(2ULL << 30), ~(3ULL << 62)
};

DECODE(quic) {
  uint8_t* c = buffer;
  for (size_t i = 0; i < integer_count; ++i) {
    uint64_t s = *c >> 6;
    size_t l = 1 << s;
    uint64_t v = 0;
    memcpy(((uint8_t *)&v) + sizeof(v) - l, c, l);
    decoded[i] = be64toh(v) & quic_type_mask[s];
    c += l;
  }
}

#define BENCHMARK(_name) \
   printf(#_name ": running\n"); \
   memset(buffer, 0, buffer_size); \
   memset(decoded, 0, integers_size); \
   (void)measure_encode_##_name(); \
   (void)measure_decode_##_name(); \
   validate(#_name)

int main(int argc, char** argv) {
  mode m = trimmed;
  if (argc >= 2 && !strcmp(argv[1], "-r")) {
    m = randomized;
    --argc;
    ++argv;
  }
  if (argc >= 2 && !strcmp(argv[1], "-c")) {
    m = counting;
    --argc;
    ++argv;
  }

  if (!setup(m)) {
    fprintf(stderr, "Unable to setup: %d\n", errno);
  }
  benchmark_floor = measure_calibrate();
  printf("Encoding and decoding %d integers over %d iterations\n",
         integer_count, iterations);
  BENCHMARK(memcpy);
  BENCHMARK(endian);
  BENCHMARK(highbitbe);
  BENCHMARK(highbitle);
  BENCHMARK(quic);
  cleanup();
}
