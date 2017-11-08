/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
   Concepts and parts of this file have been contributed by Fabio Utzig,
   chvprintf() added by Brent Roman.
 */

/**
 * @file    chprintf.c
 * @brief   Mini printf-like functionality.
 *
 * @addtogroup chprintf
 * @{
 */

#include "ch.h"
#include "chprintf.h"
#include "memstreams.h"


/* UINT64_MAX is 21 characters long, UINT32_MAX is 11 characters long in base 10 */
#define MAX_FILLER 21
#define FLOAT_PRECISION 100000

static char *long_to_string_with_divisor(char *p,
                                         long num,
                                         unsigned radix,
                                         long divisor) {
  int i;
  char *q;
  long l, ll;

  l = num;
  if (divisor == 0) {
    ll = num;
  } else {
    ll = divisor;
  }

  q = p + MAX_FILLER;
  do {
    i = (int)(l % radix);
    i += '0';
    if (i > '9')
      i += 'A' - '0' - 10;
    *--q = i;
    l /= radix;
  } while ((ll /= radix) != 0);

  i = (int)(p + MAX_FILLER - q);
  do
    *p++ = *q++;
  while (--i);

  return p;
}
static char *ltoa(char *p, long num, unsigned radix) {

  return long_to_string_with_divisor(p, num, radix, 0);
}


static char * unsigned_long_long_to_string_with_divisor(char *p,
                                         unsigned long long num,
                                         unsigned radix,
                                         unsigned long divisor) {
  long int i;
  char *q;
  unsigned long long l, ll;

  l = num;
  if (divisor == 0) {
    ll = num;
  } else {
    ll = divisor;
  }

  q = p + MAX_FILLER;
  do {
    i = (unsigned long int)(l % radix);
    i += '0';
    if (i > '9')
      i += 'A' - '0' - 10;
    *--q = i;
    l /= radix;
  } while ((ll /= radix) != 0);

  i = (unsigned long int)(p + MAX_FILLER - q);
  do
    *p++ = *q++;
  while (--i);

  return p;
}

static char *ulltoa(char *p, unsigned long long num, unsigned radix) {

  return unsigned_long_long_to_string_with_divisor(p, num, radix, 0);
}

#if CHPRINTF_USE_FLOAT
static char *ftoa(char *p, double num, const unsigned long precision_param) {
  long l;
  unsigned long precision = FLOAT_PRECISION;
  if( precision_param > 0 ) {
    precision = 1;
    for(int i = 0; i < precision_param; i++ ) {
      precision *= 10;
    }
  }

  l = (long)num;
  p = long_to_string_with_divisor(p, l, 10, 0);
  *p++ = '.';
  l = (long)((num - l) * precision);
  return long_to_string_with_divisor(p, l, 10, precision / 10);
}
#endif



/**
 * @brief   System formatted output function.
 * @details This function implements a minimal @p vprintf()-like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>p</b> pointer, prefixed with 0x, and the hex address printed
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *          .
 *
 * @param[in] chp       pointer to a @p BaseSequentialStream implementing object
 * @param[in] fmt       formatting string
 * @param[in] ap        list of parameters
 *
 * @api
 */
void chvprintf(BaseSequentialStream *chp, const char *fmt, va_list ap) {
  if( chp == NULL ) {
    return;
  }
  char *p, *s, c, filler;
  int i, precision = 0, width;
  bool_t is_long, left_align;
  bool_t is_long_long = FALSE;
  long l;
  long long ll;
  unsigned long long ull;
#if CHPRINTF_USE_FLOAT
  float f;
  char tmpbuf[2*MAX_FILLER + 1];
#else
  char tmpbuf[MAX_FILLER + 1];
#endif

  while (TRUE) {
    c = *fmt++;
    if (c == 0)
      return;
    if (c != '%') {
      chSequentialStreamPut(chp, (uint8_t)c);
      continue;
    }
    p = tmpbuf;
    s = tmpbuf;
    left_align = FALSE;
    if (*fmt == '-') {
      fmt++;
      left_align = TRUE;
    }
    filler = ' ';
    if ((*fmt == '0')) {
      fmt++;
      filler = '0';
    }
    width = 0;
    while (TRUE) {
      c = *fmt++;
      if (c >= '0' && c <= '9')
        c -= '0';
      else if (c == '*')
        c = va_arg(ap, int);
      else
        break;
      width = width * 10 + c;
    }
    precision = 0;
    if (c == '.') {
      while (TRUE) {
        c = *fmt++;
        if (c >= '0' && c <= '9')
          c -= '0';
        else if (c == '*')
          c = va_arg(ap, int);
        else
          break;
        precision *= 10;
        precision += c;
      }
    }
    /* Long modifier.*/
    if (c == 'l' || c == 'L') {
      is_long = TRUE;
      if (*fmt) {
        c = *fmt++;

        if (c == 'l' || c == 'L') {
          is_long_long = TRUE;
          if (*fmt)
            c = *fmt++;
        }
      }
    }
    else
      is_long = (c >= 'A') && (c <= 'Z');

    /* Command decoding.*/
    switch (c) {
    case 'c':
      filler = ' ';
      *p++ = va_arg(ap, int);
      break;
    case 's':
      filler = ' ';
      if ((s = va_arg(ap, char *)) == NULL)
        s = "(null)";
      if (precision == 0)
        precision = 32767;
      for (p = s; *p && (--precision >= 0); p++)
        ;
      break;
    case 'D':
    case 'd':
    case 'I':
    case 'i':
      if (is_long_long)
        ll = va_arg(ap, long long);
      else if (is_long)
        l = va_arg(ap, long);
      else
        l = va_arg(ap, int);

      if (is_long_long) {
        if (ll < 0) {
          *p++ = '-';
          if( ll == INT64_MIN ) {
            ull = (-(ll + ((long long ) 1))) + ((unsigned long long ) 1);
          } else {
            ull = -ll;
          }
        } else {
          ull = ll;
        }
        p = ulltoa(p, ull, 10);
      } else {
        if (l < 0) {
          *p++ = '-';
          l = -l;
        }
        p = ltoa(p, l, 10);
      }
      break;
#if CHPRINTF_USE_FLOAT
    case 'f':
      f = (float) va_arg(ap, double);
      if (f < 0) {
        *p++ = '-';
        f = -f;
      } else if( ! left_align ) {
        *p++ = ' ';
      }
      p = ftoa(p, f, precision);
      break;
#endif
    case 'p':
      /* Pointer */
      filler = '0';
      chSequentialStreamPut(chp, '0');
      chSequentialStreamPut(chp, 'x');
      c = 16;
      width = 2 * sizeof(void*);
      goto unsigned_common;
    case 'X':
    case 'x':
      c = 16;
      goto unsigned_common;
    case 'U':
    case 'u':
      c = 10;
      goto unsigned_common;
    case 'O':
    case 'o':
      c = 8;
unsigned_common:
      if (is_long_long)
        ull = va_arg(ap, unsigned long long);
      else if (is_long)
        l = va_arg(ap, unsigned long);
      else
        l = va_arg(ap, unsigned int);

      if( is_long_long ) {
        p = ulltoa(p, ull, c);
      } else {
        p = ltoa(p, l, c);
      }
      break;
    default:
      *p++ = c;
      break;
    }
    i = (int)(p - s);
    if ((width -= i) < 0)
      width = 0;
    if (left_align == FALSE)
      width = -width;
    if (width < 0) {
      if (*s == '-' && filler == '0') {
        chSequentialStreamPut(chp, (uint8_t)*s++);
        i--;
      }
      do {
        chSequentialStreamPut(chp, (uint8_t)filler);
      } while (++width != 0);
    }
    while (--i >= 0)
      chSequentialStreamPut(chp, (uint8_t)*s++);

    while (width) {
      chSequentialStreamPut(chp, (uint8_t)filler);
      width--;
    }
  }
}


/**
 * @brief   System formatted output function.
 * @details This function implements a minimal @p vprintf()-like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *          .
 *
 * @param[in] str       pointer to a buffer
 * @param[in] size      maximum size of the buffer
 * @param[in] fmt       formatting string
 * @return              The size of the generated string.
 *
 * @api
 */
int chsnprintf(char *str, size_t size, const char *fmt, ...) {
  va_list ap;
  MemoryStream ms;
  BaseSequentialStream *chp;

  /* Memory stream object to be used as a string writer.*/
  msObjectInit(&ms, (uint8_t *)str, size, 0);

  /* Performing the print operation using the common code.*/
  chp = (BaseSequentialStream *)&ms;
  va_start(ap, fmt);
  chvprintf(chp, fmt, ap);
  va_end(ap);

  /* Final zero and size return.*/
  chSequentialStreamPut(chp, 0);
  return ms.eos - 1;
}

/** @} */
