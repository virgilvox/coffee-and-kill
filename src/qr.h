/* qr.h: a QR code (byte mode, level M, versions 1 to 15) as a module grid */
#ifndef CK_QR_H
#define CK_QR_H
#include <stdint.h>
#define QR_MAX 77   /* version 15 */
/* fills out (n * n, 1 = dark, row major) and returns n, or 0 if text is too long */
int qr_make(const char *text, uint8_t *out, int cap);
#endif
