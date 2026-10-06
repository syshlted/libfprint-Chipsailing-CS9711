// SIGFM algorithm for libfprint

// Copyright (C) 2022 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (c) 2022 Natasha England-Elbro <natasha@natashaee.me>
// Copyright (c) 2022 Timur Mangliev <tigrmango@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char SigfmPix;

/**
 * @brief Keypoints and descriptors of one image, used for matching
 * @details Get one from sigfm_extract(), sigfm_copy_info() or
 * sigfm_deserialize_binary() and release it with sigfm_free_info()
 */
typedef struct SigfmImgInfo SigfmImgInfo;

/**
 * @brief Match ratio used by sigfm_match_score()
 */
#define SIGFM_DEFAULT_RATIO 0.85

/**
 * @brief SIFT settings, which each sensor tunes separately
 *
 * The defaults are the Goodix 538d's: sigma 2.0, edge threshold 18 and
 * contrast enhancement on. The CS9711 driver was tuned without any of that,
 * with OpenCV's defaults: sigma 1.6, edge threshold 10, no enhancement.
 */
typedef struct {
  double sigma;           /**< Gaussian sigma of the base octave, 0.5 to 10 */
  double edge_threshold;  /**< Edge response limit, 1 to 100. Higher keeps more edge-like points */
  int    clahe;           /**< Non-zero to enhance local contrast (CLAHE) before extraction */
} SigfmParams;

#define SIGFM_DEFAULT_SIGMA 2.0
#define SIGFM_DEFAULT_EDGE_THRESHOLD 18.0

/**
 * @brief Extract SIFT keypoints and descriptors from a grayscale image, with
 * the default settings
 *
 * @param pix Pixels of the image, width * height bytes, row-major
 * @param width Width of the image
 * @param height Height of the image
 * @return SigfmImgInfo* Info to pass to sigfm_match_score(), or NULL on error
 */
SigfmImgInfo * sigfm_extract (const SigfmPix * pix,
                              int              width,
                              int              height);

/**
 * @brief As sigfm_extract(), with explicit SIFT settings
 *
 * @return SigfmImgInfo* Info, or NULL on error or out-of-range settings
 */
SigfmImgInfo * sigfm_extract_params (const SigfmPix    * pix,
                                     int                 width,
                                     int                 height,
                                     const SigfmParams * params);

/**
 * @brief Destroy an SigfmImgInfo
 * @warning Call this instead of free()
 */
void sigfm_free_info (SigfmImgInfo * info);

/**
 * @brief Score how closely a frame matches another, with the default ratio
 *
 * @param frame Print to be checked
 * @param enrolled Canonical print to verify against
 * @return int Score of how closely they match, values <0 indicate error, 0
 * means always reject
 */
int sigfm_match_score (SigfmImgInfo * frame,
                       SigfmImgInfo * enrolled);

/**
 * @brief Score how closely a frame matches another
 *
 * @param ratio Lowe ratio-test threshold, between 0 and 1. Lower is stricter.
 * Each sensor driver tunes its own value; see SIGFM_DEFAULT_RATIO
 * @return int As sigfm_match_score()
 */
int sigfm_match_score_ratio (SigfmImgInfo * frame,
                             SigfmImgInfo * enrolled,
                             double         ratio);

/**
 * @brief As sigfm_match_score_ratio(), with the CS9711 project's original
 * scoring
 *
 * That project de-duplicated matches by the y coordinate of the frame point
 * alone, which gives lower scores than sigfm_match_score_ratio(). Its driver
 * uses the library's default threshold, tuned against that scoring, so it
 * keeps using it. Prefer sigfm_match_score_ratio() for new drivers.
 */
int sigfm_match_score_legacy (SigfmImgInfo * frame,
                              SigfmImgInfo * enrolled,
                              double         ratio);

/**
 * @brief Serialize an image info for storage
 *
 * The format is fixed and little-endian, so stored prints stay readable
 * across versions and machines.
 *
 * @param info SigfmImgInfo to store
 * @param outlen output: Length of the returned byte array
 * @return unsigned char* byte array for storage, release it with free(), or
 * NULL on error
 */
unsigned char * sigfm_serialize_binary (SigfmImgInfo * info,
                                        int          * outlen);

/**
 * @brief Deserialize an SigfmImgInfo from storage
 *
 * The input is untrusted: it is length- and range-checked, and anything
 * malformed is rejected.
 *
 * @param bytes Byte array to deserialize from
 * @param len Length of the byte array
 * @return SigfmImgInfo* Deserialized info, or NULL if deserialization failed
 */
SigfmImgInfo * sigfm_deserialize_binary (const unsigned char * bytes,
                                         int                   len);

/**
 * @brief Keypoints for an image. Low keypoints generally means the image is
 * low quality for matching
 */
int sigfm_keypoints_count (SigfmImgInfo * info);

/**
 * @brief Copy an SigfmImgInfo
 *
 * @return SigfmImgInfo* Newly allocated copy of info, or NULL on error
 */
SigfmImgInfo * sigfm_copy_info (SigfmImgInfo * info);

#ifdef __cplusplus
}
#endif
