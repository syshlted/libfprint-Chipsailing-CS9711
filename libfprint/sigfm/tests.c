// SIGFM algorithm for libfprint
//
// Unit tests for the C implementation: extraction, matching, copying and the
// storage format, including rejection of malformed stored data.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "sigfm.h"
#include "tests-embedded.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The capture is 256x256; most checks use a 128x128 crop, because matching
 * cost grows steeply with the number of keypoints. */
#define CAP_W 256
#define W 128
#define H 128

static int failures;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      fprintf (stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

static void
crop (unsigned char *dst, int x0, int y0)
{
  for (int y = 0; y < H; y++)
    memcpy (dst + y * W, capture_aes3500 + (y0 + y) * CAP_W + x0, W);
}

static void
put_u32 (unsigned char *p, unsigned int v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

int
main (void)
{
  unsigned char *img = malloc (W * H);
  SigfmImgInfo *info;

  crop (img, 0, 0);
  info = sigfm_extract (img, W, H);
  int count;
  int len = 0;
  unsigned char *bytes;

  CHECK (info != NULL);
  if (!info)
    return 1;
  count = sigfm_keypoints_count (info);
  CHECK (count > 20);
  printf ("keypoints %d\n", count);

  /* A print matches itself, and a stricter ratio can only lower the score. */
  int self = sigfm_match_score (info, info);
  CHECK (self > 0);
  CHECK (sigfm_match_score_ratio (info, info, 0.5) <= self);
  printf ("self score %d\n", self);

  /* Legacy scoring de-duplicates harder, so it can only score lower. */
  int legacy = sigfm_match_score_legacy (info, info, SIGFM_DEFAULT_RATIO);
  CHECK (legacy > 0 && legacy <= self);
  CHECK (sigfm_match_score_legacy (NULL, info, 0.75) < 0);
  CHECK (sigfm_match_score_legacy (info, info, 2.0) < 0);
  printf ("legacy self score %d\n", legacy);

  /* An overlapping region of the same capture extracts and scores >= 0. */
  unsigned char *shifted = malloc (W * H);
  crop (shifted, 32, 32);
  SigfmImgInfo *other = sigfm_extract (shifted, W, H);
  CHECK (other != NULL);
  if (other)
    {
      int cross = sigfm_match_score (info, other);
      CHECK (cross >= 0);
      printf ("shifted score %d\n", cross);
      sigfm_free_info (other);
    }
  free (shifted);

  /* Settings change what is found; out-of-range ones are rejected. */
  {
    SigfmParams cv = { 1.6, 10.0, 0 };
    SigfmImgInfo *p = sigfm_extract_params (img, W, H, &cv);
    SigfmParams def = { SIGFM_DEFAULT_SIGMA, SIGFM_DEFAULT_EDGE_THRESHOLD, 1 };
    SigfmImgInfo *d = sigfm_extract_params (img, W, H, &def);
    SigfmParams bad_sigma = { 0.1, 10.0, 0 };
    SigfmParams bad_edge = { 1.6, 1000.0, 0 };
    SigfmParams nan_sigma = { NAN, 10.0, 0 };

    CHECK (p != NULL && d != NULL);
    if (p && d)
      {
        CHECK (sigfm_keypoints_count (d) == count);   /* defaults = sigfm_extract */
        CHECK (sigfm_keypoints_count (p) != count);
        printf ("keypoints with sigma 1.6, edge 10, no CLAHE: %d\n", sigfm_keypoints_count (p));
      }
    sigfm_free_info (p);
    sigfm_free_info (d);
    CHECK (sigfm_extract_params (img, W, H, NULL) == NULL);
    CHECK (sigfm_extract_params (img, W, H, &bad_sigma) == NULL);
    CHECK (sigfm_extract_params (img, W, H, &bad_edge) == NULL);
    CHECK (sigfm_extract_params (img, W, H, &nan_sigma) == NULL);
  }

  /* Invalid arguments are rejected, not crashed on. */
  CHECK (sigfm_extract (NULL, W, H) == NULL);
  CHECK (sigfm_extract (img, 0, H) == NULL);
  CHECK (sigfm_extract (img, W, -1) == NULL);
  CHECK (sigfm_extract (img, 100000, 100000) == NULL);
  CHECK (sigfm_match_score (NULL, info) < 0);
  CHECK (sigfm_match_score_ratio (info, info, 0.0) < 0);
  CHECK (sigfm_match_score_ratio (info, info, 1.5) < 0);
  CHECK (sigfm_match_score_ratio (info, info, NAN) < 0);
  CHECK (sigfm_copy_info (NULL) == NULL);
  CHECK (sigfm_serialize_binary (NULL, &len) == NULL);

  /* Copy matches the original byte for byte. */
  SigfmImgInfo *copy = sigfm_copy_info (info);
  int clen = 0;
  CHECK (copy != NULL);
  bytes = sigfm_serialize_binary (info, &len);
  CHECK (bytes != NULL && len > 8);
  if (copy && bytes)
    {
      unsigned char *cbytes = sigfm_serialize_binary (copy, &clen);
      CHECK (cbytes && clen == len && memcmp (bytes, cbytes, len) == 0);
      free (cbytes);
      CHECK (sigfm_match_score (copy, info) == self);
    }
  sigfm_free_info (copy);

  /* Storage round trip is exact and the format is stable. */
  CHECK (memcmp (bytes, "SGF1", 4) == 0);
  SigfmImgInfo *back = sigfm_deserialize_binary (bytes, len);
  CHECK (back != NULL);
  if (back)
    {
      int blen = 0;
      unsigned char *bbytes = sigfm_serialize_binary (back, &blen);
      CHECK (bbytes && blen == len && memcmp (bytes, bbytes, len) == 0);
      free (bbytes);
      CHECK (sigfm_keypoints_count (back) == count);
      CHECK (sigfm_match_score (back, info) == self);
      sigfm_free_info (back);
    }

  /* Malformed stored data is rejected. */
  CHECK (sigfm_deserialize_binary (NULL, len) == NULL);
  CHECK (sigfm_deserialize_binary (bytes, 0) == NULL);
  CHECK (sigfm_deserialize_binary (bytes, 7) == NULL);
  CHECK (sigfm_deserialize_binary (bytes, len - 1) == NULL);
  {
    unsigned char *mut = malloc (len + 1);

    memcpy (mut, bytes, len);
    mut[len] = 0;
    CHECK (sigfm_deserialize_binary (mut, len + 1) == NULL);  /* trailing byte */

    memcpy (mut, bytes, len);
    mut[0] = 'X';
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);      /* bad magic */

    memcpy (mut, bytes, len);
    put_u32 (mut + 4, 0xffffffffu);
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);      /* huge count */

    memcpy (mut, bytes, len);
    put_u32 (mut + 4, count + 1);
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);      /* count/length mismatch */

    memcpy (mut, bytes, len);
    put_u32 (mut + 8, 0x7fc00000u);                           /* NaN x */
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);

    memcpy (mut, bytes, len);
    put_u32 (mut + 8 + count * 20, 0x7f800000u);              /* inf descriptor */
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);

    memcpy (mut, bytes, len);
    put_u32 (mut + 8 + 4, 0x7149f2cau);                       /* 1e30 y */
    CHECK (sigfm_deserialize_binary (mut, len) == NULL);
    free (mut);
  }

  /* An empty print round trips too. */
  {
    unsigned char empty[8] = { 'S', 'G', 'F', '1', 0, 0, 0, 0 };
    SigfmImgInfo *e = sigfm_deserialize_binary (empty, sizeof (empty));

    CHECK (e != NULL);
    if (e)
      {
        CHECK (sigfm_keypoints_count (e) == 0);
        CHECK (sigfm_match_score (e, info) == 0);
        CHECK (sigfm_match_score (info, e) == 0);
        SigfmImgInfo *ec = sigfm_copy_info (e);
        CHECK (ec != NULL);
        sigfm_free_info (ec);
        sigfm_free_info (e);
      }
  }

  free (bytes);
  free (img);
  sigfm_free_info (info);
  if (failures)
    fprintf (stderr, "%d check(s) failed\n", failures);
  return failures ? 1 : 0;
}
