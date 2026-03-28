/*
Copyright (C) 2026 skullernet

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

//
// intreadwrite.h -- macros for fast unaligned integer R/W.
//

struct unaligned16 { uint16_t u; } __attribute__((packed, may_alias));
struct unaligned32 { uint32_t u; } __attribute__((packed, may_alias));
struct unaligned64 { uint64_t u; } __attribute__((packed, may_alias));

#define RN8(p)      (*(const uint8_t *)(p))
#define RN16(p)     (((const struct unaligned16 *)(p))->u)
#define RN32(p)     (((const struct unaligned32 *)(p))->u)
#define RN64(p)     (((const struct unaligned64 *)(p))->u)

#define RN8S(p)     ((int8_t)RN8(p))
#define RN16S(p)    ((int16_t)RN16(p))
#define RN32S(p)    ((int32_t)RN32(p))
#define RN64S(p)    ((int64_t)RN64(p))

#define WN8(p, v)   (*(uint8_t *)(p) = (v))
#define WN16(p, v)  (((struct unaligned16 *)(p))->u = (v))
#define WN32(p, v)  (((struct unaligned32 *)(p))->u = (v))
#define WN64(p, v)  (((struct unaligned64 *)(p))->u = (v))

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define RL16(p) RN16(p)
#define RL32(p) RN32(p)
#define RL64(p) RN64(p)

#define WL16(p, v) WN16(p, v)
#define WL32(p, v) WN32(p, v)
#define WL64(p, v) WN64(p, v)

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define RL16(p) __builtin_bswap16(RN16(p))
#define RL32(p) __builtin_bswap32(RN32(p))
#define RL64(p) __builtin_bswap64(RN64(p))

#define WL16(p, v) WN16(p, __builtin_bswap16(v))
#define WL32(p, v) WN32(p, __builtin_bswap32(v))
#define WL64(p, v) WN64(p, __builtin_bswap64(v))

#else
#error Unknown byte order
#endif
