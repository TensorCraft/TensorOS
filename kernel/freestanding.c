#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t count) {
  unsigned char *dst = (unsigned char *)dest;
  const unsigned char *source = (const unsigned char *)src;

  for (size_t index = 0; index < count; ++index) {
    dst[index] = source[index];
  }
  return dest;
}

void *memset(void *dest, int value, size_t count) {
  unsigned char *dst = (unsigned char *)dest;

  for (size_t index = 0; index < count; ++index) {
    dst[index] = (unsigned char)value;
  }
  return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
  unsigned char *dst = (unsigned char *)dest;
  const unsigned char *source = (const unsigned char *)src;

  if (dst == source || count == 0u) {
    return dest;
  }

  if (dst < source) {
    for (size_t index = 0; index < count; ++index) {
      dst[index] = source[index];
    }
    return dest;
  }

  for (size_t index = count; index > 0u; --index) {
    dst[index - 1u] = source[index - 1u];
  }
  return dest;
}
