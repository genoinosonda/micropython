#include <stdint.h>
#include <string.h>

///#include "std.h"
#include "misc.h"
#include "mpconfig.h"
#include "qstr.h"
#include "obj.h"
#include "pfenv.h"

#if MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_DOUBLE
#include <stdio.h>
#endif

#if MICROPY_ENABLE_FLOAT
#include "formatfloat.h"
#endif

#define PF_PAD_SIZE 16
static const char *pad_spaces = "                ";
static const char *pad_zeroes = "0000000000000000";

void pfenv_vstr_add_strn(void *data, const char *str, unsigned int len){
    vstr_add_strn(data, str, len);
}

int pfenv_print_strn(const pfenv_t *pfenv, const char *str, unsigned int len, int flags, char fill, int width) {
    int left_pad = 0;
    int right_pad = 0;
    int pad = width - len;
    char pad_fill[PF_PAD_SIZE];
    const char *pad_chars;

    if (!fill || fill == ' ' ) {
        pad_chars = pad_spaces;
    } else if (fill == '0') {
        pad_chars = pad_zeroes;
    } else {
        memset(pad_fill, fill, PF_PAD_SIZE);
        pad_chars = pad_fill;
    }

    if (flags & PF_FLAG_CENTER_ADJUST) {
        left_pad = pad / 2;
        right_pad = pad - left_pad;
    } else if (flags & PF_FLAG_LEFT_ADJUST) {
        right_pad = pad;
    } else {
        left_pad = pad;
    }

    if (left_pad) {
        while (left_pad > 0) {
            int p = left_pad;
            if (p > PF_PAD_SIZE)
                p = PF_PAD_SIZE;
            pfenv->print_strn(pfenv->data, pad_chars, p);
            left_pad -= p;
        }
    }
    pfenv->print_strn(pfenv->data, str, len);
    if (right_pad) {
        while (right_pad > 0) {
            int p = right_pad;
            if (p > PF_PAD_SIZE)
                p = PF_PAD_SIZE;
            pfenv->print_strn(pfenv->data, pad_chars, p);
            right_pad -= p;
        }
    }
    return len;
}

// enough room for 32 signed number
#define INT_BUF_SIZE (16)

int pfenv_print_int(const pfenv_t *pfenv, unsigned int x, int sgn, int base, int base_char, int flags, char fill, int width) {
    char sign = 0;
    if (sgn) {
        if ((int)x < 0) {
            sign = '-';
            x = -x;
        } else if (flags & PF_FLAG_SHOW_SIGN) {
            sign = '+';
        } else if (flags & PF_FLAG_SPACE_SIGN) {
            sign = ' ';
        }
    }

    char buf[INT_BUF_SIZE];
    char *b = buf + INT_BUF_SIZE;

    if (x == 0) {
        *(--b) = '0';
    } else {
        do {
            int c = x % base;
            x /= base;
            if (c >= 10) {
                c += base_char - 10;
            } else {
                c += '0';
            }
            *(--b) = c;
        } while (b > buf && x != 0);
    }

    char prefix_char = '\0';

    if (flags & PF_FLAG_SHOW_PREFIX) {
        if (base == 2) {
            prefix_char = base_char + 'b' - 'a';
        } else if (base == 8) {
            prefix_char = base_char + 'o' - 'a';
        } else if (base == 16) {
            prefix_char = base_char + 'x' - 'a';
        }
    }

    int len = 0;
    if (flags & PF_FLAG_PAD_AFTER_SIGN) {
        if (sign) {
            len += pfenv_print_strn(pfenv, &sign, 1, flags, fill, 1);
            width--;
        }
        if (prefix_char) {
            len += pfenv_print_strn(pfenv, "0", 1, flags, fill, 1);
            len += pfenv_print_strn(pfenv, &prefix_char, 1, flags, fill, 1);
            width -= 2;
        }
    } else {
        if (prefix_char && b > &buf[1]) {
            *(--b) = prefix_char;
            *(--b) = '0';
        }
        if (sign && b > buf) {
            *(--b) = sign;
        }
    }

    len += pfenv_print_strn(pfenv, b, buf + INT_BUF_SIZE - b, flags, fill, width);
    return len;
}

#if MICROPY_ENABLE_FLOAT
int pfenv_print_float(const pfenv_t *pfenv, mp_float_t f, char fmt, int flags, char fill, int width, int prec) {
    char buf[32];
    char sign = '\0';
    int chrs = 0;

    if (flags & PF_FLAG_SHOW_SIGN) {
        sign = '+';
    }
    else
    if (flags & PF_FLAG_SPACE_SIGN) {
        sign = ' ';
    }
    int len;
#if MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_FLOAT
    len = format_float(f, buf, sizeof(buf), fmt, prec, sign);
#elif MICROPY_FLOAT_IMPL == MICROPY_FLOAT_IMPL_DOUBLE
    char fmt_buf[6];
    char *fmt_s = fmt_buf;

    *fmt_s++ = '%';
    if (sign) {
        *fmt_s++ = sign;
    }
    *fmt_s++ = '.';
    *fmt_s++ = '*';
    *fmt_s++ = fmt;
    *fmt_s = '\0';

    len = snprintf(buf, sizeof(buf), fmt_buf, prec, f);
#else
#error Unknown MICROPY FLOAT IMPL
#endif
    char *s = buf;

    if ((flags & PF_FLAG_ADD_PERCENT) && (len + 1) < sizeof(buf)) {
        buf[len++] = '%';
        buf[len] = '\0';
    }

    // buf[0] < '0' returns true if the first character is space, + or -
    if ((flags & PF_FLAG_PAD_AFTER_SIGN) && buf[0] < '0') {
        // We have a sign character
        s++;
        if (*s <= '9' || (flags & PF_FLAG_PAD_NAN_INF)) {
            // We have a number, or we have a inf/nan and PAD_NAN_INF is set
            // With '{:06e}'.format(float('-inf')) you get '-00inf'
            chrs += pfenv_print_strn(pfenv, &buf[0], 1, 0, 0, 1);
            width--;
            len--;
        }
    }

    if (*s > 'A' && (flags & PF_FLAG_PAD_NAN_INF) == 0) {
        // We have one of the inf or nan variants, suppress zero fill.
        // With printf, if you use: printf("%06e", -inf) then you get "  -inf"
        // so suppress the zero fill.
        fill = ' ';
    }
    chrs += pfenv_print_strn(pfenv, s, len, flags, fill, width); 

    return chrs;
}
#endif
