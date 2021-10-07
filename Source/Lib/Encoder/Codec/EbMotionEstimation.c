/*
* Copyright(c) 2019 Intel Corporation
* Copyright(c) 2019 Netflix, Inc.
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include <stdio.h>
#include <inttypes.h>

#include "aom_dsp_rtcd.h"
#include "EbPictureControlSet.h"
#include "EbSequenceControlSet.h"
#include "EbMotionEstimation.h"
#include "EbUtility.h"

#include "EbComputeSAD.h"
#include "EbReferenceObject.h"
#include "EbAvcStyleMcp.h"

#include "EbEncIntraPrediction.h"
#include "EbLambdaRateTables.h"
#include "EbTransforms.h"

#include "EbLog.h"
#include "EbResize.h"

/********************************************
 * Constants
 ********************************************/
#if FIX_TF_HME
#define TF_HME_MV_SAD_TH      512 // SAD_TH beyond which a penalty is applied to hme_mv_cost
#define TF_HME_MV_COST_WEIGHT 125 // MV_COST weight when the SAD_TH condition is valid
#endif
#if FIX_DO_NOT_TEST_CORRUPTED_MVS
EbBool check_mv_validity(int16_t x_mv, int16_t y_mv, uint8_t need_shift) {
    MV mv;
    //go to 1/8th if input is 1/4pel
    mv.row = y_mv << need_shift;
    mv.col = x_mv << need_shift;
    /* AV1 limits
      -16384 < MV_x_in_1/8 or MV_y_in_1/8 < 16384
      which means in full pel:
      -2048 < MV_x_in_full_pel or MV_y_in_full_pel < 2048
    */
    if (!is_mv_valid(&mv)) {
#if !CLN_REMOVE_CORRUPTED_MV_PRINTF
        printf("Corrupted-MV (%i %i) not in range  (%i %i); it will be ignored @ MD \n",
            mv.col,
            mv.row,
            MV_LOW,
            MV_UPP);
#endif
        return EB_FALSE;
    }
    return EB_TRUE;
}
#endif
#if !CLN_REMOVE_CHECK_MV_VALIDITY
void check_mv_validity(int16_t x_mv, int16_t y_mv, uint8_t need_shift) {
    MV mv;
    //go to 1/8th if input is 1/4pel
    mv.row = y_mv << need_shift;
    mv.col = x_mv << need_shift;
    /* AV1 limits
      -16384 < MV_x_in_1/8 or MV_y_in_1/8 < 16384
      which means in full pel:
      -2048 < MV_x_in_full_pel or MV_y_in_full_pel < 2048
    */
    if(! is_mv_valid(&mv))
        printf("Corrupted-MV (%i %i) not in range  (%i %i) \n",
            mv.col,
            mv.row,
            MV_LOW,
            MV_UPP);

}
#endif

#define MAX_INTRA_IN_MD 9
#define REFERENCE_PIC_LIST_0 0
#define REFERENCE_PIC_LIST_1 1
#define SC_HME_TH_STILL 1000
#define SC_HME_TH_EASY  100
#define SC_SR_DENOM_STILL 16
#define SC_SR_DENOM_EASY 8
/*******************************************
 * Compute8x4SAD_Default
 *   Unoptimized 8x4 SAD
 *******************************************/
uint32_t compute8x4_sad_kernel_c(uint8_t *src, // input parameter, source samples Ptr
                                 uint32_t src_stride, // input parameter, source stride
                                 uint8_t *ref, // input parameter, reference samples Ptr
                                 uint32_t ref_stride) // input parameter, reference stride
{
    uint32_t row_number_in_blocks_8x4;
    uint32_t sad_block_8x4 = 0;

    for (row_number_in_blocks_8x4 = 0; row_number_in_blocks_8x4 < 4; ++row_number_in_blocks_8x4) {
        sad_block_8x4 += EB_ABS_DIFF(src[0x00], ref[0x00]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x01], ref[0x01]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x02], ref[0x02]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x03], ref[0x03]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x04], ref[0x04]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x05], ref[0x05]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x06], ref[0x06]);
        sad_block_8x4 += EB_ABS_DIFF(src[0x07], ref[0x07]);
        src += src_stride;
        ref += ref_stride;
    }

    return sad_block_8x4;
}
/*******************************************
 * Compute8x8SAD_Default
 *   Unoptimized 8x8 SAD
 *******************************************/
uint32_t compute8x8_sad_kernel_c(uint8_t *src, // input parameter, source samples Ptr
                                 uint32_t src_stride, // input parameter, source stride
                                 uint8_t *ref, // input parameter, reference samples Ptr
                                 uint32_t ref_stride) // input parameter, reference stride
{
    uint32_t row_number_in_blocks_8x8;
    uint32_t sad_block_8x8 = 0;

    for (row_number_in_blocks_8x8 = 0; row_number_in_blocks_8x8 < 8; ++row_number_in_blocks_8x8) {
        sad_block_8x8 += EB_ABS_DIFF(src[0x00], ref[0x00]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x01], ref[0x01]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x02], ref[0x02]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x03], ref[0x03]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x04], ref[0x04]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x05], ref[0x05]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x06], ref[0x06]);
        sad_block_8x8 += EB_ABS_DIFF(src[0x07], ref[0x07]);
        src += src_stride;
        ref += ref_stride;
    }

    return sad_block_8x8;
}

/*******************************************
Calculate SAD for 16x16 and its 8x8 sublcoks
and check if there is improvment, if yes keep
the best SAD+MV
*******************************************/
void svt_ext_sad_calculation_8x8_16x16_c(uint8_t *src, uint32_t src_stride, uint8_t *ref,
                                         uint32_t ref_stride, uint32_t *p_best_sad_8x8,
                                         uint32_t *p_best_sad_16x16, uint32_t *p_best_mv8x8,
                                         uint32_t *p_best_mv16x16, uint32_t mv, uint32_t *p_sad16x16,
                                         uint32_t *p_sad8x8, EbBool sub_sad) {
    uint32_t sad16x16;

    if (sub_sad) {
        p_sad8x8[0] =
            (compute8x4_sad_kernel_c(
                src + 0 * src_stride + 0, 2 * src_stride, ref + 0 * ref_stride + 0, 2 * ref_stride))
            << 1;
        p_sad8x8[1] =
            (compute8x4_sad_kernel_c(
                src + 0 * src_stride + 8, 2 * src_stride, ref + 0 * ref_stride + 8, 2 * ref_stride))
            << 1;
        p_sad8x8[2] =
            (compute8x4_sad_kernel_c(
                src + 8 * src_stride + 0, 2 * src_stride, ref + 8 * ref_stride + 0, 2 * ref_stride))
            << 1;
        p_sad8x8[3] =
            (compute8x4_sad_kernel_c(
                src + 8 * src_stride + 8, 2 * src_stride, ref + 8 * ref_stride + 8, 2 * ref_stride))
            << 1;
    } else {
        p_sad8x8[0] = compute8x8_sad_kernel_c(
            src + 0 * src_stride + 0, src_stride, ref + 0 * ref_stride + 0, ref_stride);
        p_sad8x8[1] = compute8x8_sad_kernel_c(
            src + 0 * src_stride + 8, src_stride, ref + 0 * ref_stride + 8, ref_stride);
        p_sad8x8[2] = compute8x8_sad_kernel_c(
            src + 8 * src_stride + 0, src_stride, ref + 8 * ref_stride + 0, ref_stride);
        p_sad8x8[3] = compute8x8_sad_kernel_c(
            src + 8 * src_stride + 8, src_stride, ref + 8 * ref_stride + 8, ref_stride);
    }

    if (p_sad8x8[0] < p_best_sad_8x8[0]) {
        p_best_sad_8x8[0] = (uint32_t)p_sad8x8[0];
        p_best_mv8x8[0]   = mv;
    }

    if (p_sad8x8[1] < p_best_sad_8x8[1]) {
        p_best_sad_8x8[1] = (uint32_t)p_sad8x8[1];
        p_best_mv8x8[1]   = mv;
    }

    if (p_sad8x8[2] < p_best_sad_8x8[2]) {
        p_best_sad_8x8[2] = (uint32_t)p_sad8x8[2];
        p_best_mv8x8[2]   = mv;
    }

    if (p_sad8x8[3] < p_best_sad_8x8[3]) {
        p_best_sad_8x8[3] = (uint32_t)p_sad8x8[3];
        p_best_mv8x8[3]   = mv;
    }

    sad16x16 = p_sad8x8[0] + p_sad8x8[1] + p_sad8x8[2] + p_sad8x8[3];
    if (sad16x16 < p_best_sad_16x16[0]) {
        p_best_sad_16x16[0] = (uint32_t)sad16x16;
        p_best_mv16x16[0]   = mv;
    }

    *p_sad16x16 = (uint32_t)sad16x16;
}

/*******************************************
Calculate SAD for 32x32,64x64 from 16x16
and check if there is improvment, if yes keep
the best SAD+MV
*******************************************/
void svt_ext_sad_calculation_32x32_64x64_c(uint32_t *p_sad16x16, uint32_t *p_best_sad_32x32,
                                           uint32_t *p_best_sad_64x64, uint32_t *p_best_mv32x32,
                                           uint32_t *p_best_mv64x64, uint32_t mv,
                                           uint32_t *p_sad32x32) {
    uint32_t sad32x32_0, sad32x32_1, sad32x32_2, sad32x32_3, sad64x64;

    p_sad32x32[0] = sad32x32_0 = p_sad16x16[0] + p_sad16x16[1] + p_sad16x16[2] + p_sad16x16[3];
    if (sad32x32_0 < p_best_sad_32x32[0]) {
        p_best_sad_32x32[0] = sad32x32_0;
        p_best_mv32x32[0]   = mv;
    }

    p_sad32x32[1] = sad32x32_1 = p_sad16x16[4] + p_sad16x16[5] + p_sad16x16[6] + p_sad16x16[7];
    if (sad32x32_1 < p_best_sad_32x32[1]) {
        p_best_sad_32x32[1] = sad32x32_1;
        p_best_mv32x32[1]   = mv;
    }

    p_sad32x32[2] = sad32x32_2 = p_sad16x16[8] + p_sad16x16[9] + p_sad16x16[10] + p_sad16x16[11];
    if (sad32x32_2 < p_best_sad_32x32[2]) {
        p_best_sad_32x32[2] = sad32x32_2;
        p_best_mv32x32[2]   = mv;
    }

    p_sad32x32[3] = sad32x32_3 = p_sad16x16[12] + p_sad16x16[13] + p_sad16x16[14] + p_sad16x16[15];
    if (sad32x32_3 < p_best_sad_32x32[3]) {
        p_best_sad_32x32[3] = sad32x32_3;
        p_best_mv32x32[3]   = mv;
    }
    sad64x64 = sad32x32_0 + sad32x32_1 + sad32x32_2 + sad32x32_3;
    if (sad64x64 < p_best_sad_64x64[0]) {
        p_best_sad_64x64[0] = sad64x64;
        p_best_mv64x64[0]   = mv;
    }
}

/*******************************************
 * svt_ext_eight_sad_calculation_8x8_16x16
 *******************************************/
static void svt_ext_eight_sad_calculation_8x8_16x16(uint8_t *src, uint32_t src_stride, uint8_t *ref,
                                                    uint32_t ref_stride, uint32_t mv,
                                                    uint32_t start_16x16_pos, uint32_t *p_best_sad_8x8,
                                                    uint32_t *p_best_sad_16x16, uint32_t *p_best_mv8x8,
                                                    uint32_t *p_best_mv16x16,
                                                    uint32_t  p_eight_sad16x16[16][8],
                                                    uint32_t  p_eight_sad8x8[64][8],EbBool sub_sad) {
    const uint32_t start_8x8_pos = 4 * start_16x16_pos;
    int16_t        x_mv, y_mv;

    (void)p_eight_sad8x8;

    p_best_sad_8x8 += start_8x8_pos;
    p_best_mv8x8 += start_8x8_pos;
    p_best_sad_16x16 += start_16x16_pos;
    p_best_mv16x16 += start_16x16_pos;
    if (sub_sad)
    {
        uint32_t       src_stride_sub = (src_stride << 1);
        uint32_t       ref_stride_sub = (ref_stride << 1);
        for (int search_index = 0; search_index < 8; search_index++) {
            uint32_t sad8x8_0 =
                (compute8x4_sad_kernel_c(src, src_stride_sub, ref + search_index, ref_stride_sub)) << 1;
            if (sad8x8_0 < p_best_sad_8x8[0]) {
                p_best_sad_8x8[0] = (uint32_t)sad8x8_0;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[0] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_1 =
                (compute8x4_sad_kernel_c(
                    src + 8, src_stride_sub, ref + 8 + search_index, ref_stride_sub))
                << 1;
            if (sad8x8_1 < p_best_sad_8x8[1]) {
                p_best_sad_8x8[1] = (uint32_t)sad8x8_1;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[1] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_2 =
                (compute8x4_sad_kernel_c(src + (src_stride << 3),
                    src_stride_sub,
                    ref + (ref_stride << 3) + search_index,
                    ref_stride_sub))
                << 1;
            if (sad8x8_2 < p_best_sad_8x8[2]) {
                p_best_sad_8x8[2] = (uint32_t)sad8x8_2;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[2] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_3 =
                (compute8x4_sad_kernel_c(src + (src_stride << 3) + 8,
                    src_stride_sub,
                    ref + (ref_stride << 3) + 8 + search_index,
                    ref_stride_sub))
                << 1;
            if (sad8x8_3 < p_best_sad_8x8[3]) {
                p_best_sad_8x8[3] = (uint32_t)sad8x8_3;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[3] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }
            uint32_t sad16x16 = p_eight_sad16x16[start_16x16_pos][search_index] = sad8x8_0 + sad8x8_1 +
                sad8x8_2 + sad8x8_3;
            if (sad16x16 < p_best_sad_16x16[0]) {
                p_best_sad_16x16[0] = (uint32_t)sad16x16;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv16x16[0] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }
        }
    }
    else
    {
        for (int search_index = 0; search_index < 8; search_index++) {
            uint32_t sad8x8_0 =
                compute8x8_sad_kernel_c(src, src_stride, ref + search_index, ref_stride);
            if (sad8x8_0 < p_best_sad_8x8[0]) {
                p_best_sad_8x8[0] = (uint32_t)sad8x8_0;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[0] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_1 =
                (compute8x8_sad_kernel_c(
                    src + 8, src_stride, ref + 8 + search_index, ref_stride));
            if (sad8x8_1 < p_best_sad_8x8[1]) {
                p_best_sad_8x8[1] = (uint32_t)sad8x8_1;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[1] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_2 =
                (compute8x8_sad_kernel_c(src + (src_stride << 3),
                    src_stride,
                    ref + (ref_stride << 3) + search_index,
                    ref_stride));
            if (sad8x8_2 < p_best_sad_8x8[2]) {
                p_best_sad_8x8[2] = (uint32_t)sad8x8_2;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[2] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }

            uint32_t sad8x8_3 =
                (compute8x8_sad_kernel_c(src + (src_stride << 3) + 8,
                    src_stride,
                    ref + (ref_stride << 3) + 8 + search_index,
                    ref_stride));
            if (sad8x8_3 < p_best_sad_8x8[3]) {
                p_best_sad_8x8[3] = (uint32_t)sad8x8_3;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv8x8[3] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }
            uint32_t sad16x16 = p_eight_sad16x16[start_16x16_pos][search_index] = sad8x8_0 + sad8x8_1 +
                sad8x8_2 + sad8x8_3;
            if (sad16x16 < p_best_sad_16x16[0]) {
                p_best_sad_16x16[0] = (uint32_t)sad16x16;
                x_mv = _MVXT(mv) + (int16_t)search_index * 4;
                y_mv = _MVYT(mv);
                p_best_mv16x16[0] = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
            }
        }

    }
}

void svt_ext_all_sad_calculation_8x8_16x16_c(uint8_t *src, uint32_t src_stride, uint8_t *ref,
                                             uint32_t ref_stride, uint32_t mv,
#if OPT_TFILTER
    uint8_t out_8x8,
#endif
    uint32_t *p_best_sad_8x8,
                                             uint32_t *p_best_sad_16x16, uint32_t *p_best_mv8x8,
                                             uint32_t *p_best_mv16x16, uint32_t p_eight_sad16x16[16][8],
                                             uint32_t p_eight_sad8x8[64][8], EbBool sub_sad) {
    static const char offsets[16] = {0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15};
#if OPT_TFILTER
    (void)out_8x8;
#endif
    //---- 16x16 : 0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            const uint32_t block_index           = 16 * y * src_stride + 16 * x;
            const uint32_t search_position_index = 16 * y * ref_stride + 16 * x;
            svt_ext_eight_sad_calculation_8x8_16x16(src + block_index,
                                                    src_stride,
                                                    ref + search_position_index,
                                                    ref_stride,
                                                    mv,
                                                    offsets[4 * y + x],
                                                    p_best_sad_8x8,
                                                    p_best_sad_16x16,
                                                    p_best_mv8x8,
                                                    p_best_mv16x16,
                                                    p_eight_sad16x16,
                                                    p_eight_sad8x8,
                                                    sub_sad);
        }
    }
}

/*******************************************
Calculate SAD for 32x32,64x64 from 16x16
and check if there is improvment, if yes keep
the best SAD+MV
*******************************************/
void svt_ext_eight_sad_calculation_32x32_64x64_c(uint32_t  p_sad16x16[16][8],
                                                 uint32_t *p_best_sad_32x32,
                                                 uint32_t *p_best_sad_64x64,
                                                 uint32_t *p_best_mv32x32,
                                                 uint32_t *p_best_mv64x64,
                                                 uint32_t mv,
                                                 uint32_t p_sad32x32[4][8]) {
    uint32_t search_index;
    int16_t  x_mv, y_mv;
    for (search_index = 0; search_index < 8; search_index++) {
        uint32_t sad32x32_0, sad32x32_1, sad32x32_2, sad32x32_3, sad64x64;

        p_sad32x32[0][search_index] = sad32x32_0 =
            p_sad16x16[0][search_index] + p_sad16x16[1][search_index] +
            p_sad16x16[2][search_index] + p_sad16x16[3][search_index];
        if (sad32x32_0 < p_best_sad_32x32[0]) {
            p_best_sad_32x32[0] = sad32x32_0;
            x_mv                = _MVXT(mv) + (int16_t)search_index * 4;
            y_mv                = _MVYT(mv);
            p_best_mv32x32[0]   = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
        }

        p_sad32x32[1][search_index] = sad32x32_1 =
            p_sad16x16[4][search_index] + p_sad16x16[5][search_index] +
            p_sad16x16[6][search_index] + p_sad16x16[7][search_index];
        if (sad32x32_1 < p_best_sad_32x32[1]) {
            p_best_sad_32x32[1] = sad32x32_1;
            x_mv                = _MVXT(mv) + (int16_t)search_index * 4;
            y_mv                = _MVYT(mv);
            p_best_mv32x32[1]   = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
        }

        p_sad32x32[2][search_index] = sad32x32_2 =
            p_sad16x16[8][search_index] + p_sad16x16[9][search_index] +
            p_sad16x16[10][search_index] + p_sad16x16[11][search_index];
        if (sad32x32_2 < p_best_sad_32x32[2]) {
            p_best_sad_32x32[2] = sad32x32_2;
            x_mv                = _MVXT(mv) + (int16_t)search_index * 4;
            y_mv                = _MVYT(mv);
            p_best_mv32x32[2]   = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
        }

        p_sad32x32[3][search_index] = sad32x32_3 =
            p_sad16x16[12][search_index] + p_sad16x16[13][search_index] +
            p_sad16x16[14][search_index] + p_sad16x16[15][search_index];
        if (sad32x32_3 < p_best_sad_32x32[3]) {
            p_best_sad_32x32[3] = sad32x32_3;
            x_mv                = _MVXT(mv) + (int16_t)search_index * 4;
            y_mv                = _MVYT(mv);
            p_best_mv32x32[3]   = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
        }

        sad64x64 = sad32x32_0 + sad32x32_1 + sad32x32_2 + sad32x32_3;
        if (sad64x64 < p_best_sad_64x64[0]) {
            p_best_sad_64x64[0] = sad64x64;
            x_mv                = _MVXT(mv) + (int16_t)search_index * 4;
            y_mv                = _MVYT(mv);
            p_best_mv64x64[0]   = ((uint16_t)y_mv << 16) | ((uint16_t)x_mv);
        }
    }
}
/*******************************************
 * open_loop_me_get_search_point_results_block
 *******************************************/
static void open_loop_me_get_eight_search_point_results_block(
    MeContext *context_ptr, // input parameter, ME context Ptr, used to get SB Ptr
    uint32_t   list_index, // input parameter, reference list index
    uint32_t   ref_pic_index,
    uint32_t   search_region_index, // input parameter, search area origin, used to
    // point to reference samples
    int32_t x_search_index, // input parameter, search region position in the
    // horizontal direction, used to derive xMV
    int32_t y_search_index // input parameter, search region position in the
    // vertical direction, used to derive yMV
) {
    // uint32_t ref_luma_stride = ref_pic_ptr->stride_y; // NADER
    // uint8_t  *ref_ptr = ref_pic_ptr->buffer_y; // NADER
    const EbBool sub_sad = (context_ptr->me_search_method == SUB_SAD_SEARCH);
    uint32_t ref_luma_stride = context_ptr->interpolated_full_stride[list_index][ref_pic_index];
    uint8_t *ref_ptr =
        context_ptr->integer_buffer_ptr[list_index][ref_pic_index] +
        ((ME_FILTER_TAP >> 1) * context_ptr->interpolated_full_stride[list_index][ref_pic_index]) +
        (ME_FILTER_TAP >> 1) + search_region_index;

    uint32_t curr_mv_1 = (((uint16_t)y_search_index) << 18);
    uint16_t curr_mv_2 = (((uint16_t)x_search_index << 2));
    uint32_t curr_mv   = curr_mv_1 | curr_mv_2;

    svt_ext_all_sad_calculation_8x8_16x16(context_ptr->sb_src_ptr,
                                          context_ptr->sb_src_stride,
                                          ref_ptr,
                                          ref_luma_stride,
                                          curr_mv,
#if OPT_TFILTER
                                          context_ptr->me_type!=ME_MCTF,
#endif
                                          context_ptr->p_best_sad_8x8,
                                          context_ptr->p_best_sad_16x16,
                                          context_ptr->p_best_mv8x8,
                                          context_ptr->p_best_mv16x16,
                                          context_ptr->p_eight_sad16x16,
                                          context_ptr->p_eight_sad8x8,
                                          sub_sad);

   svt_ext_eight_sad_calculation_32x32_64x64(context_ptr->p_eight_sad16x16,
                                             context_ptr->p_best_sad_32x32,
                                             context_ptr->p_best_sad_64x64,
                                             context_ptr->p_best_mv32x32,
                                             context_ptr->p_best_mv64x64,
                                             curr_mv,
                                             context_ptr->p_eight_sad32x32);
}
/*******************************************
 * open_loop_me_get_search_point_results_block
 *******************************************/
static void open_loop_me_get_search_point_results_block(
    MeContext *context_ptr, // input parameter, ME context Ptr, used to get SB Ptr
    uint32_t   list_index, // input parameter, reference list index
    uint32_t   ref_pic_index,
    uint32_t   search_region_index, // input parameter, search area origin, used to
    // point to reference samples
    int32_t x_search_index, // input parameter, search region position in the
    // horizontal direction, used to derive xMV
    int32_t y_search_index) // input parameter, search region position in the
// vertical direction, used to derive yMV
{
    const EbBool sub_sad = (context_ptr->me_search_method == SUB_SAD_SEARCH);
    uint8_t *    src_ptr = context_ptr->sb_src_ptr;

    // uint8_t  *ref_ptr = ref_pic_ptr->buffer_y; // NADER
    uint8_t *ref_ptr =
        context_ptr->integer_buffer_ptr[list_index][ref_pic_index] + (ME_FILTER_TAP >> 1) +
        ((ME_FILTER_TAP >> 1) * context_ptr->interpolated_full_stride[list_index][ref_pic_index]);
    // uint32_t ref_luma_stride = ref_pic_ptr->stride_y; // NADER
    uint32_t ref_luma_stride = context_ptr->interpolated_full_stride[list_index][ref_pic_index];
    uint32_t search_position_tl_index = search_region_index;
    uint32_t search_position_index;
    uint32_t block_index;
    uint32_t src_next_16x16_offset;
    // uint32_t ref_next_16x16_offset = (ref_pic_ptr->stride_y << 4); // NADER
    uint32_t  ref_next_16x16_offset = (ref_luma_stride << 4);
    uint32_t  curr_mv_1             = (((uint16_t)y_search_index) << 18);
    uint16_t  curr_mv_2             = (((uint16_t)x_search_index << 2));
    uint32_t  curr_mv               = curr_mv_1 | curr_mv_2;
    uint32_t *p_best_sad_8x8        = context_ptr->p_best_sad_8x8;
    uint32_t *p_best_sad_16x16      = context_ptr->p_best_sad_16x16;
    uint32_t *p_best_sad_32x32      = context_ptr->p_best_sad_32x32;
    uint32_t *p_best_sad_64x64      = context_ptr->p_best_sad_64x64;
    uint32_t *p_best_mv8x8          = context_ptr->p_best_mv8x8;
    uint32_t *p_best_mv16x16        = context_ptr->p_best_mv16x16;
    uint32_t *p_best_mv32x32        = context_ptr->p_best_mv32x32;
    uint32_t *p_best_mv64x64        = context_ptr->p_best_mv64x64;
    uint32_t *p_sad32x32            = context_ptr->p_sad32x32;
    uint32_t *p_sad16x16            = context_ptr->p_sad16x16;
    uint32_t *p_sad8x8              = context_ptr->p_sad8x8;

    // TODO: block_index search_position_index could be removed

    const uint32_t src_stride = context_ptr->sb_src_stride;
    src_next_16x16_offset     = src_stride << 4;

    //---- 16x16 : 0
    block_index           = 0;
    search_position_index = search_position_tl_index;

    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[0],
                                      &p_best_sad_16x16[0],
                                      &p_best_mv8x8[0],
                                      &p_best_mv16x16[0],
                                      curr_mv,
                                      &p_sad16x16[0],
                                      &p_sad8x8[0],
                                      sub_sad);

    //---- 16x16 : 1
    block_index           = block_index + 16;
    search_position_index = search_position_tl_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[4],
                                      &p_best_sad_16x16[1],
                                      &p_best_mv8x8[4],
                                      &p_best_mv16x16[1],
                                      curr_mv,
                                      &p_sad16x16[1],
                                      &p_sad8x8[4],
                                      sub_sad);
    //---- 16x16 : 4
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;

    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[16],
                                      &p_best_sad_16x16[4],
                                      &p_best_mv8x8[16],
                                      &p_best_mv16x16[4],
                                      curr_mv,
                                      &p_sad16x16[4],
                                      &p_sad8x8[16],
                                      sub_sad);

    //---- 16x16 : 5
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[20],
                                      &p_best_sad_16x16[5],
                                      &p_best_mv8x8[20],
                                      &p_best_mv16x16[5],
                                      curr_mv,
                                      &p_sad16x16[5],
                                      &p_sad8x8[20],
                                      sub_sad);

    //---- 16x16 : 2
    block_index           = src_next_16x16_offset;
    search_position_index = search_position_tl_index + ref_next_16x16_offset;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                     src_stride,
                                     ref_ptr + search_position_index,
                                     ref_luma_stride,
                                     &p_best_sad_8x8[8],
                                     &p_best_sad_16x16[2],
                                     &p_best_mv8x8[8],
                                     &p_best_mv16x16[2],
                                     curr_mv,
                                     &p_sad16x16[2],
                                     &p_sad8x8[8],
                                     sub_sad);
    //---- 16x16 : 3
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[12],
                                      &p_best_sad_16x16[3],
                                      &p_best_mv8x8[12],
                                      &p_best_mv16x16[3],
                                      curr_mv,
                                      &p_sad16x16[3],
                                      &p_sad8x8[12],
                                      sub_sad);
    //---- 16x16 : 6
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[24],
                                      &p_best_sad_16x16[6],
                                      &p_best_mv8x8[24],
                                      &p_best_mv16x16[6],
                                      curr_mv,
                                      &p_sad16x16[6],
                                      &p_sad8x8[24],
                                      sub_sad);
    //---- 16x16 : 7
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[28],
                                      &p_best_sad_16x16[7],
                                      &p_best_mv8x8[28],
                                      &p_best_mv16x16[7],
                                      curr_mv,
                                      &p_sad16x16[7],
                                      &p_sad8x8[28],
                                      sub_sad);

    //---- 16x16 : 8
    block_index           = (src_next_16x16_offset << 1);
    search_position_index = search_position_tl_index + (ref_next_16x16_offset << 1);
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[32],
                                      &p_best_sad_16x16[8],
                                      &p_best_mv8x8[32],
                                      &p_best_mv16x16[8],
                                      curr_mv,
                                      &p_sad16x16[8],
                                      &p_sad8x8[32],
                                      sub_sad);
    //---- 16x16 : 9
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[36],
                                      &p_best_sad_16x16[9],
                                      &p_best_mv8x8[36],
                                      &p_best_mv16x16[9],
                                      curr_mv,
                                      &p_sad16x16[9],
                                      &p_sad8x8[36],
                                      sub_sad);
    //---- 16x16 : 12
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[48],
                                      &p_best_sad_16x16[12],
                                      &p_best_mv8x8[48],
                                      &p_best_mv16x16[12],
                                      curr_mv,
                                      &p_sad16x16[12],
                                      &p_sad8x8[48],
                                      sub_sad);
    //---- 16x16 : 13
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[52],
                                      &p_best_sad_16x16[13],
                                      &p_best_mv8x8[52],
                                      &p_best_mv16x16[13],
                                      curr_mv,
                                      &p_sad16x16[13],
                                      &p_sad8x8[52],
                                      sub_sad);

    //---- 16x16 : 10
    block_index           = (src_next_16x16_offset * 3);
    search_position_index = search_position_tl_index + (ref_next_16x16_offset * 3);
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[40],
                                      &p_best_sad_16x16[10],
                                      &p_best_mv8x8[40],
                                      &p_best_mv16x16[10],
                                      curr_mv,
                                      &p_sad16x16[10],
                                      &p_sad8x8[40],
                                      sub_sad);
    //---- 16x16 : 11
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[44],
                                      &p_best_sad_16x16[11],
                                      &p_best_mv8x8[44],
                                      &p_best_mv16x16[11],
                                      curr_mv,
                                      &p_sad16x16[11],
                                      &p_sad8x8[44],
                                      sub_sad);
    //---- 16x16 : 14
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[56],
                                      &p_best_sad_16x16[14],
                                      &p_best_mv8x8[56],
                                      &p_best_mv16x16[14],
                                      curr_mv,
                                      &p_sad16x16[14],
                                      &p_sad8x8[56],
                                      sub_sad);
    //---- 16x16 : 15
    block_index           = block_index + 16;
    search_position_index = search_position_index + 16;
    svt_ext_sad_calculation_8x8_16x16(src_ptr + block_index,
                                      src_stride,
                                      ref_ptr + search_position_index,
                                      ref_luma_stride,
                                      &p_best_sad_8x8[60],
                                      &p_best_sad_16x16[15],
                                      &p_best_mv8x8[60],
                                      &p_best_mv16x16[15],
                                      curr_mv,
                                      &p_sad16x16[15],
                                      &p_sad8x8[60],
                                      sub_sad);

    svt_ext_sad_calculation_32x32_64x64(p_sad16x16,
                                        p_best_sad_32x32,
                                        p_best_sad_64x64,
                                        p_best_mv32x32,
                                        p_best_mv64x64,
                                        curr_mv,
                                        &p_sad32x32[0]);
}

/*******************************************
 * open_loop_me_fullpel_search_sblock
 *******************************************/
static void open_loop_me_fullpel_search_sblock(MeContext *context_ptr, uint32_t list_index,
                                               uint32_t ref_pic_index, int16_t x_search_area_origin,
                                               int16_t  y_search_area_origin,
                                               uint32_t search_area_width,
                                               uint32_t search_area_height){
    uint32_t x_search_index, y_search_index;
    uint32_t search_area_width_rest_8 = search_area_width & 7;
    uint32_t search_area_width_mult_8 = search_area_width - search_area_width_rest_8;

    for (y_search_index = 0; y_search_index < search_area_height; y_search_index++) {
        for (x_search_index = 0; x_search_index < search_area_width_mult_8; x_search_index += 8) {
            // this function will do:  x_search_index, +1, +2, ..., +7
            open_loop_me_get_eight_search_point_results_block(
                context_ptr,
                list_index,
                ref_pic_index,
                x_search_index +
                    y_search_index *
                        context_ptr->interpolated_full_stride[list_index][ref_pic_index],
                (int32_t)x_search_index + x_search_area_origin,
                (int32_t)y_search_index + y_search_area_origin);
        }

        for (x_search_index = search_area_width_mult_8; x_search_index < search_area_width;
             x_search_index++) {
            open_loop_me_get_search_point_results_block(
                context_ptr,
                list_index,
                ref_pic_index,
                x_search_index +
                    y_search_index *
                        context_ptr->interpolated_full_stride[list_index][ref_pic_index],
                (int32_t)x_search_index + x_search_area_origin,
                (int32_t)y_search_index + y_search_area_origin);
        }
    }
}



void hme_level_0(
    PictureParentControlSet *pcs_ptr,
    MeContext *              context_ptr, // input/output parameter, ME context Ptr, used to
    // get/update ME results
    int16_t origin_x, // input parameter, SB position in the horizontal
    // direction- sixteenth resolution
    int16_t origin_y, // input parameter, SB position in the vertical
    // direction- sixteenth resolution
    uint32_t sb_width, // input parameter, SB pwidth - sixteenth resolution
    uint32_t sb_height, // input parameter, SB height - sixteenth resolution
    int16_t  x_hme_search_center, // input parameter, HME search center in the
    // horizontal direction
    int16_t y_hme_search_center, // input parameter, HME search center in the
    // vertical direction
    EbPictureBufferDesc *sixteenth_ref_pic_ptr, // input parameter, sixteenth reference Picture Ptr
    uint32_t             search_region_number_in_width, // input parameter, search region
    // number in the horizontal direction
    uint32_t search_region_number_in_height, // input parameter, search region
    // number in the vertical direction
    uint64_t *level0Bestsad_, // output parameter, Level0 SAD at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    int16_t *xLevel0SearchCenter, // output parameter, Level0 xMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    int16_t *yLevel0SearchCenter, // output parameter, Level0 yMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    uint32_t searchAreaMultiplierX, uint32_t searchAreaMultiplierY) {
    int16_t  x_top_left_search_region;
    int16_t  y_top_left_search_region;
    uint32_t search_region_index;
    int16_t  x_search_area_origin;
    int16_t  y_search_area_origin;
    int16_t  x_search_region_distance;
    int16_t  y_search_region_distance;

    int16_t pad_width;
    int16_t pad_height;

    // Adjust SR size based on the searchAreaShift
    (void)pcs_ptr;
    // Round up x_HME_L0 to be a multiple of 16
    int16_t search_area_width = MIN((int16_t)(
        (((((context_ptr->hme_level0_search_area_in_width_array[search_region_number_in_width] *
            searchAreaMultiplierX) /
            100))) +
            15) &
        ~0x0F), (int16_t)((context_ptr->hme_level0_max_search_area_in_width_array[search_region_number_in_width] + 15) & ~0x0F));
    int16_t search_area_height = MIN((int16_t)(
        ((context_ptr->hme_level0_search_area_in_height_array[search_region_number_in_height] *
            searchAreaMultiplierY) /
            100)), (int16_t)context_ptr->hme_level0_max_search_area_in_height_array[search_region_number_in_height]);
    x_search_region_distance = x_hme_search_center;
    y_search_region_distance = y_hme_search_center;
    pad_width                = (int16_t)(sixteenth_ref_pic_ptr->origin_x) - 1;
    pad_height               = (int16_t)(sixteenth_ref_pic_ptr->origin_y) - 1;

    while (search_region_number_in_width) {
        search_region_number_in_width--;
        x_search_region_distance += MIN((int16_t)(
            ((context_ptr->hme_level0_search_area_in_width_array[search_region_number_in_width] *
                searchAreaMultiplierX) /
                100)), (int16_t)(context_ptr->hme_level0_max_search_area_in_width_array[search_region_number_in_width]));
    }

    while (search_region_number_in_height) {
        search_region_number_in_height--;
        y_search_region_distance += MIN((int16_t)(
            ((context_ptr->hme_level0_search_area_in_height_array[search_region_number_in_height] *
                searchAreaMultiplierY) /
                100)), (int16_t)(context_ptr->hme_level0_max_search_area_in_height_array[search_region_number_in_height]));
    }
    x_search_area_origin =
        -(int16_t)(
        (MIN(((context_ptr->hme_level0_total_search_area_width * searchAreaMultiplierX) / 100), context_ptr->hme_level0_max_total_search_area_width)) >>
            1) +
        x_search_region_distance;
    y_search_area_origin =
        -(int16_t)(
        (MIN(((context_ptr->hme_level0_total_search_area_height * searchAreaMultiplierY) / 100), context_ptr->hme_level0_max_total_search_area_height)) >>
            1) +
        y_search_region_distance;
    // Correct the left edge of the Search Area if it is not on the reference
    // Picture
    x_search_area_origin = ((origin_x + x_search_area_origin) < -pad_width) ? -pad_width - origin_x
                                                                            : x_search_area_origin;

    search_area_width = ((origin_x + x_search_area_origin) < -pad_width)
                            ? search_area_width - (-pad_width - (origin_x + x_search_area_origin))
                            : search_area_width;

    // Correct the right edge of the Search Area if its not on the reference
    // Picture
    x_search_area_origin =
        ((origin_x + x_search_area_origin) > (int16_t)sixteenth_ref_pic_ptr->width - 1)
            ? x_search_area_origin -
                  ((origin_x + x_search_area_origin) - ((int16_t)sixteenth_ref_pic_ptr->width - 1))
            : x_search_area_origin;

    search_area_width =
        ((origin_x + x_search_area_origin + search_area_width) >
         (int16_t)sixteenth_ref_pic_ptr->width)
            ? MAX(1,
                  search_area_width - ((origin_x + x_search_area_origin + search_area_width) -
                                       (int16_t)sixteenth_ref_pic_ptr->width))
            : search_area_width;

    // Round down x_HME to be a multiple of 16 as cropping already performed
    search_area_width = (search_area_width < 16) ? search_area_width : search_area_width & ~0x0F;

    // Correct the top edge of the Search Area if it is not on the reference
    // Picture
    y_search_area_origin = ((origin_y + y_search_area_origin) < -pad_height)
                               ? -pad_height - origin_y
                               : y_search_area_origin;

    search_area_height =
        ((origin_y + y_search_area_origin) < -pad_height)
            ? search_area_height - (-pad_height - (origin_y + y_search_area_origin))
            : search_area_height;

    // Correct the bottom edge of the Search Area if its not on the reference
    // Picture
    y_search_area_origin =
        ((origin_y + y_search_area_origin) > (int16_t)sixteenth_ref_pic_ptr->height - 1)
            ? y_search_area_origin -
                  ((origin_y + y_search_area_origin) - ((int16_t)sixteenth_ref_pic_ptr->height - 1))
            : y_search_area_origin;

    search_area_height =
        (origin_y + y_search_area_origin + search_area_height >
         (int16_t)sixteenth_ref_pic_ptr->height)
            ? MAX(1,
                  search_area_height - ((origin_y + y_search_area_origin + search_area_height) -
                                        (int16_t)sixteenth_ref_pic_ptr->height))
            : search_area_height;

    x_top_left_search_region =
        ((int16_t)sixteenth_ref_pic_ptr->origin_x + origin_x) + x_search_area_origin;
    y_top_left_search_region =
        ((int16_t)sixteenth_ref_pic_ptr->origin_y + origin_y) + y_search_area_origin;
    search_region_index =
        x_top_left_search_region + y_top_left_search_region * sixteenth_ref_pic_ptr->stride_y;

    // Put the first search location into level0 results
    svt_sad_loop_kernel(
                &context_ptr->sixteenth_sb_buffer[0],
                context_ptr->hme_search_method == FULL_SAD_SEARCH ? context_ptr->sixteenth_sb_buffer_stride : context_ptr->sixteenth_sb_buffer_stride*2,
                &sixteenth_ref_pic_ptr->buffer_y[search_region_index],
                (context_ptr->hme_search_method == FULL_SAD_SEARCH)
                    ? sixteenth_ref_pic_ptr->stride_y
                    : sixteenth_ref_pic_ptr->stride_y * 2,
                (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? sb_height : sb_height >> 1,
                sb_width,
                /* results */
                level0Bestsad_,
                xLevel0SearchCenter,
                yLevel0SearchCenter,
                /* range */
                sixteenth_ref_pic_ptr->stride_y,
#if FTR_PREHME_SUB
#if TUNE_HME_SUB
                context_ptr->skip_search_line_hme0,
#else
                0,
#endif
#endif
                search_area_width,
                search_area_height);

    *level0Bestsad_ =
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
            ? *level0Bestsad_
            : *level0Bestsad_ * 2; // Multiply by 2 because considered only ever other line
    *xLevel0SearchCenter += x_search_area_origin;
    *xLevel0SearchCenter *= 4; // Multiply by 4 because operating on 1/4 resolution
    *yLevel0SearchCenter += y_search_area_origin;
    *yLevel0SearchCenter *= 4; // Multiply by 4 because operating on 1/4 resolution

    return;
}

void hme_level_1(
    MeContext *context_ptr, // input/output parameter, ME context Ptr, used to
    // get/update ME results
    int16_t origin_x, // input parameter, SB position in the horizontal
    // direction - quarter resolution
    int16_t origin_y, // input parameter, SB position in the vertical direction
    // - quarter resolution
    uint32_t             sb_width, // input parameter, SB pwidth - quarter resolution
    uint32_t             sb_height, // input parameter, SB height - quarter resolution
    EbPictureBufferDesc *quarter_ref_pic_ptr, // input parameter, quarter reference Picture Ptr
    int16_t              hme_level1_search_area_in_width, // input parameter, hme level 1 search
    // area in width
    int16_t hme_level1_search_area_in_height, // input parameter, hme level 1 search
    // area in height
    int16_t hme_level1_max_search_area_in_width, // input parameter, hme level 1 search area in width
    int16_t hme_level1_max_search_area_in_height, // input parameter, hme level 1 search
    uint32_t hme_sr_factor_x,
    uint32_t hme_sr_factor_y,
    int16_t xLevel0SearchCenter, // input parameter, best Level0 xMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    int16_t yLevel0SearchCenter, // input parameter, best Level0 yMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    uint64_t *level1Bestsad_, // output parameter, Level1 SAD at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    int16_t *xLevel1SearchCenter, // output parameter, Level1 xMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
    int16_t *yLevel1SearchCenter // output parameter, Level1 yMV at
    // (search_region_number_in_width,
    // search_region_number_in_height)
) {
    int16_t  x_top_left_search_region;
    int16_t  y_top_left_search_region;
    uint32_t search_region_index;
    // Round up x_HME_L0 to be a multiple of 8
    // Don't change TWO_DECIMATION_HME refinement; use the HME distance algorithm only for non-2D HME
    if (context_ptr->hme_decimation <= ONE_DECIMATION_HME) {
        hme_level1_search_area_in_width = MIN((((int16_t)hme_sr_factor_x  * hme_level1_search_area_in_width) / 100), hme_level1_max_search_area_in_width);
        hme_level1_search_area_in_height = MIN((((int16_t)hme_sr_factor_y  * hme_level1_search_area_in_height) / 100), hme_level1_max_search_area_in_height);
    }
    int16_t search_area_width  = (int16_t)((hme_level1_search_area_in_width + 7) & ~0x07);
    int16_t search_area_height = hme_level1_search_area_in_height;

    int16_t x_search_area_origin;
    int16_t y_search_area_origin;

    int16_t pad_width  = (int16_t)(quarter_ref_pic_ptr->origin_x) - 1;
    int16_t pad_height = (int16_t)(quarter_ref_pic_ptr->origin_y) - 1;

    x_search_area_origin = -(search_area_width >> 1) + xLevel0SearchCenter;
    y_search_area_origin = -(search_area_height >> 1) + yLevel0SearchCenter;

    // Correct the left edge of the Search Area if it is not on the reference
    // Picture
    x_search_area_origin = ((origin_x + x_search_area_origin) < -pad_width) ? -pad_width - origin_x
                                                                            : x_search_area_origin;

    search_area_width = ((origin_x + x_search_area_origin) < -pad_width)
                            ? search_area_width - (-pad_width - (origin_x + x_search_area_origin))
                            : search_area_width;
    // Correct the right edge of the Search Area if its not on the reference
    // Picture
    x_search_area_origin =
        ((origin_x + x_search_area_origin) > (int16_t)quarter_ref_pic_ptr->width - 1)
            ? x_search_area_origin -
                  ((origin_x + x_search_area_origin) - ((int16_t)quarter_ref_pic_ptr->width - 1))
            : x_search_area_origin;

    search_area_width =
        ((origin_x + x_search_area_origin + search_area_width) >
         (int16_t)quarter_ref_pic_ptr->width)
            ? MAX(1,
                  search_area_width - ((origin_x + x_search_area_origin + search_area_width) -
                                       (int16_t)quarter_ref_pic_ptr->width))
            : search_area_width;

    // Constrain x_HME_L1 to be a multiple of 8 (round down as cropping already
    // performed)
    search_area_width = (search_area_width < 8) ? search_area_width : search_area_width & ~0x07;
    // Correct the top edge of the Search Area if it is not on the reference
    // Picture
    y_search_area_origin = ((origin_y + y_search_area_origin) < -pad_height)
                               ? -pad_height - origin_y
                               : y_search_area_origin;

    search_area_height =
        ((origin_y + y_search_area_origin) < -pad_height)
            ? search_area_height - (-pad_height - (origin_y + y_search_area_origin))
            : search_area_height;

    // Correct the bottom edge of the Search Area if its not on the reference
    // Picture
    y_search_area_origin =
        ((origin_y + y_search_area_origin) > (int16_t)quarter_ref_pic_ptr->height - 1)
            ? y_search_area_origin -
                  ((origin_y + y_search_area_origin) - ((int16_t)quarter_ref_pic_ptr->height - 1))
            : y_search_area_origin;

    search_area_height =
        (origin_y + y_search_area_origin + search_area_height >
         (int16_t)quarter_ref_pic_ptr->height)
            ? MAX(1,
                  search_area_height - ((origin_y + y_search_area_origin + search_area_height) -
                                        (int16_t)quarter_ref_pic_ptr->height))
            : search_area_height;

    // Move to the top left of the search region
    x_top_left_search_region =
        ((int16_t)quarter_ref_pic_ptr->origin_x + origin_x) + x_search_area_origin;
    y_top_left_search_region =
        ((int16_t)quarter_ref_pic_ptr->origin_y + origin_y) + y_search_area_origin;
    search_region_index =
        x_top_left_search_region + y_top_left_search_region * quarter_ref_pic_ptr->stride_y;

    // Put the first search location into level0 results
    svt_sad_loop_kernel(
        &context_ptr->quarter_sb_buffer[0],
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
            ? context_ptr->quarter_sb_buffer_stride
            : context_ptr->quarter_sb_buffer_stride * 2,
        &quarter_ref_pic_ptr->buffer_y[search_region_index],
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? quarter_ref_pic_ptr->stride_y
                                                            : quarter_ref_pic_ptr->stride_y * 2,
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? sb_height : sb_height >> 1,
        sb_width,
        /* results */
        level1Bestsad_,
        xLevel1SearchCenter,
        yLevel1SearchCenter,
        /* range */
        quarter_ref_pic_ptr->stride_y,
#if FTR_PREHME_SUB
        0,
#endif
        search_area_width,
        search_area_height);

    *level1Bestsad_ =
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
            ? *level1Bestsad_
            : *level1Bestsad_ * 2; // Multiply by 2 because considered only ever other line
    *xLevel1SearchCenter += x_search_area_origin;
    *xLevel1SearchCenter *= 2; // Multiply by 2 because operating on 1/2 resolution
    *yLevel1SearchCenter += y_search_area_origin;
    *yLevel1SearchCenter *= 2; // Multiply by 2 because operating on 1/2 resolution

    return;
}



void hme_level_2(
                 PictureParentControlSet *pcs_ptr, // input parameter, Picture control set Ptr
                 MeContext *context_ptr, // input/output parameter, ME context Ptr, used to
                 // get/update ME results
                 int16_t  origin_x, // input parameter, SB position in the horizontal direction
                 int16_t  origin_y, // input parameter, SB position in the vertical direction
                 uint32_t sb_width, // input parameter, SB pwidth - full resolution
                 uint32_t sb_height, // input parameter, SB height - full resolution
                 EbPictureBufferDesc *ref_pic_ptr, // input parameter, reference Picture Ptr
                int16_t hme_level2_search_area_in_width,
                int16_t hme_level2_search_area_in_height,
                int16_t hme_level2_max_search_area_in_width, // input parameter, hme level 1 search area in width
                int16_t hme_level2_max_search_area_in_height, // input parameter, hme level 1 search
                uint32_t hme_sr_factor_x,
                uint32_t hme_sr_factor_y,
                 // number in the vertical direction
                 int16_t xLevel1SearchCenter, // input parameter, best Level1 xMV
                 // at(search_region_number_in_width,
                 // search_region_number_in_height)
                 int16_t yLevel1SearchCenter, // input parameter, best Level1 yMV
                 // at(search_region_number_in_width,
                 // search_region_number_in_height)
                 uint64_t *level2Bestsad_, // output parameter, Level2 SAD at
                 // (search_region_number_in_width,
                 // search_region_number_in_height)
                 int16_t *xLevel2SearchCenter, // output parameter, Level2 xMV at
                 // (search_region_number_in_width,
                 // search_region_number_in_height)
                 int16_t *yLevel2SearchCenter // output parameter, Level2 yMV at
                 // (search_region_number_in_width,
                 // search_region_number_in_height)
) {
    int16_t  x_top_left_search_region;
    int16_t  y_top_left_search_region;
    uint32_t search_region_index;

    // round the search region width to nearest multiple of 8 if it is less than
    // 8 or non multiple of 8 SAD calculation performance is the same for
    // searchregion width from 1 to 8
    (void)pcs_ptr;
    // Don't change TWO_DECIMATION_HME or ONE_DECIMATION_HME refinement; use the HME distance algorithm only for 0D HME
    if (context_ptr->hme_decimation == ZERO_DECIMATION_HME) {
        hme_level2_search_area_in_width = MIN((((int16_t)hme_sr_factor_x  * hme_level2_search_area_in_width) / 100), hme_level2_max_search_area_in_width);
        hme_level2_search_area_in_height = MIN((((int16_t)hme_sr_factor_y  * hme_level2_search_area_in_height) / 100), hme_level2_max_search_area_in_height);
    }
    // Round up x_HME_L0 to be a multiple of 8
    int16_t search_area_width = (int16_t)((hme_level2_search_area_in_width + 7) & ~0x07);
    int16_t search_area_height = hme_level2_search_area_in_height;
    int16_t x_search_area_origin;
    int16_t y_search_area_origin;

    int16_t pad_width  = (int16_t)BLOCK_SIZE_64 - 1;
    int16_t pad_height = (int16_t)BLOCK_SIZE_64 - 1;

    x_search_area_origin = -(search_area_width >> 1) + xLevel1SearchCenter;
    y_search_area_origin = -(search_area_height >> 1) + yLevel1SearchCenter;

    // Correct the left edge of the Search Area if it is not on the reference
    // Picture
    x_search_area_origin = ((origin_x + x_search_area_origin) < -pad_width) ? -pad_width - origin_x
                                                                            : x_search_area_origin;

    search_area_width = ((origin_x + x_search_area_origin) < -pad_width)
                            ? search_area_width - (-pad_width - (origin_x + x_search_area_origin))
                            : search_area_width;

    // Correct the right edge of the Search Area if its not on the reference
    // Picture
    x_search_area_origin = ((origin_x + x_search_area_origin) > (int16_t)ref_pic_ptr->width - 1)
                               ? x_search_area_origin - ((origin_x + x_search_area_origin) -
                                                         ((int16_t)ref_pic_ptr->width - 1))
                               : x_search_area_origin;

    search_area_width =
        ((origin_x + x_search_area_origin + search_area_width) > (int16_t)ref_pic_ptr->width)
            ? MAX(1,
                  search_area_width - ((origin_x + x_search_area_origin + search_area_width) -
                                       (int16_t)ref_pic_ptr->width))
            : search_area_width;

    // Constrain x_HME_L1 to be a multiple of 8 (round down as cropping already
    // performed)
    search_area_width = (search_area_width < 8) ? search_area_width : search_area_width & ~0x07;
    // Correct the top edge of the Search Area if it is not on the reference
    // Picture
    y_search_area_origin = ((origin_y + y_search_area_origin) < -pad_height)
                               ? -pad_height - origin_y
                               : y_search_area_origin;

    search_area_height =
        ((origin_y + y_search_area_origin) < -pad_height)
            ? search_area_height - (-pad_height - (origin_y + y_search_area_origin))
            : search_area_height;

    // Correct the bottom edge of the Search Area if its not on the reference
    // Picture
    y_search_area_origin = ((origin_y + y_search_area_origin) > (int16_t)ref_pic_ptr->height - 1)
                               ? y_search_area_origin - ((origin_y + y_search_area_origin) -
                                                         ((int16_t)ref_pic_ptr->height - 1))
                               : y_search_area_origin;

    search_area_height =
        (origin_y + y_search_area_origin + search_area_height > (int16_t)ref_pic_ptr->height)
            ? MAX(1,
                  search_area_height - ((origin_y + y_search_area_origin + search_area_height) -
                                        (int16_t)ref_pic_ptr->height))
            : search_area_height;

    // Move to the top left of the search region
    x_top_left_search_region = ((int16_t)ref_pic_ptr->origin_x + origin_x) + x_search_area_origin;
    y_top_left_search_region = ((int16_t)ref_pic_ptr->origin_y + origin_y) + y_search_area_origin;
    search_region_index =
        x_top_left_search_region + y_top_left_search_region * ref_pic_ptr->stride_y;

    // Put the first search location into level0 results
    svt_sad_loop_kernel(
        context_ptr->sb_src_ptr,
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? context_ptr->sb_src_stride
                                                            : context_ptr->sb_src_stride * 2,
        &ref_pic_ptr->buffer_y[search_region_index],
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? ref_pic_ptr->stride_y
                                                            : ref_pic_ptr->stride_y * 2,
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? sb_height : sb_height >> 1,
        sb_width,
        /* results */
        level2Bestsad_,
        xLevel2SearchCenter,
        yLevel2SearchCenter,
        /* range */
        ref_pic_ptr->stride_y,
#if FTR_PREHME_SUB
        0,
#endif
        search_area_width,
        search_area_height);

    *level2Bestsad_ =
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
            ? *level2Bestsad_
            : *level2Bestsad_ * 2; // Multiply by 2 because considered only ever other line
    *xLevel2SearchCenter += x_search_area_origin;
    *yLevel2SearchCenter += y_search_area_origin;

    return;
}

// Nader - to be replaced by loock-up table
/*******************************************
 * get_me_info_index
 *   search the correct index of the motion
 *   info that corresponds to the input
 *   md candidate
 *******************************************/
uint32_t get_me_info_index(uint32_t max_me_block, const BlockGeom *blk_geom, uint32_t geom_offset_x,
                           uint32_t geom_offset_y) {
    // search for motion info
    uint32_t block_index;
    uint32_t me_info_index = 0xFFFFFFF;

    for (block_index = 0; block_index < max_me_block; block_index++) {
        if ((blk_geom->bwidth == partition_width[block_index]) &&
            (blk_geom->bheight == partition_height[block_index]) &&
            ((blk_geom->origin_x - geom_offset_x) == pu_search_index_map[block_index][0]) &&
            ((blk_geom->origin_y - geom_offset_y) == pu_search_index_map[block_index][1])) {
            me_info_index = block_index;
            break;
        }
    }
    return me_info_index;
}

#define NSET_CAND(me_pu_result, num, dist, dir)                      \
    (me_pu_result)->distortion_direction[(num)].distortion = (dist); \
    (me_pu_result)->distortion_direction[(num)].direction  = (dir);

#if FTR_ADJUST_SR_FOR_STILL
uint16_t check_00_center(EbPictureBufferDesc *ref_pic_ptr, MeContext *context_ptr,
#else
EbErrorType check_00_center(EbPictureBufferDesc *ref_pic_ptr, MeContext *context_ptr,
#endif
                            uint32_t sb_origin_x, uint32_t sb_origin_y, uint32_t sb_width,
#if FTR_HME_ME_EARLY_EXIT
    uint32_t sb_height, int16_t *x_search_center, int16_t *y_search_center, uint32_t zz_sad)
#else
                            uint32_t sb_height, int16_t *x_search_center, int16_t *y_search_center)
#endif

{
#if !FTR_ADJUST_SR_FOR_STILL
    EbErrorType return_error = EB_ErrorNone;
#endif
    uint32_t    search_region_index, zero_mv_sad, hme_mv_sad, hme_mvd_rate;
    uint64_t    hme_mv_cost, zero_mv_cost, search_center_cost;
    int16_t     origin_x      = (int16_t)sb_origin_x;
    int16_t     origin_y      = (int16_t)sb_origin_y;
    uint32_t    subsample_sad = 1;
    int16_t     pad_width     = (int16_t)BLOCK_SIZE_64 - 1;
    int16_t     pad_height    = (int16_t)BLOCK_SIZE_64 - 1;

    search_region_index = (int16_t)ref_pic_ptr->origin_x + origin_x +
                          ((int16_t)ref_pic_ptr->origin_y + origin_y) * ref_pic_ptr->stride_y;
#if FTR_HME_ME_EARLY_EXIT
    if (context_ptr->me_early_exit_th && context_ptr->me_type != ME_MCTF)
        zero_mv_sad = zz_sad;
    else
#endif
    zero_mv_sad = svt_nxm_sad_kernel(context_ptr->sb_src_ptr,
                                     context_ptr->sb_src_stride << subsample_sad,
                                     &(ref_pic_ptr->buffer_y[search_region_index]),
                                     ref_pic_ptr->stride_y << subsample_sad,
                                     sb_height >> subsample_sad,
                                     sb_width);

    zero_mv_sad = zero_mv_sad << subsample_sad;

    // FIX
    // Correct the left edge of the Search Area if it is not on the reference
    // Picture
    *x_search_center =
        ((origin_x + *x_search_center) < -pad_width) ? -pad_width - origin_x : *x_search_center;
    // Correct the right edge of the Search Area if its not on the reference
    // Picture
    *x_search_center =
        ((origin_x + *x_search_center) > (int16_t)ref_pic_ptr->width - 1)
            ? *x_search_center - ((origin_x + *x_search_center) - ((int16_t)ref_pic_ptr->width - 1))
            : *x_search_center;
    // Correct the top edge of the Search Area if it is not on the reference
    // Picture
    *y_search_center =
        ((origin_y + *y_search_center) < -pad_height) ? -pad_height - origin_y : *y_search_center;
    // Correct the bottom edge of the Search Area if its not on the reference
    // Picture
    *y_search_center = ((origin_y + *y_search_center) > (int16_t)ref_pic_ptr->height - 1)
                           ? *y_search_center - ((origin_y + *y_search_center) -
                                                 ((int16_t)ref_pic_ptr->height - 1))
                           : *y_search_center;
    ///

    zero_mv_cost = zero_mv_sad << COST_PRECISION;
    search_region_index =
        (int16_t)(ref_pic_ptr->origin_x + origin_x) + *x_search_center +
        ((int16_t)(ref_pic_ptr->origin_y + origin_y) + *y_search_center) * ref_pic_ptr->stride_y;

    hme_mv_sad = svt_nxm_sad_kernel(context_ptr->sb_src_ptr,
                                    context_ptr->sb_src_stride << subsample_sad,
                                    &(ref_pic_ptr->buffer_y[search_region_index]),
                                    ref_pic_ptr->stride_y << subsample_sad,
                                    sb_height >> subsample_sad,
                                    sb_width);

    hme_mv_sad = hme_mv_sad << subsample_sad;

    hme_mvd_rate = 0;

    hme_mv_cost = (hme_mv_sad << COST_PRECISION) +
                  (((context_ptr->lambda * hme_mvd_rate) + MD_OFFSET) >> MD_SHIFT);
#if FIX_TF_HME
    // Apply a penalty to the HME_MV cost(@ the post - HME(0, 0) vs.HME_MV distortion check) when the HME_MV distortion is high (towards more search ~ (0,0) if difficult 64x64)
    if (context_ptr->me_type == ME_MCTF) {
        if (hme_mv_sad > TF_HME_MV_SAD_TH) {
            hme_mv_cost = (hme_mv_cost * TF_HME_MV_COST_WEIGHT) / 100;
        }
    }
#endif
    search_center_cost = MIN(zero_mv_cost, hme_mv_cost);

    *x_search_center = (search_center_cost == zero_mv_cost) ? 0 : *x_search_center;
    *y_search_center = (search_center_cost == zero_mv_cost) ? 0 : *y_search_center;
#if FTR_ADJUST_SR_FOR_STILL
    return hme_mv_sad;
#else
    return return_error;
#endif
}
// get ME references based on level:
// level: 0 => sixteenth, 1 => quarter, 2 => original


static EbPictureBufferDesc *get_me_reference(PictureParentControlSet *pcs_ptr,
                                             MeContext *context_ptr, uint8_t list_index,
                                             uint8_t ref_pic_index, uint8_t level, uint16_t *dist,
                                             uint16_t input_width, uint16_t input_height) {

    EbPictureBufferDesc* ref_pic_ptr;
    ref_pic_ptr = level == 0 ? context_ptr->me_ds_ref_array[list_index][ref_pic_index].sixteenth_picture_ptr :
                  level == 1 ? context_ptr->me_ds_ref_array[list_index][ref_pic_index].quarter_picture_ptr :
                               context_ptr->me_ds_ref_array[list_index][ref_pic_index].picture_ptr;

    if ((input_width >> (2 - level)) != ref_pic_ptr->width ||
        (input_height >> (2 - level)) != ref_pic_ptr->height) {
        SVT_WARN(
            "picture %3llu: HME level%d resolution mismatch! input (%dx%d) != (%dx%d) pa ref. \n",
            pcs_ptr->picture_number,
            level,
            input_width >> (2 - level),
            input_height >> (2 - level),
            ref_pic_ptr->width,
            ref_pic_ptr->height);
    }

    *dist = ABS((int16_t)(pcs_ptr->picture_number - context_ptr->me_ds_ref_array[list_index][ref_pic_index].picture_number));
    return ref_pic_ptr;
}



/*******************************************
 *   performs integer search motion estimation for
 all avaiable references frames
 *******************************************/


void integer_search_sb(
    PictureParentControlSet   *pcs_ptr,
    uint32_t                   sb_index,
    uint32_t                   sb_origin_x,
    uint32_t                   sb_origin_y,
    MeContext                 *context_ptr,
    EbPictureBufferDesc       *input_ptr) {

    SequenceControlSet *scs_ptr = pcs_ptr->scs_ptr;
    int16_t picture_width = pcs_ptr->aligned_width;
    int16_t picture_height = pcs_ptr->aligned_height;
    uint32_t sb_width = (input_ptr->width - sb_origin_x) < BLOCK_SIZE_64
                            ? input_ptr->width - sb_origin_x
                            : BLOCK_SIZE_64;
    uint32_t sb_height = (input_ptr->height - sb_origin_y) < BLOCK_SIZE_64
                             ? input_ptr->height - sb_origin_y
                             : BLOCK_SIZE_64;
    int16_t pad_width  = (int16_t)BLOCK_SIZE_64 - 1;
    int16_t pad_height = (int16_t)BLOCK_SIZE_64 - 1;
    int16_t origin_x = (int16_t)sb_origin_x;
    int16_t origin_y = (int16_t)sb_origin_y;
    int16_t search_area_width;
    int16_t search_area_height;
    int16_t x_search_area_origin;
    int16_t y_search_area_origin;
    int16_t  x_top_left_search_region;
    int16_t  y_top_left_search_region;
    uint32_t search_region_index;
    uint32_t num_of_list_to_search;
    uint32_t list_index;
    uint8_t ref_pic_index;
    // Final ME Search Center
    int16_t x_search_center = 0;
    int16_t y_search_center = 0;
    EbPictureBufferDesc *ref_pic_ptr;
    num_of_list_to_search = context_ptr->num_of_list_to_search;

    // Uni-Prediction motion estimation loop
    // List Loop
    for (list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        uint8_t num_of_ref_pic_to_search =
            context_ptr->num_of_ref_pic_to_search[list_index];

        // Ref Picture Loop
        for (ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
            uint16_t dist = 0;
            ref_pic_ptr   = get_me_reference(pcs_ptr,
                                           context_ptr,
                                           list_index,
                                           ref_pic_index,
                                           2,
                                           &dist,
                                           input_ptr->width,
                                           input_ptr->height);
            // Get hme results
            if (context_ptr->hme_results[list_index][ref_pic_index].do_ref == 0)
                continue;  //so will not get ME results for those references.
            x_search_center = context_ptr->hme_results[list_index][ref_pic_index].hme_sc_x;
            y_search_center = context_ptr->hme_results[list_index][ref_pic_index].hme_sc_y;
            search_area_width = context_ptr->search_area_width;
            search_area_height = context_ptr->search_area_height;

            // factor to slowdown the ME search region growth to MAX
            if (context_ptr->me_type != ME_MCTF) {
                int8_t round_up = ((dist%8) == 0) ? 0 : 1;
                dist = ((dist * 5) / 8) + round_up;
            }
            search_area_width = MIN((search_area_width*dist),context_ptr->max_me_search_width);
            search_area_height = MIN((search_area_height*dist),context_ptr->max_me_search_height);

            // Constrain x_ME to be a multiple of 8 (round up)
            // Update ME search reagion size based on hme-data
            search_area_width = ((search_area_width / context_ptr->reduce_me_sr_divisor[list_index][ref_pic_index]) + 7) & ~0x07;
            search_area_height = MAX(3, (search_area_height / context_ptr->reduce_me_sr_divisor[list_index][ref_pic_index]));
#if FTR_ADJUST_SR_USING_LIST0
            int16_t search_area_height_before_sr_reduction = search_area_height;
            int16_t search_area_width_before_sr_reduction = search_area_width;
#endif
#if FTR_ADJUST_SR_FOR_STILL
#if !FTR_HME_ME_EARLY_EXIT
            uint8_t  hme_is_accuarte = 1;
#endif
            uint64_t best_hme_sad = (uint64_t)~0;
#endif
#if FTR_HME_ME_EARLY_EXIT
                if (context_ptr->me_early_exit_th && context_ptr->me_type != ME_MCTF) {
                    if (context_ptr->zz_sad[list_index][ref_pic_index] < (context_ptr->me_early_exit_th / 6)) {
                        search_area_width = 1;
                        search_area_height = 1;
                    }
                }
                else {
                    uint8_t  hme_is_accuarte = 1;
#endif
            if (scs_ptr->enc_mode_2ndpass <= ENC_M4  || context_ptr->me_type != ME_FIRST_PASS)
            if ((x_search_center != 0 || y_search_center != 0) &&
                (context_ptr->is_used_as_reference_flag == EB_TRUE)) {
#if FTR_ADJUST_SR_FOR_STILL
                best_hme_sad = check_00_center(ref_pic_ptr,
#else
                check_00_center(ref_pic_ptr,
#endif
                                context_ptr,
                                sb_origin_x,
                                sb_origin_y,
                                sb_width,
                                sb_height,
                                &x_search_center,
                                &y_search_center
#if FTR_HME_ME_EARLY_EXIT
                                , context_ptr->zz_sad[list_index][ref_pic_index]
#endif
                );
#if FTR_ADJUST_SR_FOR_STILL
                if (x_search_center == 0 && y_search_center == 0)
                    hme_is_accuarte = 0;
#endif
            }
#if FTR_ADJUST_SR_FOR_STILL
            if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment == 2) {
                if ((hme_is_accuarte && (best_hme_sad < (24 * 24))) ||
                    (context_ptr->is_used_as_reference_flag && context_ptr->hme_results[list_index][ref_pic_index].hme_sad < (24 * 24))) {
                    search_area_height = search_area_height / 2;
                }
            }
#endif
#if FTR_ADJUST_SR_USING_LIST0
            if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment == 2) {
                if (list_index || ref_pic_index) {
                    if (context_ptr->p_sb_best_sad[0][0][0] < 5000)
                        if (search_area_height == search_area_height_before_sr_reduction && search_area_width == search_area_width_before_sr_reduction) {
                            search_area_height = search_area_height >> 1;
                            search_area_width = search_area_width >> 1;
                        }
                }
            }
#endif
#if FTR_HME_ME_EARLY_EXIT
            }
#endif

            x_search_area_origin = x_search_center - (search_area_width >> 1);
            y_search_area_origin = y_search_center - (search_area_height >> 1);

            if (scs_ptr->static_config.unrestricted_motion_vector == 0) {
                // sb_params_array in scs and ppcs are different when super-res is enabled
                // ME_OPEN_LOOP is performed on downscaled frames while others (ME_MCTF and ME_FIRST_PASS) are performed on unscaled frames
                SbParams * sb_params_array = context_ptr->me_type != ME_OPEN_LOOP ? scs_ptr->sb_params_array : pcs_ptr->sb_params_array;
                int tile_start_x = sb_params_array[sb_index].tile_start_x;
                int tile_end_x   = sb_params_array[sb_index].tile_end_x;
                // Correct the left edge of the Search Area if it is not on the
                // reference Picture
                x_search_area_origin = ((origin_x + x_search_area_origin) < tile_start_x)
                                           ? tile_start_x - origin_x
                                           : x_search_area_origin;
                search_area_width =
                    ((origin_x + x_search_area_origin) < tile_start_x)
                        ? search_area_width - (tile_start_x - (origin_x + x_search_area_origin))
                        : search_area_width;
                // Correct the right edge of the Search Area if its not on the
                // reference Picture
                x_search_area_origin =
                    ((origin_x + x_search_area_origin) > tile_end_x - 1)
                        ? x_search_area_origin -
                              ((origin_x + x_search_area_origin) - (tile_end_x - 1))
                        : x_search_area_origin;
                search_area_width =
                    ((origin_x + x_search_area_origin + search_area_width) > tile_end_x)
                        ? MAX(1,
                              search_area_width -
                                  ((origin_x + x_search_area_origin + search_area_width) -
                                   tile_end_x))
                        : search_area_width;
                // Constrain x_ME to be a multiple of 8 (round down as cropping
                // already performed)
                search_area_width =
                    (search_area_width < 8) ? search_area_width : search_area_width & ~0x07;
            } else {
                // Correct the left edge of the Search Area if it is not on the
                // reference Picture
                x_search_area_origin = ((origin_x + x_search_area_origin) < -pad_width)
                                           ? -pad_width - origin_x
                                           : x_search_area_origin;
                search_area_width =
                    ((origin_x + x_search_area_origin) < -pad_width)
                        ? search_area_width - (-pad_width - (origin_x + x_search_area_origin))
                        : search_area_width;
                // Correct the right edge of the Search Area if its not on the
                // reference Picture
                x_search_area_origin =
                    ((origin_x + x_search_area_origin) > picture_width - 1)
                        ? x_search_area_origin -
                              ((origin_x + x_search_area_origin) - (picture_width - 1))
                        : x_search_area_origin;

                search_area_width =
                    ((origin_x + x_search_area_origin + search_area_width) > picture_width)
                        ? MAX(1,
                              search_area_width -
                                  ((origin_x + x_search_area_origin + search_area_width) -
                                   picture_width))
                        : search_area_width;

                // Constrain x_ME to be a multiple of 8 (round down as cropping
                // already performed)
                search_area_width =
                    (search_area_width < 8) ? search_area_width : search_area_width & ~0x07;
            }
            if (scs_ptr->static_config.unrestricted_motion_vector == 0) {
                // sb_params_array in scs and ppcs are different when super-res is enabled
                // ME_OPEN_LOOP is performed on downscaled frames while others (ME_MCTF and ME_FIRST_PASS) are performed on unscaled frames
                SbParams* sb_params_array = context_ptr->me_type != ME_OPEN_LOOP ? scs_ptr->sb_params_array : pcs_ptr->sb_params_array;
                int tile_start_y = sb_params_array[sb_index].tile_start_y;
                int tile_end_y   = sb_params_array[sb_index].tile_end_y;
                // Correct the top edge of the Search Area if it is not on the
                // reference Picture
                y_search_area_origin = ((origin_y + y_search_area_origin) < tile_start_y)
                                           ? tile_start_y - origin_y
                                           : y_search_area_origin;

                search_area_height =
                    ((origin_y + y_search_area_origin) < tile_start_y)
                        ? search_area_height - (tile_start_y - (origin_y + y_search_area_origin))
                        : search_area_height;

                // Correct the bottom edge of the Search Area if its not on the
                // reference Picture
                y_search_area_origin =
                    ((origin_y + y_search_area_origin) > tile_end_y - 1)
                        ? y_search_area_origin -
                              ((origin_y + y_search_area_origin) - (tile_end_y - 1))
                        : y_search_area_origin;

                search_area_height =
                    (origin_y + y_search_area_origin + search_area_height > tile_end_y)
                        ? MAX(1,
                              search_area_height -
                                  ((origin_y + y_search_area_origin + search_area_height) -
                                   tile_end_y))
                        : search_area_height;
            } else {
                // Correct the top edge of the Search Area if it is not on the
                // reference Picture
                y_search_area_origin = ((origin_y + y_search_area_origin) < -pad_height)
                                           ? -pad_height - origin_y
                                           : y_search_area_origin;
                search_area_height =
                    ((origin_y + y_search_area_origin) < -pad_height)
                        ? search_area_height - (-pad_height - (origin_y + y_search_area_origin))
                        : search_area_height;
                // Correct the bottom edge of the Search Area if its not on the
                // reference Picture
                y_search_area_origin =
                    ((origin_y + y_search_area_origin) > picture_height - 1)
                        ? y_search_area_origin -
                              ((origin_y + y_search_area_origin) - (picture_height - 1))
                        : y_search_area_origin;
                search_area_height =
                    (origin_y + y_search_area_origin + search_area_height > picture_height)
                        ? MAX(1,
                              search_area_height -
                                  ((origin_y + y_search_area_origin + search_area_height) -
                                   picture_height))
                        : search_area_height;
            }
            context_ptr->adj_search_area_width  = search_area_width;
            context_ptr->adj_search_area_height = search_area_height;
            x_top_left_search_region = (int16_t)(ref_pic_ptr->origin_x + sb_origin_x) -
                                       (ME_FILTER_TAP >> 1) + x_search_area_origin;
            y_top_left_search_region = (int16_t)(ref_pic_ptr->origin_y + sb_origin_y) -
                                       (ME_FILTER_TAP >> 1) + y_search_area_origin;
            search_region_index =
                (x_top_left_search_region) + (y_top_left_search_region)*ref_pic_ptr->stride_y;
            context_ptr->integer_buffer_ptr[list_index][ref_pic_index] =
                &(ref_pic_ptr->buffer_y[search_region_index]);
            context_ptr->interpolated_full_stride[list_index][ref_pic_index] =
                ref_pic_ptr->stride_y;
            // Move to the top left of the search region
            x_top_left_search_region =
                (int16_t)(ref_pic_ptr->origin_x + sb_origin_x) + x_search_area_origin;
            y_top_left_search_region =
                (int16_t)(ref_pic_ptr->origin_y + sb_origin_y) + y_search_area_origin;
            search_region_index =
                x_top_left_search_region + y_top_left_search_region * ref_pic_ptr->stride_y;
                svt_initialize_buffer_32bits(
                            context_ptr->p_sb_best_sad[list_index][ref_pic_index],
                            21,
                            1,
                            MAX_SAD_VALUE);
                context_ptr->p_best_sad_64x64 = &(
                            context_ptr
                                ->p_sb_best_sad[list_index][ref_pic_index][ME_TIER_ZERO_PU_64x64]);
                        context_ptr->p_best_sad_32x32 =
                            &(context_ptr->p_sb_best_sad[list_index][ref_pic_index]
                                                        [ME_TIER_ZERO_PU_32x32_0]);
                        context_ptr->p_best_sad_16x16 =
                            &(context_ptr->p_sb_best_sad[list_index][ref_pic_index]
                                                        [ME_TIER_ZERO_PU_16x16_0]);
                        context_ptr->p_best_sad_8x8 = &(
                            context_ptr
                                ->p_sb_best_sad[list_index][ref_pic_index][ME_TIER_ZERO_PU_8x8_0]);

                        context_ptr->p_best_mv64x64 =
                            &(context_ptr
                                  ->p_sb_best_mv[list_index][ref_pic_index][ME_TIER_ZERO_PU_64x64]);
                        context_ptr->p_best_mv32x32 = &(
                            context_ptr
                                ->p_sb_best_mv[list_index][ref_pic_index][ME_TIER_ZERO_PU_32x32_0]);
                        context_ptr->p_best_mv16x16 = &(
                            context_ptr
                                ->p_sb_best_mv[list_index][ref_pic_index][ME_TIER_ZERO_PU_16x16_0]);
                        context_ptr->p_best_mv8x8 =
                            &(context_ptr
                                  ->p_sb_best_mv[list_index][ref_pic_index][ME_TIER_ZERO_PU_8x8_0]);
                        open_loop_me_fullpel_search_sblock(context_ptr,
                                                           list_index,
                                                           ref_pic_index,
                                                           x_search_area_origin,
                                                           y_search_area_origin,
                                                           search_area_width,
                                                           search_area_height);
        }
    }
}


/*
  using previous stage ME results (Integer Search) for each reference
  frame. keep only the references that are close to the best reference.
*/
void me_prune_ref(MeContext* context_ptr) {
    uint8_t num_of_list_to_search = context_ptr->num_of_list_to_search;
    for (uint8_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];
        // Ref Picture Loop
        for (uint8_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
            context_ptr->hme_results[list_index][ref_pic_index].hme_sad = 0;
            // Get hme results
            if (context_ptr->hme_results[list_index][ref_pic_index].do_ref == 0) {
                context_ptr->hme_results[list_index][ref_pic_index].hme_sad = MAX_SAD_VALUE * 64;
                continue;
            }
            context_ptr->p_best_sad_8x8 = &(context_ptr->p_sb_best_sad[list_index][ref_pic_index][ME_TIER_ZERO_PU_8x8_0]);
            // 8x8   [64 partitions]
            for (uint32_t pu_index = 0; pu_index < 64; ++pu_index) {
                uint32_t idx = tab8x8[pu_index];
                context_ptr->hme_results[list_index][ref_pic_index].hme_sad += context_ptr->p_best_sad_8x8[idx];
            }
        }
    }
    uint64_t best = (uint64_t)~0;
    for (int i = 0; i < MAX_NUM_OF_REF_PIC_LIST; ++i) {
        for (int j = 0; j < REF_LIST_MAX_DEPTH; ++j) {
            if (context_ptr->hme_results[i][j].hme_sad < best) {
                best = context_ptr->hme_results[i][j].hme_sad;
            }
        }
    }
    for (uint32_t li = 0; li < MAX_NUM_OF_REF_PIC_LIST; li++) {
        for (uint32_t ri = 0; ri < REF_LIST_MAX_DEPTH; ri++){
            // Prune references based on ME sad
            uint16_t prune_ref_th = context_ptr->me_hme_prune_ctrls.prune_ref_if_me_sad_dev_bigger_than_th;
            if (context_ptr->me_hme_prune_ctrls.enable_me_hme_ref_pruning &&
                (!context_ptr->me_hme_prune_ctrls.protect_closest_refs || ri > 0) &&
                (prune_ref_th != (uint16_t)~0) &&
                (context_ptr->hme_results[li][ri].hme_sad - best) * 100 > (prune_ref_th * best))
            {
                context_ptr->hme_results[li][ri].do_ref = 0;
            }
        }
    }
}

/* perform  motion search over a given search area*/
void prehme_core(
    MeContext *context_ptr,
    int16_t origin_x,
    int16_t origin_y,
    uint32_t sb_width,
    uint32_t sb_height,
    EbPictureBufferDesc *sixteenth_ref_pic_ptr,
    SearchInfo         *prehme_data
) {
    int16_t  x_top_left_search_region;
    int16_t  y_top_left_search_region;
    uint32_t search_region_index;


    int16_t pad_width = (int16_t)(sixteenth_ref_pic_ptr->origin_x) - 1;
    int16_t pad_height= (int16_t)(sixteenth_ref_pic_ptr->origin_y) - 1;

    int16_t search_area_width  = prehme_data->sa.width;
    int16_t search_area_height = prehme_data->sa.height;

    int16_t  x_search_area_origin = -(int16_t)(search_area_width  >> 1);
    int16_t  y_search_area_origin = -(int16_t)(search_area_height >> 1);

    // Correct the left edge of the Search Area if it is not on the reference Picture
    x_search_area_origin = ((origin_x + x_search_area_origin) < -pad_width)
        ? -pad_width - origin_x  :
        x_search_area_origin;

    search_area_width = ((origin_x + x_search_area_origin) < -pad_width)
        ? search_area_width - (-pad_width - (origin_x + x_search_area_origin))
        : search_area_width;

    // Correct the right edge of the Search Area if its not on the reference Picture
    x_search_area_origin =
        ((origin_x + x_search_area_origin) > (int16_t)sixteenth_ref_pic_ptr->width - 1)
        ? x_search_area_origin -
        ((origin_x + x_search_area_origin) - ((int16_t)sixteenth_ref_pic_ptr->width - 1))
        : x_search_area_origin;

    search_area_width =
        ((origin_x + x_search_area_origin + search_area_width) >
        (int16_t)sixteenth_ref_pic_ptr->width)
        ? MAX(1,
            search_area_width - ((origin_x + x_search_area_origin + search_area_width) -
            (int16_t)sixteenth_ref_pic_ptr->width))
        : search_area_width;


    // Correct the top edge of the Search Area if it is not on the reference Picture
    y_search_area_origin = ((origin_y + y_search_area_origin) < -pad_height)
        ? -pad_height - origin_y
        : y_search_area_origin;

    search_area_height =
        ((origin_y + y_search_area_origin) < -pad_height)
        ? search_area_height - (-pad_height - (origin_y + y_search_area_origin))
        : search_area_height;

    // Correct the bottom edge of the Search Area if its not on the reference Picture
    y_search_area_origin =
        ((origin_y + y_search_area_origin) > (int16_t)sixteenth_ref_pic_ptr->height - 1)
        ? y_search_area_origin -
        ((origin_y + y_search_area_origin) - ((int16_t)sixteenth_ref_pic_ptr->height - 1))
        : y_search_area_origin;

    search_area_height =
        (origin_y + y_search_area_origin + search_area_height >
        (int16_t)sixteenth_ref_pic_ptr->height)
        ? MAX(1,
            search_area_height - ((origin_y + y_search_area_origin + search_area_height) -
            (int16_t)sixteenth_ref_pic_ptr->height))
        : search_area_height;

    x_top_left_search_region =
        ((int16_t)sixteenth_ref_pic_ptr->origin_x + origin_x) + x_search_area_origin;
    y_top_left_search_region =
        ((int16_t)sixteenth_ref_pic_ptr->origin_y + origin_y) + y_search_area_origin;
    search_region_index =
        x_top_left_search_region + y_top_left_search_region * sixteenth_ref_pic_ptr->stride_y;


    svt_sad_loop_kernel(
        &context_ptr->sixteenth_sb_buffer[0],
        context_ptr->hme_search_method == FULL_SAD_SEARCH ? context_ptr->sixteenth_sb_buffer_stride : context_ptr->sixteenth_sb_buffer_stride * 2,
        &sixteenth_ref_pic_ptr->buffer_y[search_region_index],
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
        ? sixteenth_ref_pic_ptr->stride_y
        : sixteenth_ref_pic_ptr->stride_y * 2,
        (context_ptr->hme_search_method == FULL_SAD_SEARCH) ? sb_height : sb_height >> 1,
        sb_width,
        /* results */
        &prehme_data->sad,
        &prehme_data->best_mv.as_mv.col,
        &prehme_data->best_mv.as_mv.row,
        sixteenth_ref_pic_ptr->stride_y,
#if FTR_PREHME_SUB
         context_ptr->prehme_ctrl.skip_search_line,
#endif
        search_area_width,
        search_area_height);

    prehme_data->sad =
        (context_ptr->hme_search_method == FULL_SAD_SEARCH)
        ? prehme_data->sad
        : prehme_data->sad * 2; // Multiply by 2 because considered only ever other line
    prehme_data->best_mv.as_mv.col += x_search_area_origin;
    prehme_data->best_mv.as_mv.col *= 4; // Multiply by 4 because operating on 1/4 resolution
    prehme_data->best_mv.as_mv.row += y_search_area_origin;
    prehme_data->best_mv.as_mv.row *= 4; // Multiply by 4 because operating on 1/4 resolution

    return;
}
#if FTR_HME_ME_EARLY_EXIT
uint32_t get_zz_sad(EbPictureBufferDesc *ref_pic_ptr, MeContext *context_ptr,
    uint32_t sb_origin_x, uint32_t sb_origin_y, uint32_t sb_width,
    uint32_t sb_height)

{

    uint32_t    search_region_index, zero_mv_sad;
    int16_t     origin_x = (int16_t)sb_origin_x;
    int16_t     origin_y = (int16_t)sb_origin_y;
    uint32_t    subsample_sad = 1;

    search_region_index = (int16_t)ref_pic_ptr->origin_x + origin_x +
        ((int16_t)ref_pic_ptr->origin_y + origin_y) * ref_pic_ptr->stride_y;

    zero_mv_sad = svt_nxm_sad_kernel(context_ptr->sb_src_ptr,
        context_ptr->sb_src_stride << subsample_sad,
        &(ref_pic_ptr->buffer_y[search_region_index]),
        ref_pic_ptr->stride_y << subsample_sad,
        sb_height >> subsample_sad,
        sb_width);

    zero_mv_sad = zero_mv_sad << subsample_sad;

    return zero_mv_sad;
}
#endif
/* Pre HME for one Block 64x64*/
static void prehme_sb(
    PictureParentControlSet *pcs_ptr, uint32_t sb_origin_x,
    uint32_t sb_origin_y, MeContext *ctx,
    EbPictureBufferDesc *input_ptr)
{
    const uint32_t sb_width = (input_ptr->width - sb_origin_x) < BLOCK_SIZE_64  ? input_ptr->width - sb_origin_x  : BLOCK_SIZE_64;
    const uint32_t sb_height = (input_ptr->height - sb_origin_y) < BLOCK_SIZE_64 ? input_ptr->height - sb_origin_y : BLOCK_SIZE_64;
    const int16_t origin_x = (int16_t)sb_origin_x;
    const int16_t origin_y = (int16_t)sb_origin_y;


    for (int list_i = REF_LIST_0; list_i <= ctx->num_of_list_to_search; ++list_i) {
        uint8_t num_of_ref_pic_to_search = ctx->num_of_ref_pic_to_search[list_i];

        for (uint8_t ref_i = 0; ref_i < num_of_ref_pic_to_search; ++ref_i) {
            uint16_t dist = 0;
            EbPictureBufferDesc *sixteenthRefPicPtr = get_me_reference(
                pcs_ptr, ctx, list_i, ref_i, 0, &dist, input_ptr->width, input_ptr->height);

            if (ctx->temporal_layer_index > 0 || list_i == 0)
            {

#if FTR_HME_ME_EARLY_EXIT
                EbPictureBufferDesc *refpic = ctx->me_ds_ref_array[list_i][ref_i].picture_ptr;
                uint32_t zz_sad = (uint32_t)~0;
                if (ctx->me_early_exit_th && ctx->me_type != ME_MCTF) {
                    zz_sad = get_zz_sad(refpic, ctx, sb_origin_x, sb_origin_y, sb_width, sb_height);
                    ctx->zz_sad[list_i][ref_i] = zz_sad;
                }
#endif
                int32_t hme_sr_factor_x, hme_sr_factor_y;

                // factor to scaledown the ME search region growth to MAX
                int8_t   round_up = ((dist % 8) == 0) ? 0 : 1;
                uint16_t exp = 5;
                dist = ((dist * exp) / 8) + round_up;
                hme_sr_factor_x = dist * 100;
                hme_sr_factor_y = dist * 100;

                for (uint8_t sr_i = 0; sr_i < SEARCH_REGION_COUNT; sr_i++) {
#if FTR_HME_ME_EARLY_EXIT
                    if (ctx->me_early_exit_th && ctx->me_type != ME_MCTF) {
                        if (zz_sad < ctx->me_early_exit_th) {
                            ctx->prehme_data[list_i][ref_i][sr_i].best_mv.as_mv.col = 0;
                            ctx->prehme_data[list_i][ref_i][sr_i].best_mv.as_mv.row = 0;
                            ctx->prehme_data[list_i][ref_i][sr_i].sad = 0;
                            continue;
                        }
                    }
#endif
                   SearchInfo *prehme_data = &ctx->prehme_data[list_i][ref_i][sr_i];
#if FTR_PREHME_OPT
                   if (ctx->prehme_ctrl.l1_early_exit) {
                       if ((list_i == 1) && ((ctx->prehme_data[0][ref_i][sr_i].sad < (32 * 32)) ||
                           ((ABS(ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.col) < 16) &&
                           (ABS(ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.row) < 16)))) {
                           ctx->prehme_data[1][ref_i][sr_i].best_mv.as_mv.col = -ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.col;
                           ctx->prehme_data[1][ref_i][sr_i].best_mv.as_mv.row = -ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.row;
                           ctx->prehme_data[1][ref_i][sr_i].sad = ctx->prehme_data[0][ref_i][sr_i].sad;
                           continue;
                       }
                   }
#endif





#if OPT_PREHME
                   if (ctx->prehme_ctrl.use_tf_motion) {
                       if (pcs_ptr->temporal_layer_index > 0 || pcs_ptr->scs_ptr->input_resolution < INPUT_SIZE_1080p_RANGE) {

                           if (pcs_ptr->tf_motion_direction == 0) { //horz motion
                               if (sr_i == 0) { //vertical sa
                                   ctx->prehme_data[list_i][ref_i][sr_i].best_mv.as_int = 0;
                                   ctx->prehme_data[list_i][ref_i][sr_i].sad = (uint64_t)~0;
                                   continue;
                               }
                           }
                           else if (pcs_ptr->tf_motion_direction == 1) { //vert motion

                               if (sr_i == 1) { //horz sa
                                   ctx->prehme_data[list_i][ref_i][sr_i].best_mv.as_int = 0;
                                   ctx->prehme_data[list_i][ref_i][sr_i].sad = (uint64_t)~0;
                                   continue;
                               }
                           }
                       }
                   }
#endif

                   prehme_data->sa.width =
                        MIN((ctx->prehme_ctrl.prehme_sa_cfg[sr_i].sa_min.width *hme_sr_factor_x) / 100,
                            ctx->prehme_ctrl.prehme_sa_cfg[sr_i].sa_max.width
                        );
                   prehme_data->sa.height =
                       MIN((ctx->prehme_ctrl.prehme_sa_cfg[sr_i].sa_min.height *hme_sr_factor_y) / 100,
                           ctx->prehme_ctrl.prehme_sa_cfg[sr_i].sa_max.height
                       );

                        prehme_core(
                            ctx,
                            origin_x >> 2,
                            origin_y >> 2,
                            sb_width >> 2,
                            sb_height >> 2,
                            sixteenthRefPicPtr,
                            prehme_data
                            );

                }
            }
            else {
#if FTR_PREHME_OPT
                for (uint8_t sr_i = 0; sr_i < SEARCH_REGION_COUNT; sr_i++) {
                    ctx->prehme_data[1][ref_i][sr_i].best_mv.as_mv.col = -ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.col;
                    ctx->prehme_data[1][ref_i][sr_i].best_mv.as_mv.row = -ctx->prehme_data[0][ref_i][sr_i].best_mv.as_mv.row;
                    ctx->prehme_data[1][ref_i][sr_i].sad = ctx->prehme_data[0][ref_i][sr_i].sad;
                }
#else
                ctx->prehme_data[list_i][ref_i][0].sad = 16 * 16 * 255;
#endif
            }

        }
    }
}


/*******************************************
 *   performs hierarchical ME level 0
 *******************************************/


static void hme_level0_sb(
                         PictureParentControlSet *pcs_ptr, uint32_t sb_origin_x,
                          uint32_t sb_origin_y, MeContext *context_ptr,
                          EbPictureBufferDesc *input_ptr) {
    const uint32_t      sb_width = (input_ptr->width - sb_origin_x) < BLOCK_SIZE_64
        ? input_ptr->width - sb_origin_x
        : BLOCK_SIZE_64;
    const uint32_t sb_height = (input_ptr->height - sb_origin_y) < BLOCK_SIZE_64
        ? input_ptr->height - sb_origin_y
        : BLOCK_SIZE_64;
    const int16_t origin_x = (int16_t)sb_origin_x;
    const int16_t origin_y = (int16_t)sb_origin_y;
    const int num_of_list_to_search = context_ptr->num_of_list_to_search;

    // HME
    uint32_t search_region_number_in_width  = 0;
    uint32_t search_region_number_in_height = 0;

    // store base HME sizes, to be used if using ref-index based HME resizing
    uint16_t base_hme_search_width = context_ptr->hme_level0_total_search_area_width;
    uint16_t base_hme_search_height = context_ptr->hme_level0_total_search_area_height;
    uint16_t base_hme_max_search_width = context_ptr->hme_level0_max_total_search_area_width;
    uint16_t base_hme_max_search_height = context_ptr->hme_level0_max_total_search_area_height;

    // Uni-Prediction motion estimation loop
    // List Loop
    for (int list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];

        // Ref Picture Loop
        for (uint8_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
#if FTR_HME_ME_EARLY_EXIT
            if (context_ptr->me_early_exit_th && context_ptr->me_type != ME_MCTF) {
                if (context_ptr->zz_sad[list_index][ref_pic_index] < (context_ptr->me_early_exit_th >> 2)) {
#if FIX_HME_ME_EARLY_EXIT
                    for (uint32_t sr_idx_y = 0; sr_idx_y < context_ptr->number_hme_search_region_in_height; sr_idx_y++) {
                        for (uint32_t sr_idx_x = 0; sr_idx_x < context_ptr->number_hme_search_region_in_width; sr_idx_x++) {
                            context_ptr->x_hme_level0_search_center[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                            context_ptr->y_hme_level0_search_center[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                            context_ptr->hme_level0_sad[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                        }
                    }
#else
                    context_ptr->x_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
                    context_ptr->y_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
                    context_ptr->hme_level0_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
#endif
                    continue;
                }
            }
#endif
            uint16_t dist = 0;
            EbPictureBufferDesc *sixteenthRefPicPtr = get_me_reference(pcs_ptr,
                                                                       context_ptr,
                                                                       list_index,
                                                                       ref_pic_index,
                                                                       0,
                                                                       &dist,
                                                                       input_ptr->width,
                                                                       input_ptr->height);
            if (context_ptr->temporal_layer_index > 0 || list_index == 0) {
                int16_t x_search_center = 0;
                int16_t y_search_center = 0;
                        search_region_number_in_height = 0;
                        search_region_number_in_width  = 0;
                        int32_t hme_sr_factor_x, hme_sr_factor_y;
                        // factor to scaledown the ME search region growth to MAX
                        int8_t   round_up = ((dist % 8) == 0) ? 0 : 1;
                        uint16_t exp      = 5;
                        dist              = ((dist * exp) / 8) + round_up;
                        hme_sr_factor_x   = dist * 100;
                        hme_sr_factor_y   = dist * 100;
                        uint8_t is_hor = 1;
                        uint8_t is_ver = 1;
#if FTR_ADJUST_SR_FOR_STILL
                        uint8_t is_still = 0;
#endif
                        if (context_ptr->reduce_hme_l0_sr_th_min || context_ptr->reduce_hme_l0_sr_th_max) {
                            if (list_index || ref_pic_index) {
                                int16_t l0_mvx = context_ptr->x_hme_level0_search_center[0][0][search_region_number_in_width][search_region_number_in_height];
                                int16_t l0_mvy = context_ptr->y_hme_level0_search_center[0][0][search_region_number_in_width][search_region_number_in_height];// Determine whether the computed motion from list0/ref_index0 is in vertical or horizintal direction
                                is_ver = ((ABS(l0_mvx) < context_ptr->reduce_hme_l0_sr_th_min) && (ABS(l0_mvy) > context_ptr->reduce_hme_l0_sr_th_max));
                                is_hor = ((ABS(l0_mvx) > context_ptr->reduce_hme_l0_sr_th_max) && (ABS(l0_mvy) < context_ptr->reduce_hme_l0_sr_th_min));
#if FTR_ADJUST_SR_FOR_STILL
                                is_still = ((ABS(l0_mvx) < (context_ptr->reduce_hme_l0_sr_th_min * 3)) && (ABS(l0_mvy) < (context_ptr->reduce_hme_l0_sr_th_min * 3)));
#endif
                            }
                        }
                        // reduce HME search area for higher ref indices
                        if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment && context_ptr->me_sr_adjustment_ctrls.distance_based_hme_resizing) {
                            uint8_t x_offset = 1;
                            uint8_t y_offset = 1;
                            if (!is_ver) {
                                y_offset = 2;
                            }
                            if (!is_hor) {
                                x_offset = 2;
                            }
#if FTR_ADJUST_SR_FOR_STILL
                            if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment == 2) {
                                if (is_still) {
                                    x_offset = 4;
                                    y_offset = 4;
                                }
                            }
#endif
                            context_ptr->hme_level0_total_search_area_width = base_hme_search_width / (x_offset + ref_pic_index);
                            context_ptr->hme_level0_total_search_area_height = base_hme_search_height / (y_offset + ref_pic_index);
                            context_ptr->hme_level0_max_total_search_area_width = base_hme_max_search_width / (x_offset + ref_pic_index);
                            context_ptr->hme_level0_max_total_search_area_height = base_hme_max_search_height / (y_offset + ref_pic_index);
                            context_ptr->hme_level0_max_search_area_in_width_array[0] =
                                context_ptr->hme_level0_max_search_area_in_width_array[1] =
                                context_ptr->hme_level0_max_total_search_area_width / context_ptr->number_hme_search_region_in_width;
                            context_ptr->hme_level0_max_search_area_in_height_array[0] =
                                context_ptr->hme_level0_max_search_area_in_height_array[1] =
                                context_ptr->hme_level0_max_total_search_area_height / context_ptr->number_hme_search_region_in_height;
                            context_ptr->hme_level0_search_area_in_width_array[0] =
                                context_ptr->hme_level0_search_area_in_width_array[1] =
                                context_ptr->hme_level0_total_search_area_width / context_ptr->number_hme_search_region_in_width;
                            context_ptr->hme_level0_search_area_in_height_array[0] =
                                context_ptr->hme_level0_search_area_in_height_array[1] =
                                context_ptr->hme_level0_total_search_area_height / context_ptr->number_hme_search_region_in_height;
                        }
                        while (search_region_number_in_height <
                               context_ptr->number_hme_search_region_in_height) {
                            while (search_region_number_in_width <
                                   context_ptr->number_hme_search_region_in_width) {
                                hme_level_0(
                                    pcs_ptr,
                                    context_ptr,
                                    origin_x >> 2,
                                    origin_y >> 2,
                                    sb_width >> 2,
                                    sb_height >> 2,
                                    x_search_center >> 2,
                                    y_search_center >> 2,
                                    sixteenthRefPicPtr,
                                    search_region_number_in_width,
                                    search_region_number_in_height,
                                    &(context_ptr->hme_level0_sad[list_index][ref_pic_index]
                                                                 [search_region_number_in_width]
                                                                 [search_region_number_in_height]),
                                    &(context_ptr->x_hme_level0_search_center
                                          [list_index][ref_pic_index][search_region_number_in_width]
                                          [search_region_number_in_height]),
                                    &(context_ptr->y_hme_level0_search_center
                                          [list_index][ref_pic_index][search_region_number_in_width]
                                          [search_region_number_in_height]),
                                    hme_sr_factor_x,
                                    hme_sr_factor_y);
                                search_region_number_in_width++;
                            }
                            search_region_number_in_width = 0;
                            search_region_number_in_height++;
                        }
                        // reset base HME area
                        if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment && context_ptr->me_sr_adjustment_ctrls.distance_based_hme_resizing) {
                            context_ptr->hme_level0_total_search_area_width = base_hme_search_width;
                            context_ptr->hme_level0_total_search_area_height = base_hme_search_height;
                            context_ptr->hme_level0_max_total_search_area_width = base_hme_max_search_width;
                            context_ptr->hme_level0_max_total_search_area_height = base_hme_max_search_height;

                            context_ptr->hme_level0_max_search_area_in_width_array[0] =
                            context_ptr->hme_level0_max_search_area_in_width_array[1] =
                            context_ptr->hme_level0_max_total_search_area_width / context_ptr->number_hme_search_region_in_width;
                            context_ptr->hme_level0_max_search_area_in_height_array[0] =
                            context_ptr->hme_level0_max_search_area_in_height_array[1] =
                            context_ptr->hme_level0_max_total_search_area_height / context_ptr->number_hme_search_region_in_height;
                            context_ptr->hme_level0_search_area_in_width_array[0] =
                            context_ptr->hme_level0_search_area_in_width_array[1] =
                            context_ptr->hme_level0_total_search_area_width / context_ptr->number_hme_search_region_in_width;
                            context_ptr->hme_level0_search_area_in_height_array[0] =
                            context_ptr->hme_level0_search_area_in_height_array[1] =
                            context_ptr->hme_level0_total_search_area_height / context_ptr->number_hme_search_region_in_height;
                        }
                if (context_ptr->prehme_ctrl.enable) {

                    //get the worst quadrant
                    uint64_t max_sad = 0; uint8_t sr_h_max = 0, sr_w_max = 0;
                    for (uint8_t sr_h = 0; sr_h < context_ptr->number_hme_search_region_in_height; sr_h++) {
                        for (uint8_t sr_w = 0; sr_w < context_ptr->number_hme_search_region_in_width; sr_w++) {
                            if (context_ptr->hme_level0_sad[list_index][ref_pic_index][sr_w][sr_h] > max_sad) {
                                max_sad = context_ptr->hme_level0_sad[list_index][ref_pic_index][sr_w][sr_h];
                                sr_h_max = sr_h;
                                sr_w_max = sr_w;
                            }
                        }
                    }
                    uint8_t sr_i = context_ptr->prehme_data[list_index][ref_pic_index][0].sad <= context_ptr->prehme_data[list_index][ref_pic_index][1].sad ? 0 : 1;
                    //replace worst with pre-hme
                    if (context_ptr->prehme_data[list_index][ref_pic_index][sr_i].sad <
                        context_ptr->hme_level0_sad[list_index][ref_pic_index][sr_w_max][sr_h_max]) {

                        context_ptr->hme_level0_sad[list_index][ref_pic_index][sr_w_max][sr_h_max] =
                            context_ptr->prehme_data[list_index][ref_pic_index][sr_i].sad;

#if FIX_PREHME_ADD
                        context_ptr->x_hme_level0_search_center[list_index][ref_pic_index][sr_w_max][sr_h_max] =
                            context_ptr->prehme_data[list_index][ref_pic_index][sr_i].best_mv.as_mv.col;

                        context_ptr->y_hme_level0_search_center[list_index][ref_pic_index][sr_w_max][sr_h_max] =
                            context_ptr->prehme_data[list_index][ref_pic_index][sr_i].best_mv.as_mv.row;
#else
                        context_ptr->x_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] =
                            context_ptr->prehme_data[list_index][ref_pic_index][sr_i].best_mv.as_mv.col;

                        context_ptr->y_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] =
                            context_ptr->prehme_data[list_index][ref_pic_index][sr_i].best_mv.as_mv.row;
#endif


                    }
                }




            }
        }
    }
}

/*******************************************
 *   performs hierarchical ME level 1
 *******************************************/


void hme_level1_sb(
    PictureParentControlSet   *pcs_ptr,
    uint32_t                   sb_origin_x,
    uint32_t                   sb_origin_y,
    MeContext                 *context_ptr,
    EbPictureBufferDesc       *input_ptr
)
{
    uint32_t sb_width = (input_ptr->width - sb_origin_x) < BLOCK_SIZE_64
                            ? input_ptr->width - sb_origin_x
                            : BLOCK_SIZE_64;
    uint32_t sb_height = (input_ptr->height - sb_origin_y) < BLOCK_SIZE_64
                             ? input_ptr->height - sb_origin_y
                             : BLOCK_SIZE_64;
    int16_t origin_x = (int16_t)sb_origin_x;
    int16_t origin_y = (int16_t)sb_origin_y;
    // HME
    uint32_t search_region_number_in_width = 0;
    uint32_t search_region_number_in_height = 0;
    const uint32_t num_of_list_to_search = context_ptr->num_of_list_to_search;

    // Uni-Prediction motion estimation loop
    // List Loop
    for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        const uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];
        // Ref Picture Loop
        for (uint8_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search;++ref_pic_index){

            uint16_t dist = 0;
            EbPictureBufferDesc *quarterRefPicPtr = get_me_reference(pcs_ptr,
                                                                     context_ptr,
                                                                     list_index,
                                                                     ref_pic_index,
                                                                     1,
                                                                     &dist,
                                                                     input_ptr->width,
                                                                     input_ptr->height);
            if (context_ptr->temporal_layer_index > 0 || list_index == 0) {
#if FTR_HME_ME_EARLY_EXIT
                if (context_ptr->me_early_exit_th && context_ptr->me_type != ME_MCTF) {
                    if (context_ptr->zz_sad[list_index][ref_pic_index] < (context_ptr->me_early_exit_th >> 2)) {
#if FIX_HME_ME_EARLY_EXIT
                        for (uint32_t sr_idx_y = 0; sr_idx_y < context_ptr->number_hme_search_region_in_height; sr_idx_y++) {
                            for (uint32_t sr_idx_x = 0; sr_idx_x < context_ptr->number_hme_search_region_in_width; sr_idx_x++) {
                                context_ptr->x_hme_level1_search_center[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                                context_ptr->y_hme_level1_search_center[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                                context_ptr->hme_level1_sad[list_index][ref_pic_index][sr_idx_x][sr_idx_y] = 0;
                            }
                        }
#else
                        context_ptr->x_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
                        context_ptr->y_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
                        context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] = 0;
#endif
                        continue;
                    }
                }
#endif
                        search_region_number_in_height = 0;
                        search_region_number_in_width = 0;

                        int32_t hme_sr_factor_x, hme_sr_factor_y;
                        // factor to scaledown the ME search region growth to MAX
                        int8_t round_up = ((dist % 8) == 0) ? 0 : 1;
                        uint16_t exp = 5;
                        dist = ((dist * exp) / 8) + round_up;
                        hme_sr_factor_x = dist * 100;
                        hme_sr_factor_y = dist * 100;
                        while (search_region_number_in_height < context_ptr->number_hme_search_region_in_height) {
                            while (search_region_number_in_width < context_ptr->number_hme_search_region_in_width) {
                                int16_t hmeLevel1SearchAreaInWidth =
                                    context_ptr->hme_level1_search_area_in_width_array
                                        [search_region_number_in_width];
                                int16_t hmeLevel1SearchAreaInHeight =
                                    context_ptr->hme_level1_search_area_in_height_array
                                        [search_region_number_in_height];
                                int16_t hme_level1_max_search_area_width =
                                    context_ptr->hme_level1_search_area_in_width_array
                                        [search_region_number_in_width];
                                int16_t hme_level1_max_search_area_height =
                                    context_ptr->hme_level1_search_area_in_height_array
                                        [search_region_number_in_height];
                                if (context_ptr->hme_decimation <= ONE_DECIMATION_HME) {
                                    hmeLevel1SearchAreaInWidth = (int16_t)context_ptr->hme_level0_search_area_in_width_array[search_region_number_in_width];
                                    hmeLevel1SearchAreaInHeight = (int16_t)context_ptr->hme_level0_search_area_in_height_array[search_region_number_in_height];
                                    hme_level1_max_search_area_width = (int16_t)context_ptr->hme_level0_max_search_area_in_width_array[search_region_number_in_width];
                                    hme_level1_max_search_area_height = (int16_t)context_ptr->hme_level0_max_search_area_in_height_array[search_region_number_in_height];
                                }
                                hme_level_1(
                                    context_ptr,
                                    origin_x >> 1,
                                    origin_y >> 1,
                                    sb_width >> 1,
                                    sb_height >> 1,
                                    quarterRefPicPtr,
                                    hmeLevel1SearchAreaInWidth,
                                    hmeLevel1SearchAreaInHeight,
                                    hme_level1_max_search_area_width,
                                    hme_level1_max_search_area_height,
                                    hme_sr_factor_x,
                                    hme_sr_factor_y,
                                    context_ptr->x_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] >> 1,
                                    context_ptr->y_hme_level0_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] >> 1,
                                    &(context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]),
                                    &(context_ptr->x_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]),
                                    &(context_ptr->y_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]));

                                search_region_number_in_width++;
                            }
                            search_region_number_in_width = 0;
                            search_region_number_in_height++;
                        }
            }
        }
    }
}

/*******************************************
 *   performs hierarchical ME level 2
 *******************************************/



static void hme_level2_sb(
                          PictureParentControlSet *pcs_ptr, uint32_t sb_origin_x, uint32_t sb_origin_y,
                   MeContext *context_ptr, EbPictureBufferDesc *input_ptr) {
    const uint32_t sb_width = (input_ptr->width - sb_origin_x) < BLOCK_SIZE_64
        ? input_ptr->width - sb_origin_x
        : BLOCK_SIZE_64;
    const uint32_t sb_height = (input_ptr->height - sb_origin_y) < BLOCK_SIZE_64
        ? input_ptr->height - sb_origin_y
        : BLOCK_SIZE_64;
    const int16_t origin_x = (int16_t)sb_origin_x;
    const int16_t origin_y = (int16_t)sb_origin_y;
    // HME
    uint32_t search_region_number_in_width  = 0;
    uint32_t search_region_number_in_height = 0;
    const int num_of_list_to_search = context_ptr->num_of_list_to_search;
    // Uni-Prediction motion estimation loop
    // List Loop
    for (int list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];
        // Ref Picture Loop
        for (uint8_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
            uint16_t dist = 0;
            EbPictureBufferDesc *refPicPtr = get_me_reference(pcs_ptr,
                                                              context_ptr,
                                                              list_index,
                                                              ref_pic_index,
                                                              2,
                                                              &dist,
                                                              input_ptr->width,
                                                              input_ptr->height);

            if (context_ptr->temporal_layer_index > 0 || list_index == 0) {
                    // HME: Level2 search
                    search_region_number_in_height = 0;
                    search_region_number_in_width  = 0;
                    int32_t hme_sr_factor_x, hme_sr_factor_y;
                    // factor to scaledown the ME search region growth to MAX
                    int8_t   round_up = ((dist % 8) == 0) ? 0 : 1;
                    uint16_t exp      = 5;
                    dist              = ((dist * exp) / 8) + round_up;
                    hme_sr_factor_x   = dist * 100;
                    hme_sr_factor_y   = dist * 100;
                    while (search_region_number_in_height <
                           context_ptr->number_hme_search_region_in_height) {
                        while (search_region_number_in_width <
                               context_ptr->number_hme_search_region_in_width) {
                            int16_t hmeLevel2_search_area_in_width, hmeLevel2_search_area_in_height;
                            int16_t hmeLevel2_max_search_area_in_width,
                                hmeLevel2_max_search_area_in_height;
                            if (context_ptr->hme_decimation == ZERO_DECIMATION_HME) {
                                hmeLevel2_search_area_in_width =
                                    (int16_t)context_ptr->hme_level0_search_area_in_width_array
                                        [search_region_number_in_width];
                                hmeLevel2_search_area_in_height =
                                    (int16_t)context_ptr->hme_level0_search_area_in_height_array
                                        [search_region_number_in_height];
                                hmeLevel2_max_search_area_in_width =
                                    (int16_t)context_ptr->hme_level0_max_search_area_in_width_array
                                        [search_region_number_in_width];
                                hmeLevel2_max_search_area_in_height =
                                    (int16_t)context_ptr->hme_level0_max_search_area_in_height_array
                                        [search_region_number_in_height];
                            } else {
                                hmeLevel2_search_area_in_width =
                                    (int16_t)context_ptr->hme_level2_search_area_in_width_array
                                        [search_region_number_in_width];
                                hmeLevel2_search_area_in_height =
                                    (int16_t)context_ptr->hme_level2_search_area_in_height_array
                                        [search_region_number_in_height];
                                hmeLevel2_max_search_area_in_width =
                                    (int16_t)context_ptr->hme_level2_search_area_in_width_array
                                        [search_region_number_in_width];
                                hmeLevel2_max_search_area_in_height =
                                    (int16_t)context_ptr->hme_level2_search_area_in_height_array
                                        [search_region_number_in_height];
                            }
                            hme_level_2(
                                pcs_ptr,
                                context_ptr,
                                origin_x,
                                origin_y,
                                sb_width,
                                sb_height,
                                refPicPtr,
                                hmeLevel2_search_area_in_width,
                                hmeLevel2_search_area_in_height,
                                hmeLevel2_max_search_area_in_width,
                                hmeLevel2_max_search_area_in_height,
                                hme_sr_factor_x,
                                hme_sr_factor_y,
                                context_ptr
                                    ->x_hme_level1_search_center[list_index][ref_pic_index]
                                                                [search_region_number_in_width]
                                                                [search_region_number_in_height],
                                context_ptr
                                    ->y_hme_level1_search_center[list_index][ref_pic_index]
                                                                [search_region_number_in_width]
                                                                [search_region_number_in_height],
                                &(context_ptr->hme_level2_sad[list_index][ref_pic_index]
                                                             [search_region_number_in_width]
                                                             [search_region_number_in_height]),
                                &(context_ptr
                                      ->x_hme_level2_search_center[list_index][ref_pic_index]
                                                                  [search_region_number_in_width]
                                                                  [search_region_number_in_height]),
                                &(context_ptr->y_hme_level2_search_center
                                      [list_index][ref_pic_index][search_region_number_in_width]
                                      [search_region_number_in_height]));

                            search_region_number_in_width++;
                        }
                        search_region_number_in_width = 0;
                        search_region_number_in_height++;
                    }
            }
        }
    }
}

/*******************************************
 *   Set the final search centre
 *******************************************/


void set_final_seach_centre_sb(
    PictureParentControlSet   *pcs_ptr,
    MeContext                 *context_ptr
) {
    UNUSED(pcs_ptr);
    // Hierarchical ME Search Center
    int16_t xHmeSearchCenter = 0;
    int16_t yHmeSearchCenter = 0;

    // Final ME Search Center
    int16_t x_search_center = 0;
    int16_t y_search_center = 0;

    // Search Center SADs
    uint64_t hmeMvSad = 0;
    uint32_t num_of_list_to_search;
    uint32_t list_index;
    uint8_t ref_pic_index;
    // Configure HME level 0, level 1 and level 2 from static config parameters
    EbBool enable_hme_level0_flag =
        context_ptr->enable_hme_level0_flag;
    EbBool enable_hme_level1_flag =
        context_ptr->enable_hme_level1_flag;
    EbBool enable_hme_level2_flag =
        context_ptr->enable_hme_level2_flag;

    uint64_t best_cost = (uint64_t)~0;
    context_ptr->best_list_idx = 0;
    context_ptr->best_ref_idx = 0;
    num_of_list_to_search = context_ptr->num_of_list_to_search;

    // Uni-Prediction motion estimation loop
    // List Loop
    for (list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];
        // Ref Picture Loop
        for (ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index){
            if (context_ptr->temporal_layer_index > 0 || list_index == 0) {
                if (context_ptr->enable_hme_flag) {
                    // Hierarchical ME - Search Center
                    if (enable_hme_level0_flag && !enable_hme_level1_flag &&
                        !enable_hme_level2_flag) {
                        xHmeSearchCenter =
                            context_ptr
                                ->x_hme_level0_search_center[list_index][ref_pic_index][0][0];
                        yHmeSearchCenter =
                            context_ptr
                                ->y_hme_level0_search_center[list_index][ref_pic_index][0][0];
                        hmeMvSad = context_ptr->hme_level0_sad[list_index][ref_pic_index][0][0];

                        uint32_t search_region_number_in_width  = 1;
                        uint32_t search_region_number_in_height = 0;

                        while (search_region_number_in_height <
                               context_ptr->number_hme_search_region_in_height) {
                            while (search_region_number_in_width <
                                   context_ptr->number_hme_search_region_in_width) {
                                xHmeSearchCenter =
                                    (context_ptr->hme_level0_sad[list_index][ref_pic_index]
                                                                [search_region_number_in_width]
                                                                [search_region_number_in_height] <
                                     hmeMvSad)
                                    ? context_ptr->x_hme_level0_search_center
                                          [list_index][ref_pic_index][search_region_number_in_width]
                                          [search_region_number_in_height]
                                    : xHmeSearchCenter;
                                yHmeSearchCenter =
                                    (context_ptr->hme_level0_sad[list_index][ref_pic_index]
                                                                [search_region_number_in_width]
                                                                [search_region_number_in_height] <
                                     hmeMvSad)
                                    ? context_ptr->y_hme_level0_search_center
                                          [list_index][ref_pic_index][search_region_number_in_width]
                                          [search_region_number_in_height]
                                    : yHmeSearchCenter;
                                hmeMvSad =
                                    (context_ptr->hme_level0_sad[list_index][ref_pic_index]
                                                                [search_region_number_in_width]
                                                                [search_region_number_in_height] <
                                     hmeMvSad)
                                    ? context_ptr->hme_level0_sad[list_index][ref_pic_index]
                                                                 [search_region_number_in_width]
                                                                 [search_region_number_in_height]
                                    : hmeMvSad;
                                search_region_number_in_width++;
                            }
                            search_region_number_in_width = 0;
                            search_region_number_in_height++;
                        }
                    }

                    if (enable_hme_level1_flag && !enable_hme_level2_flag) {
                        xHmeSearchCenter = context_ptr->x_hme_level1_search_center[list_index][ref_pic_index][0][0];
                        yHmeSearchCenter = context_ptr->y_hme_level1_search_center[list_index][ref_pic_index][0][0];
                        hmeMvSad = context_ptr->hme_level1_sad[list_index][ref_pic_index][0][0];

                        uint32_t search_region_number_in_width  = 1;
                        uint32_t search_region_number_in_height = 0;

                        while (
                            search_region_number_in_height <
                            context_ptr->number_hme_search_region_in_height) {
                            while (search_region_number_in_width < context_ptr->number_hme_search_region_in_width) {
                                xHmeSearchCenter = (context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad) ?
                                    context_ptr->x_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]: xHmeSearchCenter;
                                yHmeSearchCenter = (context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad) ?
                                    context_ptr->y_hme_level1_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] : yHmeSearchCenter;
                                hmeMvSad = (context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad) ?
                                    context_ptr->hme_level1_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] : hmeMvSad;
                                search_region_number_in_width++;
                            }
                            search_region_number_in_width = 0;
                            search_region_number_in_height++;
                        }
                    }

                    if (enable_hme_level2_flag) {
                        xHmeSearchCenter = context_ptr->x_hme_level2_search_center[list_index][ref_pic_index][0][0];
                        yHmeSearchCenter = context_ptr->y_hme_level2_search_center[list_index][ref_pic_index][0][0];
                        hmeMvSad = context_ptr->hme_level2_sad[list_index][ref_pic_index][0][0];

                        uint32_t search_region_number_in_width  = 1;
                        uint32_t search_region_number_in_height = 0;

                        while (  search_region_number_in_height < context_ptr->number_hme_search_region_in_height) {
                            while (search_region_number_in_width < context_ptr->number_hme_search_region_in_width) {
                                xHmeSearchCenter =
                                    (context_ptr->hme_level2_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad)
                                    ? context_ptr->x_hme_level2_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]: xHmeSearchCenter;
                                yHmeSearchCenter =
                                    (context_ptr->hme_level2_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad)
                                    ? context_ptr->y_hme_level2_search_center[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]: yHmeSearchCenter;
                                hmeMvSad =
                                    (context_ptr->hme_level2_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height] < hmeMvSad)
                                    ? context_ptr->hme_level2_sad[list_index][ref_pic_index][search_region_number_in_width][search_region_number_in_height]: hmeMvSad;
                                search_region_number_in_width++;
                            }
                            search_region_number_in_width = 0;
                            search_region_number_in_height++;
                        }
                    }

                    x_search_center = xHmeSearchCenter;
                    y_search_center = yHmeSearchCenter;
                }
            }
            else {
                x_search_center = 0;
                y_search_center = 0;
            }

            //sc valid for all cases. 0,0 if hme not done.
            context_ptr->hme_results[list_index][ref_pic_index].hme_sc_x = x_search_center;
            context_ptr->hme_results[list_index][ref_pic_index].hme_sc_y = y_search_center;

            context_ptr->hme_results[list_index][ref_pic_index].hme_sad = hmeMvSad;//this is not valid in all cases. only when HME is done, and when HMELevel2 is done
            //also for base layer some references are redundant!!
            context_ptr->hme_results[list_index][ref_pic_index].do_ref = 1;
            if (hmeMvSad < best_cost) {
                best_cost = hmeMvSad;
                context_ptr->best_list_idx = list_index;
                context_ptr->best_ref_idx = ref_pic_index;
            }
        }
    }
}


/*******************************************
 *   performs hierarchical ME for every ref frame
 *******************************************/


void hme_sb(
    PictureParentControlSet   *pcs_ptr,
    uint32_t                   sb_origin_x,
    uint32_t                   sb_origin_y,
    MeContext                 *context_ptr,
    EbPictureBufferDesc       *input_ptr
){


    if (context_ptr->prehme_ctrl.enable ){

        // perform pre - hierarchical ME level 0
        prehme_sb(
            pcs_ptr,
            sb_origin_x,
            sb_origin_y,
            context_ptr,
            input_ptr);
    }



    // perform hierarchical ME level 0
    // Configure HME level 0, level 1 and level 2 from static config parameters
    const EbBool enable_hme_level0_flag = context_ptr->hme_decimation <= ONE_DECIMATION_HME
        ? 0
        : context_ptr->enable_hme_level0_flag;
    if (context_ptr->enable_hme_flag && enable_hme_level0_flag) {
    hme_level0_sb(
        pcs_ptr,
        sb_origin_x,
        sb_origin_y,
        context_ptr,
        input_ptr);
    }
    // prune hierarchical ME level 0
    // perform hierarchical ME level 1
    const EbBool enable_hme_level1_flag = context_ptr->hme_decimation == ONE_DECIMATION_HME
        ? context_ptr->enable_hme_level0_flag
        : context_ptr->hme_decimation == ZERO_DECIMATION_HME ? 0
        : context_ptr->enable_hme_level1_flag;
    if (context_ptr->enable_hme_flag && enable_hme_level1_flag) {
    hme_level1_sb(
        pcs_ptr,
        sb_origin_x,
        sb_origin_y,
        context_ptr,
        input_ptr);
    }
    // prune hierarchical ME level 1
    // perform hierarchical ME level 2
    const EbBool enable_hme_level2_flag = context_ptr->hme_decimation == ZERO_DECIMATION_HME
        ? context_ptr->enable_hme_level0_flag
        : context_ptr->enable_hme_level2_flag;
    if (context_ptr->enable_hme_flag && enable_hme_level2_flag) {
    hme_level2_sb(
        pcs_ptr,
        sb_origin_x,
        sb_origin_y,
        context_ptr,
        input_ptr);
    }
    // set final mv centre
    set_final_seach_centre_sb(
        pcs_ptr,
        context_ptr);


#if OPT_PREHME
    if (context_ptr->me_type == ME_MCTF) {
        if (ABS(context_ptr->hme_results[0][0].hme_sc_x) > ABS(context_ptr->hme_results[0][0].hme_sc_y))
            context_ptr->tf_tot_horz_blks++;
        else
            context_ptr->tf_tot_vert_blks++;
    }
#endif


}



void hme_prune_ref_and_adjust_sr(MeContext* context_ptr) {
    uint64_t best = (uint64_t)~0;
    for (int i = 0; i < MAX_NUM_OF_REF_PIC_LIST; ++i) {
        for (int j = 0; j < REF_LIST_MAX_DEPTH; ++j) {
            if (context_ptr->hme_results[i][j].hme_sad < best) {
                best = context_ptr->hme_results[i][j].hme_sad;
            }
        }
    }
    uint16_t prune_ref_th = context_ptr->me_hme_prune_ctrls.prune_ref_if_hme_sad_dev_bigger_than_th;
    uint16_t mv_length_th                         = context_ptr->me_sr_adjustment_ctrls.reduce_me_sr_based_on_mv_length_th;
    uint16_t stationary_hme_sad_abs_th            = context_ptr->me_sr_adjustment_ctrls.stationary_hme_sad_abs_th;
    uint16_t reduce_me_sr_based_on_hme_sad_abs_th = context_ptr->me_sr_adjustment_ctrls.reduce_me_sr_based_on_hme_sad_abs_th;
    for (uint32_t li = 0; li < MAX_NUM_OF_REF_PIC_LIST; li++) {
        for (uint32_t ri = 0; ri < REF_LIST_MAX_DEPTH; ri++){
            // Prune references based on HME sad
            if (context_ptr->me_hme_prune_ctrls.enable_me_hme_ref_pruning &&
                (!context_ptr->me_hme_prune_ctrls.protect_closest_refs || ri > 0) &&
                (prune_ref_th != (uint16_t)~0) &&
                ((context_ptr->hme_results[li][ri].hme_sad - best) * 100 > (prune_ref_th * best)))
            {
                context_ptr->hme_results[li][ri].do_ref = 0;
            }

            // Reduce the ME search region if the hme sad is low
            if (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment) {
                if (ABS(context_ptr->hme_results[li][ri].hme_sc_x) <= mv_length_th &&
                    ABS(context_ptr->hme_results[li][ri].hme_sc_y) <= mv_length_th &&
                    context_ptr->hme_results[li][ri].hme_sad < stationary_hme_sad_abs_th)
                {
                    context_ptr->reduce_me_sr_divisor[li][ri] = context_ptr->me_sr_adjustment_ctrls.stationary_me_sr_divisor;
                }
                else if (context_ptr->hme_results[li][ri].hme_sad < reduce_me_sr_based_on_hme_sad_abs_th) {
                    context_ptr->reduce_me_sr_divisor[li][ri] = context_ptr->me_sr_adjustment_ctrls.me_sr_divisor_for_low_hme_sad;
                }
            }
        }
    }
}

#if OPT_ME
const uint8_t z_to_raster[85] =
{
    0,
    1,2,
    3,4,

    5,6,9,10,
    7,8,11,12,
    13,14,17,18,
    15,16,19,20,

    21,22,29,30,
    23,24,31,32,
    37,38,45,46,
    39,40,47,48,

    25,26,33,34,
    27,28,35,36,
    41,42,49,50,
    43,44,51,52,

    53,54,61,62,
    55,56,63,64,
    69,70,77,78,
    71,72,79,80,

    57,58,65,66,
    59,60,67,68,
    73,74,81,82,
    75,76,83,84


};
#endif
#if OPT_ME
#if SS_CLN_ME_CAND_ARRAY
void construct_me_candidate_array_mrp_off(
    PictureParentControlSet *pcs_ptr, MeContext *context_ptr,
    uint32_t num_of_list_to_search,
    uint32_t sb_index)
{
    // This function should only be called if there is one ref frame in each list
    assert(context_ptr->num_of_ref_pic_to_search[0] == 1);
    assert(context_ptr->num_of_ref_pic_to_search[1] == 1);
    const uint8_t ref_pic_idx = 0;

    // Set whether the reference from each list is allowed
    uint8_t blk_do_ref_org[MAX_NUM_OF_REF_PIC_LIST];
    blk_do_ref_org[REF_LIST_0] = context_ptr->hme_results[REF_LIST_0][0].do_ref;
    blk_do_ref_org[REF_LIST_1] = (num_of_list_to_search == 0) ? 0 : context_ptr->hme_results[REF_LIST_1][0].do_ref;
    num_of_list_to_search &= context_ptr->hme_results[REF_LIST_1][0].do_ref;

    const uint32_t me_prune_th = (blk_do_ref_org[0] && blk_do_ref_org[1]) ? context_ptr->prune_me_candidates_th : 0;

    // Set the count to 1 for all PUs using memset, which is faster than setting at the end of each loop.  The count will only need
    // to be updated if both reference frames are allowed.
#if ME_8X8
#if FTR_M13
    uint8_t number_of_pus = pcs_ptr->enable_me_16x16 ? pcs_ptr->enable_me_8x8 ? pcs_ptr->max_number_of_pus_per_sb : MAX_SB64_PU_COUNT_NO_8X8 : MAX_SB64_PU_COUNT_WO_16X16;
#else
    uint8_t number_of_pus = pcs_ptr->enable_me_8x8 ? pcs_ptr->max_number_of_pus_per_sb : MAX_SB64_PU_COUNT_NO_8X8;
#endif
    memset(pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index, 1, number_of_pus);
#else
    memset(pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index, 1, pcs_ptr->max_number_of_pus_per_sb);
#endif

    for (uint8_t n_idx = 0; n_idx < pcs_ptr->max_number_of_pus_per_sb; ++n_idx) {

        const uint8_t pu_index = z_to_raster[n_idx];
        uint8_t me_cand_offset = 0;

#if ME_8X8
#if FTR_M13
        uint8_t use_me_pu = pcs_ptr->enable_me_16x16 ? pcs_ptr->enable_me_8x8 || n_idx < MAX_SB64_PU_COUNT_NO_8X8 : n_idx < MAX_SB64_PU_COUNT_WO_16X16;
        MeCandidate* me_candidate_array = NULL;
        if (use_me_pu)
            me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#else
        uint8_t use_me_8x8 = pcs_ptr->enable_me_8x8 || n_idx < MAX_SB64_PU_COUNT_NO_8X8;
        MeCandidate* me_candidate_array = NULL;
        if (use_me_8x8)
            me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#endif
#else
        MeCandidate* me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#endif
        uint8_t blk_do_ref[MAX_NUM_OF_REF_PIC_LIST] = { blk_do_ref_org[REF_LIST_0] , blk_do_ref_org[REF_LIST_1] };
        uint32_t best_me_dist = (me_prune_th > 0) ? MIN(context_ptr->p_sb_best_sad[REF_LIST_0][ref_pic_idx][n_idx], context_ptr->p_sb_best_sad[REF_LIST_1][ref_pic_idx][n_idx]) : (uint32_t)~0;
#if FTR_LIMIT_ME_CANDS
        int8_t min_dist_list = -1;
        // If both refs have a candidate, use only the best one for unipred
        if (context_ptr->use_best_unipred_cand_only && blk_do_ref[REF_LIST_0] && blk_do_ref[REF_LIST_1])
            min_dist_list = context_ptr->p_sb_best_sad[REF_LIST_0][ref_pic_idx][n_idx] < context_ptr->p_sb_best_sad[REF_LIST_1][ref_pic_idx][n_idx] ? 0 : 1;
#endif
        // Unipred candidates
#if FTR_LIMIT_ME_CANDS
#if ME_8X8
#if FTR_M13
        for (int list_index = REF_LIST_0; (uint32_t)list_index <= num_of_list_to_search && (use_me_pu || me_cand_offset == 0); ++list_index) {
#else
        for (int list_index = REF_LIST_0; (uint32_t)list_index <= num_of_list_to_search && (use_me_8x8 || me_cand_offset == 0); ++list_index) {
#endif
#else
        for (int list_index = REF_LIST_0; (uint32_t)list_index <= num_of_list_to_search; ++list_index) {
#endif
#else
        for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
#endif

                //ME was skipped, so do not add this Unipred candidate
                if (blk_do_ref[list_index] == 0)
                    continue;

                if (me_prune_th > 0) {
                    uint32_t current_to_best_dist_distance = (context_ptr->p_sb_best_sad[list_index][ref_pic_idx][n_idx] - best_me_dist) * 100;
                    if (current_to_best_dist_distance > (best_me_dist * me_prune_th))
                    {
                        blk_do_ref[list_index] = 0;
                        continue;
                    }

                }
#if FTR_LIMIT_ME_CANDS
                if (min_dist_list != -1 && min_dist_list != list_index) {
                    // Need to save the MV in case bipred is injected
#if ME_8X8
#if FTR_M13
                    if (use_me_pu)
#else
                    if (use_me_8x8)
#endif
                        pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_idx].as_int =
                            context_ptr->p_sb_best_mv[list_index][ref_pic_idx][n_idx];
#else
                    pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_idx].as_int =
                        context_ptr->p_sb_best_mv[list_index][ref_pic_idx][n_idx];
#endif
                    continue;
                }
#endif
                if (me_cand_offset == 0)
                    context_ptr->me_distortion[pu_index] = context_ptr->p_sb_best_sad[list_index][ref_pic_idx][n_idx];

#if ME_8X8
#if FTR_M13
                if (use_me_pu) {
#else
                if (use_me_8x8) {
#endif
                    me_candidate_array[me_cand_offset].direction = list_index;
                    me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_idx;
                    me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_idx;
                    me_candidate_array[me_cand_offset].ref0_list = list_index == 0 ? list_index : 24;
                    me_candidate_array[me_cand_offset].ref1_list = list_index == 1 ? list_index : 24;

                    pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_idx].as_int =
                        context_ptr->p_sb_best_mv[list_index][ref_pic_idx][n_idx];
                }
#else
                me_candidate_array[me_cand_offset].direction = list_index;
                me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_idx;
                me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_idx;
                me_candidate_array[me_cand_offset].ref0_list = list_index == 0 ? list_index : 24;
                me_candidate_array[me_cand_offset].ref1_list = list_index == 1 ? list_index : 24;

                pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_idx].as_int =
                    context_ptr->p_sb_best_mv[list_index][ref_pic_idx][n_idx];
#endif

                me_cand_offset++;
        }

        // Can have up to one bipred cand (LAST ,BWD)
#if ME_8X8
#if FTR_M13
        if (blk_do_ref[REF_LIST_0] && blk_do_ref[REF_LIST_1] && use_me_pu) {
#else
        if (blk_do_ref[REF_LIST_0] && blk_do_ref[REF_LIST_1] && use_me_8x8) {
#endif
#else
        if (blk_do_ref[REF_LIST_0] && blk_do_ref[REF_LIST_1]) {
#endif
            // If get here, will have 3 candidates, since both unipred directions are valid
            assert(num_of_list_to_search);
#if FTR_LIMIT_ME_CANDS
            me_candidate_array[me_cand_offset].direction = BI_PRED;
            me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_idx;
            me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_idx;
            me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_0;
            me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_1;

            // store total me candidate count
            pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index[pu_index] = me_cand_offset + 1;
#else
            me_candidate_array[2].direction = BI_PRED;
            me_candidate_array[2].ref_idx_l0 = ref_pic_idx;
            me_candidate_array[2].ref_idx_l1 = ref_pic_idx;
            me_candidate_array[2].ref0_list = REFERENCE_PIC_LIST_0;
            me_candidate_array[2].ref1_list = REFERENCE_PIC_LIST_1;

            // store total me candidate count
            pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index[pu_index] = 3;
#endif
        }
    }
}
#endif
void construct_me_candidate_array(
    PictureParentControlSet *pcs_ptr, MeContext *context_ptr,
    uint32_t num_of_list_to_search,
    uint32_t sb_index)
{


    for (uint32_t n_idx = 0; n_idx < pcs_ptr->max_number_of_pus_per_sb; ++n_idx)
    {
#if SS_CLN_ME_CAND_ARRAY
        uint8_t pu_index = (n_idx > 4) ? z_to_raster[n_idx] : n_idx;
        uint8_t me_cand_offset = 0;
#else
        uint32_t pu_index;
        if (n_idx > 4)
            pu_index = z_to_raster[n_idx];
        else
            pu_index = n_idx;

        uint32_t me_cand_offset = 0;
#endif

    #if OPT_ME
#if ME_8X8
#if FTR_M13
        uint8_t use_me_pu = pcs_ptr->enable_me_16x16 ? pcs_ptr->enable_me_8x8 || n_idx < MAX_SB64_PU_COUNT_NO_8X8 : n_idx < MAX_SB64_PU_COUNT_WO_16X16;
        MeCandidate* me_candidate_array = NULL;
        if (use_me_pu)
            me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#else
        uint8_t use_me_8x8 = pcs_ptr->enable_me_8x8 || n_idx < MAX_SB64_PU_COUNT_NO_8X8;
        MeCandidate* me_candidate_array = NULL;
        if (use_me_8x8)
            me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#endif
#else
        MeCandidate* me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * pcs_ptr->pa_me_data->max_cand];
#endif
    #else
        MeCandidate* me_candidate_array = &pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[pu_index * MAX_PA_ME_CAND];
    #endif
        uint8_t blk_do_ref[MAX_NUM_OF_REF_PIC_LIST][MAX_REF_IDX];
        uint32_t current_to_best_dist_distance;
#if SS_CLN_ME_CAND_ARRAY
        const uint32_t me_prune_th = context_ptr->prune_me_candidates_th; //to change to 32bit
#else
        const uint32_t me_prune_th = (uint32_t)context_ptr->prune_me_candidates_th; //to change to 32bit
#endif
        uint32_t best_me_dist = ~0;


        //add a fast path for 2 references at the end

        // Determine the best me distortion
        if (me_prune_th > 0) {
            for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
                for (uint32_t ref_pic = 0; ref_pic < context_ptr->num_of_ref_pic_to_search[list_index]; ++ref_pic) {

                    if (context_ptr->hme_results[list_index][ref_pic].do_ref == 0) // TODO: make this a local variable
                        continue;

                    best_me_dist = context_ptr->p_sb_best_sad[list_index][ref_pic][n_idx] < best_me_dist ?
                        context_ptr->p_sb_best_sad[list_index][ref_pic][n_idx] : best_me_dist;
                }
            }
        }

        // Unipred candidates
#if ME_8X8
#if FTR_M13
        for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search && (use_me_pu || me_cand_offset == 0); ++list_index) {
            const uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];

            for (uint32_t ref_pic_index = 0; (ref_pic_index < num_of_ref_pic_to_search) && (use_me_pu || (me_cand_offset == 0)); ++ref_pic_index) {
#else
        for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search && (use_me_8x8 || me_cand_offset == 0); ++list_index) {
            const uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];

            for (uint32_t ref_pic_index = 0; (ref_pic_index < num_of_ref_pic_to_search) && (use_me_8x8 || (me_cand_offset == 0)); ++ref_pic_index) {
#endif
#else
        for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
            const uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];

            for (uint32_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
#endif


                blk_do_ref[list_index][ref_pic_index] = context_ptr->hme_results[list_index][ref_pic_index].do_ref;


                //ME was skipped, so do not add this Unipred candidate
                if (context_ptr->hme_results[list_index][ref_pic_index].do_ref == 0)
                    continue;

                if (me_prune_th > 0) {
                    current_to_best_dist_distance = (context_ptr->p_sb_best_sad[list_index][ref_pic_index][n_idx] - best_me_dist) * 100;
                    if (current_to_best_dist_distance > (best_me_dist * me_prune_th))
                    {
                        blk_do_ref[list_index][ref_pic_index] = 0;
                        continue;
                    }

                }


                if (me_cand_offset == 0)
                    context_ptr->me_distortion[pu_index] = context_ptr->p_sb_best_sad[list_index][ref_pic_index][n_idx];
#if ME_8X8
#if FTR_M13
                if (use_me_pu) {
#else
                if (use_me_8x8) {
#endif
                    me_candidate_array[me_cand_offset].direction = list_index;
                    me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_index;
                    me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_index;
                    me_candidate_array[me_cand_offset].ref0_list = list_index == 0 ? list_index : 24;
                    me_candidate_array[me_cand_offset].ref1_list = list_index == 1 ? list_index : 24;

                    pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_index].as_int =
                        context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx];
                }
#else
                me_candidate_array[me_cand_offset].direction = list_index;
                me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_index;
                me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_index;
                me_candidate_array[me_cand_offset].ref0_list = list_index == 0 ? list_index : 24;
                me_candidate_array[me_cand_offset].ref1_list = list_index == 1 ? list_index : 24;

    #if OPT_ME
                pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * pcs_ptr->pa_me_data->max_refs + (list_index ? pcs_ptr->pa_me_data->max_l0 : 0) + ref_pic_index].as_int =
                    context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx];
    #else
                pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * MAX_PA_ME_MV + (list_index ? 4 : 0) + ref_pic_index].as_int =
                    context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx];
    #endif
#endif
                me_cand_offset++;
            }
        }
#if ME_8X8
#if FTR_M13
        if (num_of_list_to_search && use_me_pu) {
#else
        if (num_of_list_to_search && use_me_8x8) {
#endif
#else
        if (num_of_list_to_search) {
#endif
            // 1st set of BIPRED cand
            // (LAST ,BWD), (LAST,ALT ), (LAST,ALT2 )
            // (LAST2,BWD), (LAST2,ALT), (LAST2,ALT2)
            // (LAST3,BWD), (LAST3,ALT), (LAST3,ALT2)
            // (GOLD ,BWD), (GOLD,ALT ), (GOLD,ALT2 )
            for (uint32_t first_list_ref_pict_idx = 0;
                first_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_0];
                first_list_ref_pict_idx++) {
                for (uint32_t second_list_ref_pict_idx = 0;
                    second_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_1];
                    second_list_ref_pict_idx++) {

                    if (blk_do_ref[REF_LIST_0][first_list_ref_pict_idx] &&
                        blk_do_ref[REF_LIST_1][second_list_ref_pict_idx]) {

                        me_candidate_array[me_cand_offset].direction = BI_PRED;
                        me_candidate_array[me_cand_offset].ref_idx_l0 = first_list_ref_pict_idx;
                        me_candidate_array[me_cand_offset].ref_idx_l1 = second_list_ref_pict_idx;
                        me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_0;
                        me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_1;
                        me_cand_offset++;
                    }
                    }
                }

            // 2nd set of BIPRED cand: (LAST,LAST2) (LAST,LAST3) (LAST,GOLD)
            for (uint32_t first_list_ref_pict_idx = 1;
                first_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_0];
                first_list_ref_pict_idx++) {

                if (blk_do_ref[REF_LIST_0][0] &&
                    blk_do_ref[REF_LIST_0][first_list_ref_pict_idx]) {

                    me_candidate_array[me_cand_offset].direction = BI_PRED;
                    me_candidate_array[me_cand_offset].ref_idx_l0 = 0;
                    me_candidate_array[me_cand_offset].ref_idx_l1 = first_list_ref_pict_idx;
                    me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_0;
                    me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_0;
                    me_cand_offset++;
                }
                }

            // 3rd set of BIPRED cand: (BWD, ALT)

            if (context_ptr->num_of_ref_pic_to_search[REF_LIST_1] == 3 && blk_do_ref[REF_LIST_1][0] &&
                blk_do_ref[REF_LIST_1][2]) {
                {
                    me_candidate_array[me_cand_offset].direction = BI_PRED;
                    me_candidate_array[me_cand_offset].ref_idx_l0 = 0;
                    me_candidate_array[me_cand_offset].ref_idx_l1 = 2;
                    me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_1;
                    me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_1;
                    me_cand_offset++;
                }
            }
        }


        // store total me candidate count
#if ME_8X8
#if FTR_M13
        if (use_me_pu)
#else
        if (use_me_8x8)
#endif
            pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index[pu_index] = me_cand_offset;
#else
        pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index[pu_index] = me_cand_offset;
#endif
    }

}
#else
void construct_me_candidate_array(
    PictureParentControlSet *pcs_ptr, MeContext *context_ptr,
    uint32_t num_of_list_to_search,
    uint32_t pu_index, uint32_t sb_index) {

    uint32_t n_idx;
    if (pu_index > 20)
        n_idx = tab8x8[pu_index - 21] + 21;
    else if (pu_index > 4)
        n_idx = tab16x16[pu_index - 5] + 5;
    else
        n_idx = pu_index;

    uint32_t me_cand_offset = pu_index * MAX_PA_ME_CAND;
    MeCandidate* me_candidate_array = pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array;
    int64_t current_to_best_dist_distance = 0;
    int64_t me_prune_th = context_ptr->prune_me_candidates_th;
    int64_t best_me_dist = MAX_SAD_VALUE;

    // Determine the best me distortion
    if (me_prune_th > 0) {
        for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
            for (uint32_t ref_pic = 0; ref_pic < context_ptr->num_of_ref_pic_to_search[list_index]; ++ref_pic) {

                if (context_ptr->hme_results[list_index][ref_pic].do_ref == 0) // TODO: make this a local variable
                    continue;

                best_me_dist = context_ptr->p_sb_best_sad[list_index][ref_pic][n_idx] < best_me_dist ?
                    context_ptr->p_sb_best_sad[list_index][ref_pic][n_idx] : best_me_dist;
            }
        }
    }

    // Unipred candidates
    for (uint32_t list_index = REF_LIST_0; list_index <= num_of_list_to_search; ++list_index) {
        const uint8_t num_of_ref_pic_to_search = context_ptr->num_of_ref_pic_to_search[list_index];

        for (uint32_t ref_pic_index = 0; ref_pic_index < num_of_ref_pic_to_search; ++ref_pic_index) {
            //ME was skipped, so do not add this Unipred candidate
            if (context_ptr->hme_results[list_index][ref_pic_index].do_ref == 0)
                continue;

            if (me_prune_th > 0) {
                current_to_best_dist_distance = (context_ptr->p_sb_best_sad[list_index][ref_pic_index][n_idx] - best_me_dist) * 100;
                if (current_to_best_dist_distance > (best_me_dist * me_prune_th))
                    continue;
            }

            if (me_cand_offset == pu_index * MAX_PA_ME_CAND)
                context_ptr->me_distortion[pu_index] = context_ptr->p_sb_best_sad[list_index][ref_pic_index][n_idx];

            me_candidate_array[me_cand_offset].direction = list_index;
            me_candidate_array[me_cand_offset].ref_idx_l0 = ref_pic_index;
            me_candidate_array[me_cand_offset].ref_idx_l1 = ref_pic_index;
            me_candidate_array[me_cand_offset].ref0_list = list_index == 0 ? list_index : 24;
            me_candidate_array[me_cand_offset].ref1_list = list_index == 1 ? list_index : 24;

            pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * MAX_PA_ME_MV + (list_index ? 4 : 0) + ref_pic_index].x_mv =
                _MVXT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);

            pcs_ptr->pa_me_data->me_results[sb_index]->me_mv_array[pu_index * MAX_PA_ME_MV + (list_index ? 4 : 0) + ref_pic_index].y_mv =
                _MVYT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);
            //check if final MV is within AV1 limits
            check_mv_validity(
                _MVXT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]),
                _MVYT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]),
                1);

            me_cand_offset++;
        }
    }

    if (num_of_list_to_search) {
        // 1st set of BIPRED cand
        // (LAST ,BWD), (LAST,ALT ), (LAST,ALT2 )
        // (LAST2,BWD), (LAST2,ALT), (LAST2,ALT2)
        // (LAST3,BWD), (LAST3,ALT), (LAST3,ALT2)
        // (GOLD ,BWD), (GOLD,ALT ), (GOLD,ALT2 )
        for (uint32_t first_list_ref_pict_idx = 0;
            first_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_0];
            first_list_ref_pict_idx++) {
            for (uint32_t second_list_ref_pict_idx = 0;
                second_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_1];
                second_list_ref_pict_idx++) {
                if (context_ptr->hme_results[REF_LIST_0][first_list_ref_pict_idx].do_ref &&
                    context_ptr->hme_results[REF_LIST_1][second_list_ref_pict_idx].do_ref) {

                    if (me_prune_th > 0) {
                        current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_0][first_list_ref_pict_idx][n_idx] - best_me_dist) * 100;
                        if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                            continue;
                        current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_1][second_list_ref_pict_idx][n_idx] - best_me_dist) * 100;
                        if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                            continue;
                    }

                    me_candidate_array[me_cand_offset].direction = BI_PRED;
                    me_candidate_array[me_cand_offset].ref_idx_l0 = first_list_ref_pict_idx;
                    me_candidate_array[me_cand_offset].ref_idx_l1 = second_list_ref_pict_idx;
                    me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_0;
                    me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_1;
                    me_cand_offset++;
                }
            }
        }

        // 2nd set of BIPRED cand: (LAST,LAST2) (LAST,LAST3) (LAST,GOLD)
        for (uint32_t first_list_ref_pict_idx = 1;
            first_list_ref_pict_idx < context_ptr->num_of_ref_pic_to_search[REF_LIST_0];
            first_list_ref_pict_idx++) {
            if (context_ptr->hme_results[REF_LIST_0][0].do_ref &&
                context_ptr->hme_results[REF_LIST_0][first_list_ref_pict_idx].do_ref) {

                if (me_prune_th > 0) {
                    current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_0][0][n_idx] - best_me_dist) * 100;
                    if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                        continue;
                    current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_0][first_list_ref_pict_idx][n_idx] - best_me_dist) * 100;
                    if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                        continue;
                }

                me_candidate_array[me_cand_offset].direction = BI_PRED;
                me_candidate_array[me_cand_offset].ref_idx_l0 = 0;
                me_candidate_array[me_cand_offset].ref_idx_l1 = first_list_ref_pict_idx;
                me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_0;
                me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_0;
                me_cand_offset++;
            }
        }

        // 3rd set of BIPRED cand: (BWD, ALT)
        if (context_ptr->num_of_ref_pic_to_search[REF_LIST_1] == 3 && context_ptr->hme_results[REF_LIST_1][0].do_ref &&
            context_ptr->hme_results[REF_LIST_1][2].do_ref) {

            uint8_t inject_cand = 1;
            if (me_prune_th > 0) {
                current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_1][0][n_idx] - best_me_dist) * 100;
                if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                    inject_cand = 0;
                current_to_best_dist_distance = (context_ptr->p_sb_best_sad[REF_LIST_1][2][n_idx] - best_me_dist) * 100;
                if (current_to_best_dist_distance > best_me_dist * me_prune_th)
                    inject_cand = 0;
            }
            if (inject_cand) {
                me_candidate_array[me_cand_offset].direction = BI_PRED;
                me_candidate_array[me_cand_offset].ref_idx_l0 = 0;
                me_candidate_array[me_cand_offset].ref_idx_l1 = 2;
                me_candidate_array[me_cand_offset].ref0_list = REFERENCE_PIC_LIST_1;
                me_candidate_array[me_cand_offset].ref1_list = REFERENCE_PIC_LIST_1;
                me_cand_offset++;
            }
        }
    }

    // Update total me candidate count
    pcs_ptr->pa_me_data->me_results[sb_index]->total_me_candidate_index[pu_index] =
        MIN((me_cand_offset - pu_index * MAX_PA_ME_CAND), MAX_PA_ME_CAND);
}
#endif


// Active and stationary detection for global motion
void perform_gm_detection(
    PictureParentControlSet *pcs_ptr, // input parameter, Picture Control Set Ptr
    uint32_t                 sb_index, // input parameter, SB Index
    MeContext* context_ptr // input parameter, ME Context Ptr, used to store decimated/interpolated SB/SR
) {

    SequenceControlSet *scs_ptr = pcs_ptr->scs_ptr;
    uint64_t stationary_cnt = 0;
    uint64_t per_sig_cnt[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH][NUM_MV_COMPONENTS][NUM_MV_HIST];
    uint64_t tot_cnt = 0;
    memset(per_sig_cnt, 0, sizeof(uint64_t) * MAX_MV_HIST_SIZE);

    if (scs_ptr->input_resolution <= INPUT_SIZE_480p_RANGE) {
        for (unsigned i = 0; i < 64; i++) {
            uint8_t n_idx = 21 + i;
#if ME_8X8
#if FTR_M13
            if (!pcs_ptr->enable_me_8x8) {
                if (n_idx >= MAX_SB64_PU_COUNT_NO_8X8)
                    n_idx = me_idx_85_8x8_to_16x16_conversion[n_idx - MAX_SB64_PU_COUNT_NO_8X8];
                if (!pcs_ptr->enable_me_16x16)
                    if (n_idx >= MAX_SB64_PU_COUNT_WO_16X16)
                        n_idx = me_idx_16x16_to_parent_32x32_conversion[n_idx - MAX_SB64_PU_COUNT_WO_16X16];
            }
#else
            if (!pcs_ptr->enable_me_8x8)
                if (n_idx >= MAX_SB64_PU_COUNT_NO_8X8)
                    n_idx = me_idx_85_8x8_to_16x16_conversion[n_idx - MAX_SB64_PU_COUNT_NO_8X8];
#endif
#endif
#if OPT_ME
            MeCandidate* me_candidate = &(pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[n_idx * pcs_ptr->pa_me_data->max_cand]);
#else
            MeCandidate* me_candidate = &(pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[n_idx * MAX_PA_ME_CAND]);
#endif

            uint32_t list_index = (me_candidate->direction == 0 || me_candidate->direction == 2) ? me_candidate->ref0_list : me_candidate->ref1_list;
            uint32_t ref_pic_index = (me_candidate->direction == 0 || me_candidate->direction == 2) ? me_candidate->ref_idx_l0 : me_candidate->ref_idx_l1;

            // Active block detection
            uint16_t dist = ABS((int16_t)(pcs_ptr->picture_number - context_ptr->me_ds_ref_array[list_index][ref_pic_index].picture_number));
            int active_th = (pcs_ptr->gm_ctrls.use_distance_based_active_th) ? MAX(dist >> 1, 4) : 4;

            int mx = _MVXT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);
            if (mx < -active_th)
                per_sig_cnt[list_index][ref_pic_index][0][0]++;
            else if (mx > active_th)
                per_sig_cnt[list_index][ref_pic_index][0][1]++;
            int my = _MVYT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);
            if (my < -active_th)
                per_sig_cnt[list_index][ref_pic_index][1][0]++;
            else if (my > active_th)
                per_sig_cnt[list_index][ref_pic_index][1][1]++;

            // Stationary block detection
            int stationary_th = 0;
            if (abs(mx) <= stationary_th && abs(my) <= stationary_th)
                stationary_cnt++;

            tot_cnt++;
        }
    }
    else {
        for (unsigned i = 0; i < 16; i++) {
            uint8_t n_idx = 5 + i;
#if FTR_M13
            if (!pcs_ptr->enable_me_16x16)
                if (n_idx >= MAX_SB64_PU_COUNT_WO_16X16)
                    n_idx = me_idx_16x16_to_parent_32x32_conversion[n_idx - MAX_SB64_PU_COUNT_WO_16X16];
#endif
#if OPT_ME
            MeCandidate* me_candidate = &(pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[n_idx * pcs_ptr->pa_me_data->max_cand]);
#else
            MeCandidate* me_candidate = &(pcs_ptr->pa_me_data->me_results[sb_index]->me_candidate_array[n_idx * MAX_PA_ME_CAND]);
#endif

            uint32_t list_index = (me_candidate->direction == 0 || me_candidate->direction == 2) ? me_candidate->ref0_list : me_candidate->ref1_list;
            uint32_t ref_pic_index = (me_candidate->direction == 0 || me_candidate->direction == 2) ? me_candidate->ref_idx_l0 : me_candidate->ref_idx_l1;

            // Active block detection
            uint16_t dist = ABS((int16_t)(pcs_ptr->picture_number - context_ptr->me_ds_ref_array[list_index][ref_pic_index].picture_number));
            int active_th = (pcs_ptr->gm_ctrls.use_distance_based_active_th) ? MAX(dist * 16, 32) : 32;

            int mx = _MVXT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);
            if (mx < -active_th)
                per_sig_cnt[list_index][ref_pic_index][0][0]++;
            else if (mx > active_th)
                per_sig_cnt[list_index][ref_pic_index][0][1]++;
            int my = _MVYT(context_ptr->p_sb_best_mv[list_index][ref_pic_index][n_idx]);
            if (my < -active_th)
                per_sig_cnt[list_index][ref_pic_index][1][0]++;
            else if (my > active_th)
                per_sig_cnt[list_index][ref_pic_index][1][1]++;

            // Stationary block detection
            int stationary_th = 4;
            if (abs(mx) <= stationary_th && abs(my) <= stationary_th)
                stationary_cnt++;

            tot_cnt++;
        }
    }

    // Set stationary_block_present_sb to 1 if stationary_cnt is higher than 5%
    if (stationary_cnt > ((tot_cnt * 5) / 100))
        pcs_ptr->stationary_block_present_sb[sb_index] = 1;

    for (int l = 0; l < MAX_NUM_OF_REF_PIC_LIST; l++) {
        for (int r = 0; r < REF_LIST_MAX_DEPTH; r++) {
            for (int c = 0; c < NUM_MV_COMPONENTS; c++) {
                for (int s = 0; s < NUM_MV_HIST; s++) {
                    if (per_sig_cnt[l][r][c][s] > (tot_cnt / 2)) {
                        pcs_ptr->rc_me_allow_gm[sb_index] = 1;
                        break;
                    }
                }
            }
        }
    }

}




// Compute the distortion per block size based on the ME results
void compute_distortion(
    PictureParentControlSet *pcs_ptr, // input parameter, Picture Control Set Ptr
    uint32_t                 sb_index, // input parameter, SB Index
    MeContext* context_ptr // input parameter, ME Context Ptr, used to store decimated/interpolated SB/SR
) {
    SequenceControlSet *scs_ptr = pcs_ptr->scs_ptr;
    // Determine sb_64x64_me_class
    SbParams *sb_params = &pcs_ptr->sb_params_array[sb_index];
    uint32_t sb_size = 64 * 64;
    uint32_t dist_64x64 = 0, dist_32x32 = 0, dist_16x16 = 0, dist_8x8 = 0;

    // 64x64
    {
        dist_64x64 = context_ptr->me_distortion[0];
    }

    // 32x32
    for (unsigned i = 0; i < 4; i++) {
        dist_32x32 += context_ptr->me_distortion[1 + i];
    }

    // 16x16
    for (unsigned i = 0; i < 16; i++) {
        dist_16x16 += context_ptr->me_distortion[5 + i];
    }

    // 8x8
    for (unsigned i = 0; i < 64; i++) {
        dist_8x8 += context_ptr->me_distortion[21 + i];
    }

    uint64_t mean_dist_8x8 = dist_8x8 / 64;
    uint64_t sum_ofsq_dist_8x8 = 0;
    for (unsigned i = 0; i < 64; i++) {
#if FTR_BIAS_STAT
        const  int64_t diff = (context_ptr->me_distortion[21 + i] - mean_dist_8x8);
        sum_ofsq_dist_8x8 += diff * diff;
#else
        sum_ofsq_dist_8x8 += (context_ptr->me_distortion[21 + i] - mean_dist_8x8) * (context_ptr->me_distortion[21 + i] - mean_dist_8x8);
#endif
    }

    pcs_ptr->me_8x8_cost_variance[sb_index] = (uint32_t)(sum_ofsq_dist_8x8 / 64);
#if FTR_BIAS_STAT
    // Compute the sum of the distortion of all 16 16x16 (720 and above) and
    // 64 8x8 (for lower resolutions) blocks in the SB
    pcs_ptr->rc_me_distortion[sb_index] = (scs_ptr->input_resolution <= INPUT_SIZE_480p_RANGE) ? dist_8x8 : dist_16x16;
    const uint32_t pix_num = sb_params->width * sb_params->height;
    // Normalize
    pcs_ptr->me_64x64_distortion[sb_index] = (((dist_64x64 * sb_size) / (pix_num)) * context_ptr->stat_factor) / 100;
    pcs_ptr->me_32x32_distortion[sb_index] = (((dist_32x32 * sb_size) / (pix_num)) * context_ptr->stat_factor) / 100;
    pcs_ptr->me_16x16_distortion[sb_index] = (((dist_16x16 * sb_size) / (pix_num)) * context_ptr->stat_factor) / 100;
    pcs_ptr->me_8x8_distortion[sb_index] = (((dist_8x8 * sb_size) / (pix_num)) * context_ptr->stat_factor) / 100;
#else
    // Compute the sum of the distortion of all 16 16x16 (720 and above) and
    // 64 8x8 (for lower resolutions) blocks in the SB
    pcs_ptr->rc_me_distortion[sb_index] = (scs_ptr->input_resolution <= INPUT_SIZE_480p_RANGE) ? dist_8x8 : dist_16x16;

    // Normalize
    pcs_ptr->me_64x64_distortion[sb_index] = (dist_64x64 * sb_size) / (sb_params->width * sb_params->height);
    pcs_ptr->me_32x32_distortion[sb_index] = (dist_32x32 * sb_size) / (sb_params->width * sb_params->height);
    pcs_ptr->me_16x16_distortion[sb_index] = (dist_16x16 * sb_size) / (sb_params->width * sb_params->height);
    pcs_ptr->me_8x8_distortion[sb_index] = (dist_8x8 * sb_size) / (sb_params->width * sb_params->height);
#endif
}



// Initalize data used in ME/HME
static INLINE void init_me_hme_data(MeContext* context_ptr) {

    // Initialize HME search centres to 0
    if (context_ptr->enable_hme_flag) {
        memset(context_ptr->x_hme_level0_search_center, 0,
            sizeof(context_ptr->x_hme_level0_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);
        memset(context_ptr->y_hme_level0_search_center, 0,
            sizeof(context_ptr->y_hme_level0_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);

        memset(context_ptr->x_hme_level1_search_center, 0,
            sizeof(context_ptr->x_hme_level1_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);
        memset(context_ptr->y_hme_level1_search_center, 0,
            sizeof(context_ptr->y_hme_level1_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);

        memset(context_ptr->x_hme_level2_search_center, 0,
            sizeof(context_ptr->x_hme_level2_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);
        memset(context_ptr->y_hme_level2_search_center, 0,
            sizeof(context_ptr->y_hme_level2_search_center[0][0][0][0]) * MAX_NUM_OF_REF_PIC_LIST
            * MAX_REF_IDX * EB_HME_SEARCH_AREA_COLUMN_MAX_COUNT * EB_HME_SEARCH_AREA_ROW_MAX_COUNT);
    }

    // R2R FIX: no winner integer MV is set in special case like initial p_sb_best_mv for overlay case,
    // then it sends dirty p_sb_best_mv to MD, initializing it is necessary
    memset(context_ptr->p_sb_best_mv, 0, sizeof(context_ptr->p_sb_best_mv[0][0][0]) * MAX_NUM_OF_REF_PIC_LIST * REF_LIST_MAX_DEPTH * SQUARE_PU_COUNT);

    //init hme results buffer
    for (uint32_t li = 0; li < MAX_NUM_OF_REF_PIC_LIST; li++) {
        for (uint32_t ri = 0; ri < REF_LIST_MAX_DEPTH; ri++) {
            if (context_ptr->me_type != ME_MCTF)
                context_ptr->hme_results[li][ri].list_i = li;
            context_ptr->hme_results[li][ri].ref_i = ri;
            context_ptr->hme_results[li][ri].do_ref = 1;
            context_ptr->hme_results[li][ri].hme_sad = 0xFFFFFFFF;
            context_ptr->reduce_me_sr_divisor[li][ri] = 1;
#if FIX_HME_ME_EARLY_EXIT
            context_ptr->zz_sad[li][ri] = (uint32_t)~0;
#endif
        }
    }

}

/*******************************************
 * motion_estimate_sb
 *   performs ME (SB)
 *******************************************/


EbErrorType motion_estimate_sb(
    PictureParentControlSet *pcs_ptr, // input parameter, Picture Control Set Ptr
    uint32_t                 sb_index, // input parameter, SB Index
    uint32_t                 sb_origin_x, // input parameter, SB Origin X
    uint32_t                 sb_origin_y, // input parameter, SB Origin X
    MeContext
        *context_ptr, // input parameter, ME Context Ptr, used to store decimated/interpolated SB/SR
    EbPictureBufferDesc *input_ptr) // input parameter, source Picture Ptr

{
    EbErrorType         return_error = EB_ErrorNone;

#if! OPT_ME
    uint32_t            max_number_of_pus_per_sb = pcs_ptr->max_number_of_pus_per_sb;
#endif
    uint32_t num_of_list_to_search = context_ptr->num_of_list_to_search;

    //pruning of the references is not done for alt-ref / when HMeLevel2 not done
    uint8_t prune_ref = context_ptr->enable_hme_flag &&
        context_ptr->me_type != ME_MCTF;
    // Initialize ME/HME buffers
    init_me_hme_data(context_ptr);
    // HME: Perform Hierachical Motion Estimation for all refrence frames.
    hme_sb(pcs_ptr, sb_origin_x, sb_origin_y, context_ptr, input_ptr);
#if OPT_EARLY_TF_ME_EXIT
    if (context_ptr->me_type == ME_MCTF && context_ptr->hme_results[0][0].hme_sad < context_ptr->tf_me_exit_th) {
        context_ptr->tf_use_pred_64x64_only_th = (uint8_t)~0;
        return return_error;
    }
#endif
    // prune the refrence frames based on the HME outputs.
    if (prune_ref &&
        (context_ptr->me_sr_adjustment_ctrls.enable_me_sr_adjustment ||
         context_ptr->me_hme_prune_ctrls.enable_me_hme_ref_pruning)) {
        hme_prune_ref_and_adjust_sr(context_ptr);
    }
    // Full pel: Perform the Integer Motion Estimation on the allowed refrence frames.
    integer_search_sb(pcs_ptr, sb_index, sb_origin_x, sb_origin_y, context_ptr, input_ptr);
    // prune the refrence frames
    if (prune_ref && context_ptr->me_hme_prune_ctrls.enable_me_hme_ref_pruning) {
        me_prune_ref(context_ptr);
    }

    if (context_ptr->me_type != ME_MCTF) {

#if OPT_ME
        {
#else
        // Bi-Prediction motion estimation loop
        for (uint32_t pu_index = 0; pu_index < max_number_of_pus_per_sb; ++pu_index) {
#endif
#if SS_CLN_ME_CAND_ARRAY
            if (context_ptr->num_of_ref_pic_to_search[REF_LIST_0] == 1 &&
                context_ptr->num_of_ref_pic_to_search[REF_LIST_1] == 1)
                construct_me_candidate_array_mrp_off(
                    pcs_ptr,
                    context_ptr,
                    num_of_list_to_search,
                    sb_index);
            else
#endif
            construct_me_candidate_array(
                pcs_ptr,
                context_ptr,
                num_of_list_to_search,
#if !OPT_ME
                pu_index,
#endif
                sb_index);
        }
        if (context_ptr->me_type != ME_FIRST_PASS)
        // Save the distortion per block size
        compute_distortion(pcs_ptr, sb_index, context_ptr);

        // Perform GM detection if GM is enabled
        pcs_ptr->stationary_block_present_sb[sb_index] = 0;
        pcs_ptr->rc_me_allow_gm[sb_index] = 0;

        if (pcs_ptr->gm_ctrls.enabled)
            perform_gm_detection(pcs_ptr, sb_index, context_ptr);
    }

    return return_error;
}




EbErrorType open_loop_intra_search_mb(
    PictureParentControlSet *pcs_ptr, uint32_t sb_index,
    EbPictureBufferDesc *input_ptr)
{
    EbErrorType return_error = EB_ErrorNone;
    SequenceControlSet *scs_ptr =  pcs_ptr->scs_ptr;

    uint32_t cu_origin_x;
    uint32_t cu_origin_y;
    uint32_t pa_blk_index = 0;
    SbParams *sb_params = &scs_ptr->sb_params_array[sb_index];
    OisMbResults *ois_mb_results_ptr;
    uint8_t *above_row;
    uint8_t *left_col;
    uint8_t *above0_row;
    uint8_t *left0_col;
    uint32_t mb_stride = (scs_ptr->seq_header.max_frame_width + 15) / 16;

    DECLARE_ALIGNED(16, uint8_t, left0_data[MAX_TX_SIZE * 2 + 32]);
    DECLARE_ALIGNED(16, uint8_t, above0_data[MAX_TX_SIZE * 2 + 32]);
    DECLARE_ALIGNED(16, uint8_t, left_data[MAX_TX_SIZE * 2 + 32]);
    DECLARE_ALIGNED(16, uint8_t, above_data[MAX_TX_SIZE * 2 + 32]);

    DECLARE_ALIGNED(32, uint8_t, predictor8[256 * 2]);
    DECLARE_ALIGNED(32, int16_t, src_diff[256]);
    DECLARE_ALIGNED(32, int32_t, coeff[256]);
    uint8_t *predictor = predictor8;

    while (pa_blk_index < CU_MAX_COUNT) {
        const CodedBlockStats *blk_stats_ptr;
        blk_stats_ptr = get_coded_blk_stats(pa_blk_index);
        uint8_t bsize = blk_stats_ptr->size;
        EbBool small_boundary_blk = EB_FALSE;

        //if(sb_params->raster_scan_blk_validity[md_scan_to_raster_scan[pa_blk_index]])
        {
            cu_origin_x = sb_params->origin_x + blk_stats_ptr->origin_x;
            cu_origin_y = sb_params->origin_y + blk_stats_ptr->origin_y;
            if ((blk_stats_ptr->origin_x % 16) == 0 && (blk_stats_ptr->origin_y % 16) == 0 &&
                ((pcs_ptr->enhanced_picture_ptr->width - cu_origin_x) < 16 || (pcs_ptr->enhanced_picture_ptr->height - cu_origin_y) < 16))
                small_boundary_blk = EB_TRUE;
        }

        if(bsize != 16 && !small_boundary_blk) {
            pa_blk_index++;
            continue;
        }
        if (sb_params->raster_scan_blk_validity[md_scan_to_raster_scan[pa_blk_index]]) {
            // always process as block16x16 even bsize or tx_size is 8x8
            TxSize tx_size = TX_16X16;
            bsize = 16;
            cu_origin_x = sb_params->origin_x + blk_stats_ptr->origin_x;
            cu_origin_y = sb_params->origin_y + blk_stats_ptr->origin_y;
            above0_row = above0_data + 16;
            left0_col = left0_data + 16;
            above_row = above_data + 16;
            left_col = left_data + 16;
#if OPT_TPL_DATA
            ois_mb_results_ptr = pcs_ptr->pa_me_data->ois_mb_results[(cu_origin_y >> 4) * mb_stride + (cu_origin_x >> 4)];
#else
            ois_mb_results_ptr = pcs_ptr->ois_mb_results[(cu_origin_y >> 4) * mb_stride + (cu_origin_x >> 4)];
#endif
            memset(ois_mb_results_ptr, 0, sizeof(*ois_mb_results_ptr));
            uint8_t *src = input_ptr->buffer_y + pcs_ptr->enhanced_picture_ptr->origin_x + cu_origin_x +
                           (pcs_ptr->enhanced_picture_ptr->origin_y + cu_origin_y) * input_ptr->stride_y;

            // Fill Neighbor Arrays
            update_neighbor_samples_array_open_loop_mb(
                                                        1, // use_top_righ_bottom_left
                                                        1, // update_top_neighbor
                                                       above0_row - 1, left0_col - 1,
                                                       input_ptr, input_ptr->stride_y, cu_origin_x, cu_origin_y, bsize, bsize);
            uint8_t ois_intra_mode;
            uint8_t intra_mode_start = DC_PRED;
            EbBool   enable_paeth                = pcs_ptr->scs_ptr->static_config.enable_paeth == DEFAULT ? EB_TRUE : (EbBool) pcs_ptr->scs_ptr->static_config.enable_paeth;
            EbBool   enable_smooth               = pcs_ptr->scs_ptr->static_config.enable_smooth == DEFAULT ? EB_TRUE : (EbBool) pcs_ptr->scs_ptr->static_config.enable_smooth;
            uint8_t intra_mode_end =
                pcs_ptr->tpl_ctrls.tpl_opt_flag
                    ? DC_PRED
                    : enable_paeth ? PAETH_PRED : enable_smooth ? SMOOTH_H_PRED : D67_PRED;
            PredictionMode best_mode       = DC_PRED;
            int64_t        best_intra_cost = INT64_MAX;

            for (ois_intra_mode = intra_mode_start; ois_intra_mode <= intra_mode_end; ++ois_intra_mode) {
                int32_t p_angle = av1_is_directional_mode((PredictionMode)ois_intra_mode) ? mode_to_angle_map[(PredictionMode)ois_intra_mode] : 0;
                // Edge filter
                if(av1_is_directional_mode((PredictionMode)ois_intra_mode) && 1/*scs_ptr->seq_header.enable_intra_edge_filter*/) {
                    EB_MEMCPY(left_data,  left0_data,  sizeof(uint8_t)*(MAX_TX_SIZE * 2 + 32));
                    EB_MEMCPY(above_data, above0_data, sizeof(uint8_t)*(MAX_TX_SIZE * 2 + 32));
                    above_row = above_data + 16;
                    left_col  = left_data + 16;
                    filter_intra_edge(ois_mb_results_ptr, ois_intra_mode, scs_ptr->seq_header.max_frame_width, scs_ptr->seq_header.max_frame_height, p_angle, (int32_t)cu_origin_x, (int32_t)cu_origin_y, above_row, left_col);
                } else {
                    above_row = above0_row;
                    left_col  = left0_col;
                }
                // PRED
                intra_prediction_open_loop_mb(p_angle, ois_intra_mode, cu_origin_x, cu_origin_y, tx_size, above_row, left_col, predictor, 16);
                // Distortion
                int64_t intra_cost;
                if (pcs_ptr->tpl_ctrls.tpl_opt_flag && pcs_ptr->tpl_ctrls.use_pred_sad_in_intra_search) {
                    intra_cost = svt_nxm_sad_kernel_sub_sampled(
                        src,
                        input_ptr->stride_y,
                        predictor,
                        16,
                        16,
                        16);
                }
                else {
#if FTR_TPL_TX_SUBSAMPLE
                    TxSize cost_tx_size = pcs_ptr->tpl_ctrls.subsample_tx ? TX_16X8 : TX_16X16;
                    svt_aom_subtract_block(16 >> pcs_ptr->tpl_ctrls.subsample_tx,
                        16,
                        src_diff,
                        16 << pcs_ptr->tpl_ctrls.subsample_tx,
                        src,
                        input_ptr->stride_y << pcs_ptr->tpl_ctrls.subsample_tx,
                        predictor,
                        16 << pcs_ptr->tpl_ctrls.subsample_tx);

                    EB_TRANS_COEFF_SHAPE pf_shape = pcs_ptr->tpl_ctrls.tpl_opt_flag ? pcs_ptr->tpl_ctrls.pf_shape : DEFAULT_SHAPE;
                    svt_av1_wht_fwd_txfm(src_diff, 16 << pcs_ptr->tpl_ctrls.subsample_tx, coeff, cost_tx_size, pf_shape, 8, 0);

                    intra_cost = svt_aom_satd(coeff, 256 >> pcs_ptr->tpl_ctrls.subsample_tx) << pcs_ptr->tpl_ctrls.subsample_tx;
#else
                    svt_aom_subtract_block(16, 16, src_diff, 16, src, input_ptr->stride_y, predictor, 16);
                    EB_TRANS_COEFF_SHAPE pf_shape = pcs_ptr->tpl_ctrls.tpl_opt_flag ? pcs_ptr->tpl_ctrls.pf_shape : DEFAULT_SHAPE;
                    svt_av1_wht_fwd_txfm(src_diff, 16, coeff, 2/*TX_16X16*/, pf_shape, 8, 0);
                    intra_cost = svt_aom_satd(coeff, 16 * 16);
#endif
                }
                // printf("open_loop_intra_search_mb aom_satd mbxy %d %d, mode=%d, satd=%d, dst[0~4]=0x%d,%d,%d,%d\n", cu_origin_x, cu_origin_y, ois_intra_mode, intra_cost, predictor[0], predictor[1], predictor[2], predictor[3]);
                if (intra_cost < best_intra_cost) {
                    best_intra_cost = intra_cost;
                    best_mode = ois_intra_mode;
                }
            }
            // store intra_cost to pcs
            ois_mb_results_ptr->intra_mode = best_mode;
            ois_mb_results_ptr->intra_cost = best_intra_cost;
            //if(pcs_ptr->picture_number == 16 && cu_origin_x <= 15 && cu_origin_y == 0)
            //    printf("open_loop_intra_search_mb cost0 poc%d sb_index=%d, mb_origin_xy=%d %d, best_mode=%d, best_intra_cost=%d, offset=%d, src[0~3]= %d %d %d %d\n", pcs_ptr->picture_number, sb_index, cu_origin_x, cu_origin_y, best_mode, best_intra_cost, (cu_origin_y >> 4) * mb_stride + (cu_origin_x >> 4), src[0], src[1], src[2], src[3]);
        }
        pa_blk_index++;
    }
    return return_error;
}
