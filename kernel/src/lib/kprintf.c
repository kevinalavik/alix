#include <lib/kprintf.h>

#include <stdbool.h>
#include <stdint.h>
#include <dev/uart.h>

static void buf_putc(char *buf, size_t bufsz, size_t *pos, char c)
{
	if (*pos + 1 < bufsz)
		buf[*pos] = c;

	(*pos)++;
}

static void buf_put_repeat(char *buf, size_t bufsz, size_t *pos, char c,
						   size_t count)
{
	for (size_t i = 0; i < count; i++)
		buf_putc(buf, bufsz, pos, c);
}

static void buf_puts(char *buf, size_t bufsz, size_t *pos, const char *s)
{
	if (s == NULL)
		s = "(null)";

	while (*s != '\0') {
		buf_putc(buf, bufsz, pos, *s);
		s++;
	}
}

static size_t buf_strlen(const char *s)
{
	size_t len = 0;

	if (s == NULL)
		return 6;

	while (s[len] != '\0')
		len++;

	return len;
}

static size_t buf_u64_to_tmp(char *tmp, size_t tmpsz, uint64_t val,
							 unsigned base, bool upper)
{
	size_t n = 0;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	if (base < 2 || base > 16 || tmpsz == 0)
		return 0;

	if (val == 0) {
		tmp[0] = '0';
		return 1;
	}

	while (val != 0 && n < tmpsz) {
		tmp[n++] = digits[val % base];
		val /= base;
	}

	return n;
}

static void buf_put_width(char *buf, size_t bufsz, size_t *pos, size_t width,
						  size_t len, char pad)
{
	if (width > len)
		buf_put_repeat(buf, bufsz, pos, pad, width - len);
}

static void buf_putu_base(char *buf, size_t bufsz, size_t *pos, uint64_t val,
						  unsigned base, bool upper, size_t width,
						  bool zero_pad, bool left_align)
{
	char tmp[32];
	size_t n = buf_u64_to_tmp(tmp, sizeof(tmp), val, base, upper);
	size_t len = n;
	char pad = zero_pad ? '0' : ' ';

	if (n == 0)
		return;

	if (!left_align)
		buf_put_width(buf, bufsz, pos, width, len, pad);

	while (n > 0)
		buf_putc(buf, bufsz, pos, tmp[--n]);

	if (left_align)
		buf_put_width(buf, bufsz, pos, width, len, ' ');
}

static void buf_puti(char *buf, size_t bufsz, size_t *pos, int64_t val,
					 size_t width, bool zero_pad, bool left_align)
{
	uint64_t uval;
	size_t digits;
	size_t len;
	char tmp[32];
	bool negative = val < 0;

	if (negative)
		uval = (uint64_t)(-(val + 1)) + 1;
	else
		uval = (uint64_t)val;

	digits = buf_u64_to_tmp(tmp, sizeof(tmp), uval, 10, false);
	if (digits == 0)
		return;

	len = digits + (negative ? 1 : 0);

	if (left_align) {
		if (negative)
			buf_putc(buf, bufsz, pos, '-');
		while (digits > 0)
			buf_putc(buf, bufsz, pos, tmp[--digits]);
		if (width > len)
			buf_put_repeat(buf, bufsz, pos, ' ', width - len);
		return;
	}

	if (zero_pad) {
		if (negative) {
			buf_putc(buf, bufsz, pos, '-');
			if (width > len)
				buf_put_repeat(buf, bufsz, pos, '0', width - len);
		} else if (width > digits) {
			buf_put_repeat(buf, bufsz, pos, '0', width - digits);
		}
	} else {
		if (width > len)
			buf_put_repeat(buf, bufsz, pos, ' ', width - len);
		if (negative)
			buf_putc(buf, bufsz, pos, '-');
	}

	while (digits > 0)
		buf_putc(buf, bufsz, pos, tmp[--digits]);
}

int kvsnprintf(char *buf, size_t bufsz, const char *fmt, va_list ap)
{
	size_t pos = 0;

	if (bufsz == 0)
		return 0;

	if (fmt == NULL)
		fmt = "";

	while (*fmt != '\0') {
		if (*fmt != '%') {
			buf_putc(buf, bufsz, &pos, *fmt);
			fmt++;
			continue;
		}

		fmt++;

		if (*fmt == '%') {
			buf_putc(buf, bufsz, &pos, '%');
			fmt++;
			continue;
		}

		bool long_long = false;
		bool size_type = false;
		bool zero_pad = false;
		bool left_align = false;
		size_t width = 0;

		for (;;) {
			if (*fmt == '-') {
				left_align = true;
				zero_pad = false;
				fmt++;
				continue;
			}

			if (*fmt == '0' && width == 0 && !left_align) {
				zero_pad = true;
				fmt++;
				continue;
			}

			break;
		}

		while (*fmt >= '0' && *fmt <= '9') {
			width = (width * 10) + (size_t)(*fmt - '0');
			fmt++;
		}

		if (*fmt == 'l' && *(fmt + 1) == 'l') {
			long_long = true;
			fmt += 2;
		} else if (*fmt == 'z') {
			size_type = true;
			fmt++;
		}

		switch (*fmt) {
		case 's':
		{
			const char *s = va_arg(ap, const char *);
			size_t len = buf_strlen(s);

			if (!left_align)
				buf_put_width(buf, bufsz, &pos, width, len, ' ');
			buf_puts(buf, bufsz, &pos, s);
			if (left_align)
				buf_put_width(buf, bufsz, &pos, width, len, ' ');
			break;
		}

		case 'c':
		{
			char ch = (char)va_arg(ap, int);

			if (!left_align)
				buf_put_width(buf, bufsz, &pos, width, 1, ' ');
			buf_putc(buf, bufsz, &pos, ch);
			if (left_align)
				buf_put_width(buf, bufsz, &pos, width, 1, ' ');
			break;
		}

		case 'd':
		case 'i':
			if (long_long)
				buf_puti(buf, bufsz, &pos, va_arg(ap, long long), width,
						 zero_pad, left_align);
			else if (size_type)
				buf_puti(buf, bufsz, &pos, (int64_t)va_arg(ap, size_t), width,
						 zero_pad, left_align);
			else
				buf_puti(buf, bufsz, &pos, va_arg(ap, int), width, zero_pad,
						 left_align);
			break;

		case 'u':
			if (long_long)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned long long),
							  10, false, width, zero_pad, left_align);
			else if (size_type)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, size_t), 10, false,
							  width, zero_pad, left_align);
			else
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned int), 10,
							  false, width, zero_pad, left_align);
			break;

		case 'x':
			if (long_long)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned long long),
							  16, false, width, zero_pad, left_align);
			else if (size_type)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, size_t), 16, false,
							  width, zero_pad, left_align);
			else
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned int), 16,
							  false, width, zero_pad, left_align);
			break;

		case 'X':
			if (long_long)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned long long),
							  16, true, width, zero_pad, left_align);
			else if (size_type)
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, size_t), 16, true,
							  width, zero_pad, left_align);
			else
				buf_putu_base(buf, bufsz, &pos, va_arg(ap, unsigned int), 16,
							  true, width, zero_pad, left_align);
			break;

		case 'p': {
			uintptr_t ptr = (uintptr_t)va_arg(ap, void *);

			if (!left_align)
				buf_put_width(buf, bufsz, &pos, width, 2, ' ');
			buf_puts(buf, bufsz, &pos, "0x");
			buf_putu_base(buf, bufsz, &pos, ptr, 16, false, 0, false,
						  left_align);
			if (left_align)
				buf_put_width(buf, bufsz, &pos, width, 2, ' ');
			break;
		}

		default:
			buf_putc(buf, bufsz, &pos, '%');

			if (*fmt != '\0')
				buf_putc(buf, bufsz, &pos, *fmt);

			break;
		}

		if (*fmt != '\0')
			fmt++;
	}

	if (pos >= bufsz)
		buf[bufsz - 1] = '\0';
	else
		buf[pos] = '\0';

	return (int)pos;
}

int ksnprintf(char *buf, size_t bufsz, const char *fmt, ...)
{
	va_list ap;
	int written;

	va_start(ap, fmt);
	written = kvsnprintf(buf, bufsz, fmt, ap);
	va_end(ap);

	return written;
}

void vkprintf(const char *fmt, va_list ap)
{
	char buf[256];

	kvsnprintf(buf, sizeof(buf), fmt, ap);
	uart_wstr(buf);
}

void kprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vkprintf(fmt, ap);
	va_end(ap);
}
