/*
* Simd Library.
*
* Copyright (c) 2011-2014 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy 
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
* copies of the Software, and to permit persons to whom the Software is 
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in 
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "Simd/SimdMemory.h"
#include "Simd/SimdLoad.h"
#include "Simd/SimdStore.h"
#include "Simd/SimdExtract.h"
#include "Simd/SimdConst.h"
#include "Simd/SimdMath.h"
#include "Simd/SimdCompare.h"
#include "Simd/SimdSse2.h"

namespace Simd
{
#ifdef SIMD_SSE2_ENABLE    
    namespace Sse2
    {
        template <bool align> void GetStatistic(const uint8_t * src, size_t stride, size_t width, size_t height, 
             uint8_t * min, uint8_t * max, uint8_t * average)
        {
            assert(width*height && width >= A);
            if(align)
                assert(Aligned(src) && Aligned(stride));

            size_t bodyWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + bodyWidth);
            __m128i sum = _mm_setzero_si128();
            __m128i min_ = K_INV_ZERO;
            __m128i max_ = K_ZERO;
            for(size_t row = 0; row < height; ++row)
            {
                for(size_t col = 0; col < bodyWidth; col += A)
                {
                    const __m128i value = Load<align>((__m128i*)(src + col));
                    min_ = _mm_min_epu8(min_, value);
                    max_ = _mm_max_epu8(max_, value);
                    sum = _mm_add_epi64(_mm_sad_epu8(value, K_ZERO), sum);
                }
                if(width - bodyWidth)
                {
                    const __m128i value = Load<false>((__m128i*)(src + width - A));
                    min_ = _mm_min_epu8(min_, value);
                    max_ = _mm_max_epu8(max_, value);
                    sum = _mm_add_epi64(_mm_sad_epu8(_mm_and_si128(tailMask, value), K_ZERO), sum);
                }
                src += stride;
            }

             uint8_t min_buffer[A], max_buffer[A];
            _mm_storeu_si128((__m128i*)min_buffer, min_);
            _mm_storeu_si128((__m128i*)max_buffer, max_);
            *min = UCHAR_MAX;
            *max = 0;
            for (size_t i = 0; i < A; ++i)
            {
                *min = Base::MinU8(min_buffer[i], *min);
                *max = Base::MaxU8(max_buffer[i], *max);
            }
            *average = (uint8_t)((ExtractInt64Sum(sum) + width*height/2)/(width*height));
        }

        void GetStatistic(const uint8_t * src, size_t stride, size_t width, size_t height, 
             uint8_t * min, uint8_t * max, uint8_t * average)
        {
            if(Aligned(src) && Aligned(stride))
                GetStatistic<true>(src, stride, width, height, min, max, average);
            else
                GetStatistic<false>(src, stride, width, height, min, max, average);
        }

        template <bool small> void GetMoments16(__m128i row, __m128i col, 
            __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy);

        template<> SIMD_INLINE void GetMoments16<true>(__m128i row, __m128i col, 
            __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy)
        {
            x = _mm_add_epi32(x, _mm_madd_epi16(col, K16_0001));
            y = _mm_add_epi32(y, _mm_madd_epi16(row, K16_0001));
            xx = _mm_add_epi32(xx, _mm_madd_epi16(col, col));
            xy = _mm_add_epi32(xy, _mm_madd_epi16(col, row));
            yy = _mm_add_epi32(yy, _mm_madd_epi16(row,row));
        }

        template<> SIMD_INLINE void GetMoments16<false>(__m128i row, __m128i col, 
            __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy)
        {
            x = _mm_add_epi32(x, _mm_madd_epi16(col, K16_0001));
            y = _mm_add_epi32(y, _mm_madd_epi16(row, K16_0001));
            xx = _mm_add_epi64(xx, HorizontalSum32(_mm_madd_epi16(col, col)));
            xy = _mm_add_epi64(xy, HorizontalSum32(_mm_madd_epi16(col, row)));
            yy = _mm_add_epi64(yy, HorizontalSum32(_mm_madd_epi16(row, row)));
        }

        template <bool small> SIMD_INLINE void GetMoments8(__m128i mask, __m128i & row, __m128i & col, 
            __m128i & area, __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy)
        {
            area = _mm_add_epi64(area, _mm_sad_epu8(_mm_and_si128(K8_01, mask), K_ZERO));

            const __m128i lo = _mm_cmpeq_epi16(_mm_unpacklo_epi8(mask, K_ZERO), K16_00FF);
            GetMoments16<small>(_mm_and_si128(lo, row), _mm_and_si128(lo, col), x, y, xx, xy, yy);
            col = _mm_add_epi16(col, K16_0008);

            const __m128i hi = _mm_cmpeq_epi16(_mm_unpackhi_epi8(mask, K_ZERO), K16_00FF);
            GetMoments16<small>(_mm_and_si128(hi, row), _mm_and_si128(hi, col), x, y, xx, xy, yy);
            col = _mm_add_epi16(col, K16_0008);
        }

        template <bool align> void GetMomentsSmall(const uint8_t * mask, size_t stride, size_t width, size_t height, uint8_t index, 
            __m128i & area, __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy)
        {
            size_t alignedWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedWidth);

            const __m128i K16_I = _mm_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7);
            const __m128i _index = _mm_set1_epi8(index);
            const __m128i tailCol = _mm_add_epi16(K16_I, _mm_set1_epi16((uint16_t)(width - A)));

            for(size_t row = 0; row < height; ++row)
            {
                __m128i _col = K16_I;
                __m128i _row = _mm_set1_epi16((short)row);

                __m128i _rowX = _mm_setzero_si128();
                __m128i _rowY = _mm_setzero_si128();
                __m128i _rowXX = _mm_setzero_si128();
                __m128i _rowXY = _mm_setzero_si128();
                __m128i _rowYY = _mm_setzero_si128();
                for(size_t col = 0; col < alignedWidth; col += A)
                {
                    __m128i _mask = _mm_cmpeq_epi8(Load<align>((__m128i*)(mask + col)), _index);
                    GetMoments8<true>(_mask, _row, _col, area, _rowX, _rowY, _rowXX, _rowXY, _rowYY);
                }
                if(alignedWidth != width)
                {
                    __m128i _mask = _mm_and_si128(_mm_cmpeq_epi8(Load<false>((__m128i*)(mask + width - A)), _index), tailMask);
                    _col = tailCol;
                    GetMoments8<true>(_mask, _row, _col, area, _rowX, _rowY, _rowXX, _rowXY, _rowYY);
                }
                x = _mm_add_epi64(x, HorizontalSum32(_rowX));
                y = _mm_add_epi64(y, HorizontalSum32(_rowY));
                xx = _mm_add_epi64(xx, HorizontalSum32(_rowXX));
                xy = _mm_add_epi64(xy, HorizontalSum32(_rowXY));
                yy = _mm_add_epi64(yy, HorizontalSum32(_rowYY));

                mask += stride;
            }
        }

        template <bool align> void GetMomentsLarge(const uint8_t * mask, size_t stride, size_t width, size_t height, uint8_t index, 
            __m128i & area, __m128i & x, __m128i & y, __m128i & xx, __m128i & xy, __m128i & yy)
        {
            size_t alignedWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedWidth);

            const __m128i K16_I = _mm_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7);
            const __m128i _index = _mm_set1_epi8(index);
            const __m128i tailCol = _mm_add_epi16(K16_I, _mm_set1_epi16((uint16_t)(width - A)));

            for(size_t row = 0; row < height; ++row)
            {
                __m128i _col = K16_I;
                __m128i _row = _mm_set1_epi16((short)row);

                __m128i _rowX = _mm_setzero_si128();
                __m128i _rowY = _mm_setzero_si128();
                for(size_t col = 0; col < alignedWidth; col += A)
                {
                    __m128i _mask = _mm_cmpeq_epi8(Load<align>((__m128i*)(mask + col)), _index);
                    GetMoments8<false>(_mask, _row, _col, area, _rowX, _rowY, xx, xy, yy);
                }
                if(alignedWidth != width)
                {
                    __m128i _mask = _mm_and_si128(_mm_cmpeq_epi8(Load<false>((__m128i*)(mask + width - A)), _index), tailMask);
                    _col = tailCol;
                    GetMoments8<false>(_mask, _row, _col, area, _rowX, _rowY, xx, xy, yy);
                }
                x = _mm_add_epi64(x, HorizontalSum32(_rowX));
                y = _mm_add_epi64(y, HorizontalSum32(_rowY));

                mask += stride;
            }
        }

        SIMD_INLINE bool IsSmall(uint64_t width, uint64_t height)
        {
            return 
                width*width*width < 0x300000000ULL && 
                width*width*height < 0x200000000ULL && 
                width*height*height < 0x100000000ULL;
        }

        template <bool align> void GetMoments(const uint8_t * mask, size_t stride, size_t width, size_t height, uint8_t index, 
            uint64_t * area, uint64_t * x, uint64_t * y, uint64_t * xx, uint64_t * xy, uint64_t * yy)
        {
            assert(width >= A && width < SHRT_MAX && height < SHRT_MAX);
            if(align)
                assert(Aligned(mask) && Aligned(stride));

            __m128i _area = _mm_setzero_si128();
            __m128i _x = _mm_setzero_si128();
            __m128i _y = _mm_setzero_si128();
            __m128i _xx = _mm_setzero_si128();
            __m128i _xy = _mm_setzero_si128();
            __m128i _yy = _mm_setzero_si128();

            if(IsSmall(width, height))
                GetMomentsSmall<align>(mask, stride, width, height, index, _area, _x, _y, _xx, _xy, _yy);
            else
                GetMomentsLarge<align>(mask, stride, width, height, index, _area, _x, _y, _xx, _xy, _yy);

            *area = ExtractInt64Sum(_area);
            *x = ExtractInt64Sum(_x);
            *y = ExtractInt64Sum(_y);
            *xx = ExtractInt64Sum(_xx);
            *xy = ExtractInt64Sum(_xy);
            *yy = ExtractInt64Sum(_yy);
        }

        void GetMoments(const uint8_t * mask, size_t stride, size_t width, size_t height, uint8_t index, 
            uint64_t * area, uint64_t * x, uint64_t * y, uint64_t * xx, uint64_t * xy, uint64_t * yy)
        {
            if(Aligned(mask) && Aligned(stride))
                GetMoments<true>(mask, stride, width, height, index, area, x, y, xx, xy, yy);
            else
                GetMoments<false>(mask, stride, width, height, index, area, x, y, xx, xy, yy);
        }

        template <bool align> void GetRowSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            size_t alignedWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedWidth);

            memset(sums, 0, sizeof(uint32_t)*height);
            for(size_t row = 0; row < height; ++row)
            {
                __m128i sum = _mm_setzero_si128();
                for(size_t col = 0; col < alignedWidth; col += A)
                {
                    __m128i _src = Load<align>((__m128i*)(src + col));
                    sum = _mm_add_epi32(sum, _mm_sad_epu8(_src, K_ZERO));
                }
                if(alignedWidth != width)
                {
                    __m128i _src = _mm_and_si128(Load<false>((__m128i*)(src + width - A)), tailMask);
                    sum = _mm_add_epi32(sum, _mm_sad_epu8(_src, K_ZERO));
                }
                sums[row] = ExtractInt32Sum(sum);
                src += stride;
            }
        }

        void GetRowSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            if(Aligned(src) && Aligned(stride))
                GetRowSums<true>(src, stride, width, height, sums);
            else
                GetRowSums<false>(src, stride, width, height, sums);
        }

        namespace
        {
            struct Buffer
            {
                Buffer(size_t width)
                {
                    _p = Allocate(sizeof(uint16_t)*width + sizeof(uint32_t)*width);
                    sums16 = (uint16_t*)_p;
                    sums32 = (uint32_t*)(sums16 + width);
                }

                ~Buffer()
                {
                    Free(_p);
                }

                uint16_t * sums16;
                uint32_t * sums32;
            private:
                void *_p;
            };
        }

        template <bool align> SIMD_INLINE void Sum16(__m128i src8, uint16_t * sums16)
        {
            Store<align>((__m128i*)sums16 + 0, _mm_add_epi16(Load<align>((__m128i*)sums16 + 0), _mm_unpacklo_epi8(src8, K_ZERO)));
            Store<align>((__m128i*)sums16 + 1, _mm_add_epi16(Load<align>((__m128i*)sums16 + 1), _mm_unpackhi_epi8(src8, K_ZERO)));
        }

        template <bool align> SIMD_INLINE void Sum32(__m128i src16, uint32_t * sums32)
        {
            Store<align>((__m128i*)sums32 + 0, _mm_add_epi32(Load<align>((__m128i*)sums32 + 0), _mm_unpacklo_epi16(src16, K_ZERO)));
            Store<align>((__m128i*)sums32 + 1, _mm_add_epi32(Load<align>((__m128i*)sums32 + 1), _mm_unpackhi_epi16(src16, K_ZERO)));
        }

        template <bool align> void GetColSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            size_t alignedLoWidth = AlignLo(width, A);
            size_t alignedHiWidth = AlignHi(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedLoWidth);
            size_t stepSize = SCHAR_MAX + 1;
            size_t stepCount = (height + SCHAR_MAX)/stepSize;

            Buffer buffer(alignedHiWidth);
            memset(buffer.sums32, 0, sizeof(uint32_t)*alignedHiWidth);
            for(size_t step = 0; step < stepCount; ++step)
            {
                size_t rowStart = step*stepSize;
                size_t rowEnd = Min(rowStart + stepSize, height);

                memset(buffer.sums16, 0, sizeof(uint16_t)*width);
                for(size_t row = rowStart; row < rowEnd; ++row)
                {
                    for(size_t col = 0; col < alignedLoWidth; col += A)
                    {
                        __m128i src8 = Load<align>((__m128i*)(src + col));
                        Sum16<true>(src8, buffer.sums16 + col);
                    }
                    if(alignedLoWidth != width)
                    {
                        __m128i src8 = _mm_and_si128(Load<false>((__m128i*)(src + width - A)), tailMask);
                        Sum16<false>(src8, buffer.sums16 + width - A);
                    }
                    src += stride;
                }

                for(size_t col = 0; col < alignedHiWidth; col += HA)
                {
                    __m128i src16 = Load<true>((__m128i*)(buffer.sums16 + col));
                    Sum32<true>(src16, buffer.sums32 + col);
                }
            }
            memcpy(sums, buffer.sums32, sizeof(uint32_t)*width);
        }

        void GetColSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            if(Aligned(src) && Aligned(stride))
                GetColSums<true>(src, stride, width, height, sums);
            else
                GetColSums<false>(src, stride, width, height, sums);
        }

        template <bool align> void GetAbsDyRowSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            size_t alignedWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedWidth);

            memset(sums, 0, sizeof(uint32_t)*height);
            const uint8_t * src0 = src;
            const uint8_t * src1 = src + stride;
            height--;
            for(size_t row = 0; row < height; ++row)
            {
                __m128i sum = _mm_setzero_si128();
                for(size_t col = 0; col < alignedWidth; col += A)
                {
                    __m128i _src0 = Load<align>((__m128i*)(src0 + col));
                    __m128i _src1 = Load<align>((__m128i*)(src1 + col));
                    sum = _mm_add_epi32(sum, _mm_sad_epu8(_src0, _src1));
                }
                if(alignedWidth != width)
                {
                    __m128i _src0 = _mm_and_si128(Load<false>((__m128i*)(src0 + width - A)), tailMask);
                    __m128i _src1 = _mm_and_si128(Load<false>((__m128i*)(src1 + width - A)), tailMask);
                    sum = _mm_add_epi32(sum, _mm_sad_epu8(_src0, _src1));
                }
                sums[row] = ExtractInt32Sum(sum);
                src0 += stride;
                src1 += stride;
            }
        }

        void GetAbsDyRowSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            if(Aligned(src) && Aligned(stride))
                GetAbsDyRowSums<true>(src, stride, width, height, sums);
            else
                GetAbsDyRowSums<false>(src, stride, width, height, sums);
        }

        template <bool align> void GetAbsDxColSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            width--;
            size_t alignedLoWidth = AlignLo(width, A);
            size_t alignedHiWidth = AlignHi(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + alignedLoWidth);
            size_t stepSize = SCHAR_MAX + 1;
            size_t stepCount = (height + SCHAR_MAX)/stepSize;

            Buffer buffer(alignedHiWidth);
            memset(buffer.sums32, 0, sizeof(uint32_t)*alignedHiWidth);
            for(size_t step = 0; step < stepCount; ++step)
            {
                size_t rowStart = step*stepSize;
                size_t rowEnd = Min(rowStart + stepSize, height);

                memset(buffer.sums16, 0, sizeof(uint16_t)*width);
                for(size_t row = rowStart; row < rowEnd; ++row)
                {
                    for(size_t col = 0; col < alignedLoWidth; col += A)
                    {
                        __m128i _src0 = Load<align>((__m128i*)(src + col + 0));
                        __m128i _src1 = Load<false>((__m128i*)(src + col + 1));
                        Sum16<true>(AbsDifferenceU8(_src0, _src1), buffer.sums16 + col);
                    }
                    if(alignedLoWidth != width)
                    {
                        __m128i _src0 = Load<false>((__m128i*)(src + width - A + 0));
                        __m128i _src1 = Load<false>((__m128i*)(src + width - A + 1));
                        Sum16<false>(_mm_and_si128(AbsDifferenceU8(_src0, _src1), tailMask), buffer.sums16 + width - A);
                    }
                    src += stride;
                }

                for(size_t col = 0; col < alignedHiWidth; col += HA)
                {
                    __m128i src16 = Load<true>((__m128i*)(buffer.sums16 + col));
                    Sum32<true>(src16, buffer.sums32 + col);
                }
            }
            memcpy(sums, buffer.sums32, sizeof(uint32_t)*width);
            sums[width] = 0;
        }

        void GetAbsDxColSums(const uint8_t * src, size_t stride, size_t width, size_t height, uint32_t * sums)
        {
            if(Aligned(src) && Aligned(stride))
                GetAbsDxColSums<true>(src, stride, width, height, sums);
            else
                GetAbsDxColSums<false>(src, stride, width, height, sums);
        }

        template <bool align> void ValueSum(const uint8_t * src, size_t stride, size_t width, size_t height, uint64_t * sum)
        {
            assert(width >= A);
            if(align)
                assert(Aligned(src) && Aligned(stride));

            size_t bodyWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + bodyWidth);
            __m128i fullSum = _mm_setzero_si128();
            for(size_t row = 0; row < height; ++row)
            {
                for(size_t col = 0; col < bodyWidth; col += A)
                {
                    const __m128i src_ = Load<align>((__m128i*)(src + col));
                    fullSum = _mm_add_epi64(_mm_sad_epu8(src_, K_ZERO), fullSum);
                }
                if(width - bodyWidth)
                {
                    const __m128i src_ = _mm_and_si128(tailMask, Load<false>((__m128i*)(src + width - A)));
                    fullSum = _mm_add_epi64(_mm_sad_epu8(src_, K_ZERO), fullSum);
                }
                src += stride;
            }
            *sum = ExtractInt64Sum(fullSum);
        }

        void ValueSum(const uint8_t * src, size_t stride, size_t width, size_t height, uint64_t * sum)
        {
            if(Aligned(src) && Aligned(stride))
                ValueSum<true>(src, stride, width, height, sum);
            else
                ValueSum<false>(src, stride, width, height, sum);
        }

        SIMD_INLINE __m128i Square(__m128i src)
        {
            const __m128i lo = _mm_unpacklo_epi8(src, _mm_setzero_si128());
            const __m128i hi = _mm_unpackhi_epi8(src, _mm_setzero_si128());
            return _mm_add_epi32(_mm_madd_epi16(lo, lo), _mm_madd_epi16(hi, hi));
        }

        template <bool align> void SquareSum(const uint8_t * src, size_t stride, size_t width, size_t height, uint64_t * sum)
        {
            assert(width >= A);
            if(align)
                assert(Aligned(src) && Aligned(stride));

            size_t bodyWidth = AlignLo(width, A);
            __m128i tailMask = ShiftLeft(K_INV_ZERO, A - width + bodyWidth);
            __m128i fullSum = _mm_setzero_si128();
            for(size_t row = 0; row < height; ++row)
            {
                __m128i rowSum = _mm_setzero_si128();
                for(size_t col = 0; col < bodyWidth; col += A)
                {
                    const __m128i src_ = Load<align>((__m128i*)(src + col));
                    rowSum = _mm_add_epi32(rowSum, Square(src_));
                }
                if(width - bodyWidth)
                {
                    const __m128i src_ = _mm_and_si128(tailMask, Load<false>((__m128i*)(src + width - A)));
                    rowSum = _mm_add_epi32(rowSum, Square(src_));
                }
                fullSum = _mm_add_epi64(fullSum, HorizontalSum32(rowSum));
                src += stride;
            }
            *sum = ExtractInt64Sum(fullSum);
        }

        void SquareSum(const uint8_t * src, size_t stride, size_t width, size_t height, uint64_t * sum)
        {
            if(Aligned(src) && Aligned(stride))
                SquareSum<true>(src, stride, width, height, sum);
            else
                SquareSum<false>(src, stride, width, height, sum);
        }
    }
#endif// SIMD_SSE2_ENABLE
}