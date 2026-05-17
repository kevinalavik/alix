#include <lib/string.h>

typedef size_t word_t __attribute__((__may_alias__));

#define WORD_BYTES ((size_t)sizeof(word_t))

static inline word_t repeat_byte(uint8_t c)
{
	word_t out = c;

	for (size_t i = 8; i < WORD_BYTES * 8; i <<= 1)
		out |= out << i;

	return out;
}

static inline _Bool has_zero_byte(word_t x)
{
	word_t ones = repeat_byte(0x01);
	word_t highs = repeat_byte(0x80);

	return ((x - ones) & ~x & highs) != 0;
}

size_t strlen(const char *s)
{
	const char *start = s;

	if (s == NULL)
		return 0;

	while (((uintptr_t)s & (WORD_BYTES - 1)) != 0) {
		if (*s == '\0')
			return (size_t)(s - start);
		s++;
	}

	for (;;) {
		word_t chunk = *(const word_t *)s;
		if (has_zero_byte(chunk))
			break;
		s += WORD_BYTES;
	}

	while (*s != '\0')
		s++;

	return (size_t)(s - start);
}

size_t strlcpy(char *dst, const char *src, size_t dstsz)
{
	size_t len;
	size_t copy;

	if (src == NULL)
		src = "";

	len = strlen(src);
	if (dstsz == 0)
		return len;

	copy = len;
	if (copy >= dstsz)
		copy = dstsz - 1;

	if (copy != 0)
		memcpy(dst, src, copy);
	dst[copy] = '\0';
	return len;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
	uint8_t *d = dest;
	const uint8_t *s = src;

	while (n != 0 && (((uintptr_t)d | (uintptr_t)s) & (WORD_BYTES - 1)) != 0) {
		*d++ = *s++;
		n--;
	}

	while (n >= WORD_BYTES) {
		*(word_t *)d = *(const word_t *)s;
		d += WORD_BYTES;
		s += WORD_BYTES;
		n -= WORD_BYTES;
	}

	while (n != 0) {
		*d++ = *s++;
		n--;
	}

	return dest;
}

void *memset(void *s, int c, size_t n)
{
	uint8_t *p = s;
	word_t fill = repeat_byte((uint8_t)c);

	while (n != 0 && ((uintptr_t)p & (WORD_BYTES - 1)) != 0) {
		*p++ = (uint8_t)c;
		n--;
	}

	while (n >= WORD_BYTES) {
		*(word_t *)p = fill;
		p += WORD_BYTES;
		n -= WORD_BYTES;
	}

	while (n != 0) {
		*p++ = (uint8_t)c;
		n--;
	}

	return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
	uint8_t *d = dest;
	const uint8_t *s = src;

	if (d == s || n == 0)
		return dest;

	if ((uintptr_t)d < (uintptr_t)s || (uintptr_t)d >= (uintptr_t)s + n)
		return memcpy(dest, src, n);

	d += n;
	s += n;

	while (n != 0 && (((uintptr_t)d | (uintptr_t)s) & (WORD_BYTES - 1)) != 0) {
		*--d = *--s;
		n--;
	}

	while (n >= WORD_BYTES) {
		d -= WORD_BYTES;
		s -= WORD_BYTES;
		*(word_t *)d = *(const word_t *)s;
		n -= WORD_BYTES;
	}

	while (n != 0) {
		*--d = *--s;
		n--;
	}

	return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
	const uint8_t *p1 = s1;
	const uint8_t *p2 = s2;

	while (n != 0 && (((uintptr_t)p1 | (uintptr_t)p2) & (WORD_BYTES - 1)) != 0) {
		if (*p1 != *p2)
			return *p1 < *p2 ? -1 : 1;
		p1++;
		p2++;
		n--;
	}

	while (n >= WORD_BYTES) {
		word_t a = *(const word_t *)p1;
		word_t b = *(const word_t *)p2;

		if (a != b) {
			for (size_t i = 0; i < WORD_BYTES; i++) {
				if (p1[i] != p2[i])
					return p1[i] < p2[i] ? -1 : 1;
			}
		}

		p1 += WORD_BYTES;
		p2 += WORD_BYTES;
		n -= WORD_BYTES;
	}

	while (n != 0) {
		if (*p1 != *p2)
			return *p1 < *p2 ? -1 : 1;
		p1++;
		p2++;
		n--;
	}

	return 0;
}
