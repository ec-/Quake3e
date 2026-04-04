/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "q_shared.h"
#include "sizebuf.h"
#include "intreadwrite.h"

void SZ_Init(sizebuf_t *buf, void *data, size_t size, const char *tag)
{
    assert(size <= INT32_MAX);
    memset(buf, 0, sizeof(*buf));
    buf->data = data;
    buf->maxsize = size;
    buf->tag = tag;
}

void SZ_InitWrite(sizebuf_t *buf, void *data, size_t size)
{
    SZ_Init(buf, data, size, "none");
    buf->allowoverflow = qtrue;
}

void SZ_InitRead(sizebuf_t *buf, const void *data, size_t size)
{
    SZ_Init(buf, (void *)data, size, "none");
    buf->cursize = size;
    buf->allowunderflow = qtrue;
}

void SZ_Clear(sizebuf_t *buf)
{
    buf->cursize = 0;
    buf->readcount = 0;
    buf->overflowed = qfalse;
}

void *SZ_GetSpace(sizebuf_t *buf, size_t len)
{
    void    *data;

    if (buf->cursize > buf->maxsize) {
        Com_Error(ERR_FATAL,
                  "%s: %s: already overflowed",
                  __func__, buf->tag);
    }

    if (len > buf->maxsize - buf->cursize) {
        if (len > buf->maxsize) {
            Com_Error(ERR_FATAL,
                      "%s: %s: %"PRIz"u is > full buffer size %u",
                      __func__, buf->tag, len, buf->maxsize);
        }

        if (!buf->allowoverflow) {
            Com_Error(ERR_FATAL,
                      "%s: %s: overflow without allowoverflow set",
                      __func__, buf->tag);
        }

        //Com_DPrintf("%s: %s: overflow\n", __func__, buf->tag);
        SZ_Clear(buf);
        buf->overflowed = qtrue;
    }

    data = buf->data + buf->cursize;
    buf->cursize += len;
    return data;
}

void SZ_WriteByte(sizebuf_t *sb, int c)
{
    WN8(SZ_GetSpace(sb, 1), c);
}

void SZ_WriteShort(sizebuf_t *sb, int c)
{
    WL16(SZ_GetSpace(sb, 2), c);
}

void SZ_WriteLong(sizebuf_t *sb, int c)
{
    WL32(SZ_GetSpace(sb, 4), c);
}

void *SZ_ReadData(sizebuf_t *buf, size_t len)
{
    void    *data;

    if (buf->readcount > buf->cursize || len > buf->cursize - buf->readcount) {
        if (!buf->allowunderflow) {
            Com_Error(ERR_DROP, "%s: read past end of message", __func__);
        }
        buf->readcount = buf->cursize + 1;
        return NULL;
    }

    data = buf->data + buf->readcount;
    buf->readcount += len;
    return data;
}

int SZ_ReadByte(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 1);
    return buf ? RN8(buf) : -1;
}

int SZ_ReadShort(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 2);
    return buf ? RL16(buf) : -1;
}

int SZ_ReadLong(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 4);
    return buf ? RL32(buf) : -1;
}

int64_t SZ_ReadLong64(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 8);
    return buf ? RL64(buf) : -1;
}

uint32_t SZ_ReadLeb(sizebuf_t *sb)
{
    uint32_t v = 0;
    int c, bits = 0;

    do {
        if (bits >= 32)
            return -1;
        c = SZ_ReadByte(sb);
        if (c == -1)
            return -1;
        v |= (c & UINT32_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    return v;
}

static inline int64_t SignExtend64(uint64_t v, int bits)
{
    return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

int64_t SZ_ReadSignedLeb(sizebuf_t *sb, int len)
{
    uint64_t v = 0;
    int c, bits = 0;

    do {
        if (bits >= len)
            return -1;
        c = SZ_ReadByte(sb);
        if (c == -1)
            return -1;
        v |= (c & UINT64_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    if (bits < len)
        return SignExtend64(v, bits);
    return v;
}
