#include "stdio.h"
#include "../syscall.h"
#include "string.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static char printf_buf[1024];
typedef long ssize_t;

int putchar(int c) {
    char ch = (char)c;
    printf_buf[0] = ch;
    printf_buf[1] = '\0';
    sys_write(printf_buf);
    return c;
}

int puts(const char *s) {
    sys_write(s);
    sys_write("\n");
    return 0;
}

/* ===== internal formatting core ===== */

#define FLAGS_LEFT   (1 << 0)  /* '-' */
#define FLAGS_PLUS   (1 << 1)  /* '+' */
#define FLAGS_SPACE  (1 << 2)  /* ' ' */
#define FLAGS_ZERO   (1 << 3)  /* '0' */
#define FLAGS_ALT    (1 << 4)  /* '#' */

typedef enum {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_Z
} length_t;

static void buf_putc(char *buf, size_t size, size_t *pos, char c) {
    if (*pos + 1 < size) {
        buf[*pos] = c;
    }
    (*pos)++;
}

static void buf_puts_n(char *buf, size_t size, size_t *pos, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        buf_putc(buf, size, pos, s[i]);
    }
}

/* convert unsigned integer to base 'base', digits reversed in tmp[]; returns length */
static size_t utoa_unsigned(unsigned long long val,
                            unsigned int base,
                            int uppercase,
                            char *tmp)
{
    const char *digits_l = "0123456789abcdef";
    const char *digits_u = "0123456789ABCDEF";
    const char *digits   = uppercase ? digits_u : digits_l;

    size_t i = 0;
    if (val == 0) {
        tmp[i++] = '0';
        return i;
    }

    while (val) {
        tmp[i++] = digits[val % base];
        val /= base;
    }

    return i; /* digits are in reverse order */
}

static unsigned long long get_unsigned_arg(va_list *ap, length_t len) {
    switch (len) {
        case LEN_HH: return (unsigned char)va_arg(*ap, unsigned int);
        case LEN_H:  return (unsigned short)va_arg(*ap, unsigned int);
        case LEN_L:  return (unsigned long)va_arg(*ap, unsigned long);
        case LEN_LL: return (unsigned long long)va_arg(*ap, unsigned long long);
        case LEN_Z:  return (size_t)va_arg(*ap, size_t);
        case LEN_NONE:
        default:     return (unsigned int)va_arg(*ap, unsigned int);
    }
}

static long long get_signed_arg(va_list *ap, length_t len) {
    switch (len) {
        case LEN_HH: return (signed char)va_arg(*ap, int);
        case LEN_H:  return (short)va_arg(*ap, int);
        case LEN_L:  return (long)va_arg(*ap, long);
        case LEN_LL: return (long long)va_arg(*ap, long long);
        case LEN_Z:  return (ssize_t)va_arg(*ap, ssize_t);
        case LEN_NONE:
        default:     return (int)va_arg(*ap, int);
    }
}

/* Returns number of chars that WOULD have been written (like snprintf) */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;

    if (!buf || size == 0) {
        buf = (char *)"";
        size = 1;
    }

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            buf_putc(buf, size, &pos, *p);
            continue;
        }

        p++; /* skip '%' */

        /* flags */
        int flags = 0;
        int parsing = 1;
        while (parsing) {
            switch (*p) {
                case '-': flags |= FLAGS_LEFT;  p++; break;
                case '+': flags |= FLAGS_PLUS;  p++; break;
                case ' ': flags |= FLAGS_SPACE; p++; break;
                case '0': flags |= FLAGS_ZERO;  p++; break;
                case '#': flags |= FLAGS_ALT;   p++; break;
                default:  parsing = 0;          break;
            }
        }
        if (flags & FLAGS_LEFT) {
            flags &= ~FLAGS_ZERO;   /* left-align kills zero pad */
        }

        /* width */
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                width = -width;
                flags |= FLAGS_LEFT;
                flags &= ~FLAGS_ZERO;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }

        /* precision */
        int precision = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
            } else {
                precision = 0;
                while (*p >= '0' && *p <= '9') {
                    precision = precision * 10 + (*p - '0');
                    p++;
                }
            }
            if (precision < 0) precision = 0;
        }

        /* length */
        length_t len = LEN_NONE;
        if (*p == 'h') {
            if (*(p + 1) == 'h') {
                len = LEN_HH;
                p += 2;
            } else {
                len = LEN_H;
                p++;
            }
        } else if (*p == 'l') {
            if (*(p + 1) == 'l') {
                len = LEN_LL;
                p += 2;
            } else {
                len = LEN_L;
                p++;
            }
        } else if (*p == 'z') {
            len = LEN_Z;
            p++;
        }

        /* specifier */
        char spec = *p;
        if (!spec) break;

        if (spec == 'd' || spec == 'i' || spec == 'u' ||
            spec == 'x' || spec == 'X' || spec == 'o' || spec == 'p') {

            int is_signed = (spec == 'd' || spec == 'i');
            int base = 10;
            int uppercase = 0;

            if (spec == 'x' || spec == 'X' || spec == 'p') base = 16;
            if (spec == 'X') uppercase = 1;
            if (spec == 'o') base = 8;

            unsigned long long uval = 0;
            long long sval = 0;
            int negative = 0;

            if (spec == 'p') {
                void *ptr = va_arg(ap, void *);
                uval = (unsigned long long)(uintptr_t)ptr;
                flags |= FLAGS_ALT;
                base = 16;
            } else if (is_signed) {
                sval = get_signed_arg(&ap, len);
                if (sval < 0) {
                    negative = 1;
                    uval = (unsigned long long)(-sval);
                } else {
                    uval = (unsigned long long)sval;
                }
            } else {
                uval = get_unsigned_arg(&ap, len);
            }

            char tmp[64];
            size_t num_len = 0;

            if (precision == 0 && uval == 0) {
                num_len = 0;
            } else {
                num_len = utoa_unsigned(uval, base, uppercase, tmp);
            }

            size_t prec_zeros = 0;
            if (precision > 0 && (size_t)precision > num_len) {
                prec_zeros = (size_t)precision - num_len;
            }

            char sign_char = 0;
            if (is_signed) {
                if (negative) {
                    sign_char = '-';
                } else if (flags & FLAGS_PLUS) {
                    sign_char = '+';
                } else if (flags & FLAGS_SPACE) {
                    sign_char = ' ';
                }
            }

            const char *prefix = "";
            size_t prefix_len = 0;
            if (flags & FLAGS_ALT) {
                if (base == 16 && uval != 0) {
                    prefix = uppercase ? "0X" : "0x";
                    prefix_len = 2;
                } else if (base == 8 && num_len > 0 && tmp[0] != '0') {
                    prefix = "0";
                    prefix_len = 1;
                } else if (spec == 'p') {
                    prefix = "0x";
                    prefix_len = 2;
                }
            } else if (spec == 'p') {
                prefix = "0x";
                prefix_len = 2;
            }

            size_t total_num_len = num_len + prec_zeros;
            size_t total_len = total_num_len + prefix_len + (sign_char ? 1 : 0);

            char pad_char = ' ';
            if ((flags & FLAGS_ZERO) && !(flags & FLAGS_LEFT) && precision < 0) {
                pad_char = '0';
            }

            int pad = 0;
            if (width > (int)total_len) {
                pad = width - (int)total_len;
            }

            /* left space padding */
            if (!(flags & FLAGS_LEFT) && pad > 0 && pad_char == ' ') {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

            /* sign */
            if (sign_char) {
                buf_putc(buf, size, &pos, sign_char);
            }

            /* prefix */
            if (prefix_len) {
                buf_puts_n(buf, size, &pos, prefix, prefix_len);
            }

            /* zero padding from width */
            if (!(flags & FLAGS_LEFT) && pad > 0 && pad_char == '0') {
                while (pad-- > 0) buf_putc(buf, size, &pos, '0');
            }

            /* precision zeros */
            while (prec_zeros-- > 0) {
                buf_putc(buf, size, &pos, '0');
            }

            /* digits (tmp is reversed) */
            for (size_t i = 0; i < num_len; i++) {
                buf_putc(buf, size, &pos, tmp[num_len - 1 - i]);
            }

            /* right padding */
            if ((flags & FLAGS_LEFT) && pad > 0) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

        } else if (spec == 'f') {
            /* floating point: %f only, no sci-notation, no hex-floats, we're not a glibc clone */
            double v = va_arg(ap, double);

            if (precision < 0) {
                precision = 6; /* default for %f */
            }

            int negative = 0;
            if (v < 0.0) {
                negative = 1;
                v = -v;
            }

            /* rounding: add 0.5 * 10^-precision */
            double rounding = 0.5;
            for (int d = 0; d < precision; d++) {
                rounding /= 10.0;
            }
            v += rounding;

            unsigned long long whole = (unsigned long long)v;
            double frac = v - (double)whole;

            /* integer part */
            char int_buf[64];
            size_t int_len = 0;
            if (whole == 0) {
                int_buf[int_len++] = '0';
            } else {
                while (whole && int_len < sizeof(int_buf)) {
                    int_buf[int_len++] = (char)('0' + (whole % 10ULL));
                    whole /= 10ULL;
                }
            }

            /* sign */
            char sign_char = 0;
            if (negative) {
                sign_char = '-';
            } else if (flags & FLAGS_PLUS) {
                sign_char = '+';
            } else if (flags & FLAGS_SPACE) {
                sign_char = ' ';
            }

            int have_dot = 0;
            if (precision > 0) {
                have_dot = 1;
            } else if ((flags & FLAGS_ALT) && precision == 0) {
                have_dot = 1; /* %#.0f prints trailing '.' */
            }

            size_t frac_len = (precision > 0) ? (size_t)precision : 0;
            size_t total_len = int_len + frac_len + (have_dot ? 1 : 0) + (sign_char ? 1 : 0);

            char pad_char = ' ';
            if ((flags & FLAGS_ZERO) && !(flags & FLAGS_LEFT)) {
                pad_char = '0';
            }

            int pad = 0;
            if (width > (int)total_len) {
                pad = width - (int)total_len;
            }

            /* left padding with spaces */
            if (!(flags & FLAGS_LEFT) && pad > 0 && pad_char == ' ') {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

            /* sign */
            if (sign_char) {
                buf_putc(buf, size, &pos, sign_char);
            }

            /* left padding with zeros (after sign) */
            if (!(flags & FLAGS_LEFT) && pad > 0 && pad_char == '0') {
                while (pad-- > 0) buf_putc(buf, size, &pos, '0');
            }

            /* integer part (reverse int_buf) */
            for (size_t i = 0; i < int_len; i++) {
                buf_putc(buf, size, &pos, int_buf[int_len - 1 - i]);
            }

            if (have_dot) {
                buf_putc(buf, size, &pos, '.');

                for (int d = 0; d < precision; d++) {
                    frac *= 10.0;
                    int digit = (int)frac;
                    if (digit < 0) digit = 0;
                    if (digit > 9) digit = 9;
                    buf_putc(buf, size, &pos, (char)('0' + digit));
                    frac -= digit;
                }
            }

            /* right padding */
            if ((flags & FLAGS_LEFT) && pad > 0) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

        } else if (spec == 'c') {
            int ch = va_arg(ap, int);
            int pad = (width > 1) ? (width - 1) : 0;

            if (!(flags & FLAGS_LEFT)) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

            buf_putc(buf, size, &pos, (char)ch);

            if (flags & FLAGS_LEFT) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

        } else if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";

            size_t len_s = strlen(s);
            if (precision >= 0 && (size_t)precision < len_s) {
                len_s = (size_t)precision;
            }

            int pad = 0;
            if (width > (int)len_s) {
                pad = width - (int)len_s;
            }

            if (!(flags & FLAGS_LEFT)) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

            buf_puts_n(buf, size, &pos, s, len_s);

            if (flags & FLAGS_LEFT) {
                while (pad-- > 0) buf_putc(buf, size, &pos, ' ');
            }

        } else if (spec == '%') {
            buf_putc(buf, size, &pos, '%');
        } else {
            /* unknown specifier: print literally */
            buf_putc(buf, size, &pos, '%');
            buf_putc(buf, size, &pos, spec);
        }
    }

    if (size > 0) {
        size_t term = (pos < size) ? pos : (size - 1);
        buf[term] = '\0';
    }

    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    int len = vsnprintf(printf_buf, sizeof(printf_buf), fmt, ap);
    printf_buf[sizeof(printf_buf) - 1] = '\0';
    sys_write(printf_buf);
    return len;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(printf_buf, sizeof(printf_buf), fmt, ap);
    va_end(ap);

    printf_buf[sizeof(printf_buf) - 1] = '\0';
    sys_write(printf_buf);
    return len;
}