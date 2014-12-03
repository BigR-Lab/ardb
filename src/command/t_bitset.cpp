/*
 *Copyright (c) 2013-2014, yinqiwen <yinqiwen@gmail.com>
 *All rights reserved.
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 *BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ardb.hpp"

OP_NAMESPACE_BEGIN
    static const unsigned long BITOP_AND = 0;
    static const unsigned long BITOP_OR = 1;
    static const unsigned long BITOP_XOR = 2;
    static const unsigned long BITOP_NOT = 3;

    static const uint32 BIT_SUBSET_SIZE = 4096;
    static const uint32 BIT_SUBSET_BYTES_SIZE = BIT_SUBSET_SIZE >> 3;
    //copy from redis
    static long popcount(const void *s, long count)
    {
        long bits = 0;
        unsigned char *p;
        const uint32_t* p4 = (const uint32_t*) s;
        static const unsigned char bitsinbyte[256] =
            { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2,
                    3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3,
                    2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3,
                    4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3,
                    3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4,
                    5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5,
                    3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4,
                    5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8 };

        /* Count bits 16 bytes at a time */
        while (count >= 16)
        {
            uint32_t aux1, aux2, aux3, aux4;

            aux1 = *p4++;
            aux2 = *p4++;
            aux3 = *p4++;
            aux4 = *p4++;
            count -= 16;

            aux1 = aux1 - ((aux1 >> 1) & 0x55555555);
            aux1 = (aux1 & 0x33333333) + ((aux1 >> 2) & 0x33333333);
            aux2 = aux2 - ((aux2 >> 1) & 0x55555555);
            aux2 = (aux2 & 0x33333333) + ((aux2 >> 2) & 0x33333333);
            aux3 = aux3 - ((aux3 >> 1) & 0x55555555);
            aux3 = (aux3 & 0x33333333) + ((aux3 >> 2) & 0x33333333);
            aux4 = aux4 - ((aux4 >> 1) & 0x55555555);
            aux4 = (aux4 & 0x33333333) + ((aux4 >> 2) & 0x33333333);
            bits += ((((aux1 + (aux1 >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24)
                    + ((((aux2 + (aux2 >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24)
                    + ((((aux3 + (aux3 >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24)
                    + ((((aux4 + (aux4 >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24);
        }
        /* Count the remaining bytes */
        p = (unsigned char*) p4;
        while (count--)
            bits += bitsinbyte[*p++];
        return bits;
    }

    static long popcount_bitval(const std::string& val, int32 offset, int32 limit)
    {
        if (limit < 0)
        {
            limit = val.size() - 1;
        }
        if (val.size() > limit)
        {
            std::string tmp = val.substr(offset, limit - offset + 1);
            return popcount(tmp.data(), tmp.size());
        }
        else if (val.size() <= limit && val.size() <= offset)
        {
            return 0;
        }
        else if (val.size() > offset)
        {
            std::string tmp = val.substr(offset);
            return popcount(tmp.data(), tmp.size());
        }
        return 0;
    }

    int Ardb::String2BitSet(Context& ctx, ValueObject& meta)
    {
        meta.meta.str_value.ToString();
        int len = sdslen(meta.meta.str_value.value.sv);
        BatchWriteGuard guard(ctx);
        const char* str = meta.meta.str_value.value.sv;
        int count = len / BIT_SUBSET_BYTES_SIZE;
        if (len % BIT_SUBSET_BYTES_SIZE > 0)
        {
            count++;
        }
        int64 total_bit_count = 0;
        for (int i = 0; i < count; i++)
        {
            ValueObject bitvalue;
            bitvalue.key.type = BITSET_ELEMENT;
            bitvalue.key.db = ctx.currentDB;
            bitvalue.key.key = meta.key.key;
            bitvalue.key.score.SetInt64(i + 1);
            bitvalue.type = BITSET_ELEMENT;
            int str_len = BIT_SUBSET_BYTES_SIZE;
            if (i == count - 1)
            {
                str_len = len - (count - 1) * BIT_SUBSET_BYTES_SIZE;
            }
            Slice ss(str + i * BIT_SUBSET_BYTES_SIZE, str_len);
            bitvalue.element.SetString(ss, false);
            int bit_count = popcount(str + i * BIT_SUBSET_BYTES_SIZE, str_len);
            bitvalue.score.SetInt64(bit_count);
            SetKeyValue(ctx, bitvalue);
            total_bit_count += bit_count;
        }
        meta.type = BITSET_META;
        meta.meta.len = total_bit_count;
        meta.meta.min_index.SetInt64(1);
        meta.meta.max_index.SetInt64(count);
        SetKeyValue(ctx, meta);
        return 0;
    }

    static inline int SetStringBit(sds* str, uint64 offset, uint8 on)
    {
        int byte, bit;
        int byteval, bitval;
        byte = offset >> 3;
        if (sdslen(*str) < (byte + 1))
        {
            //bitvalue.element.str.resize(byte + 1);
            (*str) = sdsgrowzero(*str, byte + 1);
        }
        byteval = ((const uint8*) (*str))[byte];
        bit = 7 - (offset & 0x7);
        bitval = byteval & (1 << bit);

        if (bitval == on)
        {
            return bitval;
        }

        int tmp = byteval;

        byteval &= ~(1 << bit);
        byteval |= ((on & 0x1) << bit);
        if (byteval != tmp)
        {
            ((uint8_t*) (*str))[byte] = byteval;
        }
        return bitval == 0 ? 0 : 1;
    }

    int Ardb::SetBit(Context& ctx, RedisCommandFrame& cmd)
    {
        uint64 offset;
        if (!string_touint64(cmd.GetArguments()[1], offset))
        {
            fill_error_reply(ctx.reply, "value is not an integer or out of range");
            return 0;
        }
        if (cmd.GetArguments()[2] != "1" && cmd.GetArguments()[2] != "0")
        {
            fill_error_reply(ctx.reply, "bit is not an integer or out of range");
            return 0;
        }
        uint8 value = cmd.GetArguments()[2] != "0";
        KeyLockerGuard keyguard(m_key_lock, ctx.currentDB, cmd.GetArguments()[0]);
        int byte, bit;
        int byteval, bitval;
        long on = value;
        /* Bits can only be set or cleared... */
        if (on & ~1)
        {
            return -1;
        }
        bool set_changed = false;
        bool store_as_string = false;
        ValueObject meta;
        int err = GetMetaValue(ctx, cmd.GetArguments()[0], KEY_END, meta);
        if (0 == err && meta.type != BITSET_META && meta.type != STRING_META)
        {
            err = ERR_INVALID_TYPE;
        }
        CHECK_ARDB_RETURN_VALUE(ctx.reply, err);

        if (meta.type == STRING_META)
        {
            if (offset < m_cfg.max_string_bitset_value)
            {
                store_as_string = true;
            }
            else
            {
                store_as_string = false;
                String2BitSet(ctx, meta);
            }
        }
        else if (meta.type == BITSET_META)
        {
            store_as_string = false;
        }
        else if (0 != err)
        {
            if (offset < m_cfg.max_string_bitset_value)
            {
                store_as_string = true;
            }
            else
            {
                store_as_string = false;
                set_changed = true;
            }
        }

        if (!store_as_string)
        {
            meta.type = BITSET_META;
            meta.key.type = BITSET_META;
            uint64 index = (offset / BIT_SUBSET_SIZE) + 1;
            if (meta.meta.min_index.IsNil() || meta.meta.min_index.value.iv > index)
            {
                meta.meta.min_index.SetInt64(index);
                set_changed = true;
            }
            if (meta.meta.max_index.IsNil() || meta.meta.max_index.value.iv < index)
            {
                meta.meta.max_index.SetInt64(index);
                set_changed = true;
            }

            offset = offset % BIT_SUBSET_SIZE;

            ValueObject bitvalue;
            bitvalue.key.type = BITSET_ELEMENT;
            bitvalue.key.db = ctx.currentDB;
            bitvalue.key.key = cmd.GetArguments()[0];
            bitvalue.key.score.SetInt64(index);
            bitvalue.type = BITSET_ELEMENT;

            bool element_changed = false;
            GetKeyValue(ctx, bitvalue.key, &bitvalue);

            bitvalue.element.encoding = STRING_ENCODING_RAW;
            int old_bit = SetStringBit(&(bitvalue.element.value.sv), offset, on);
            if (old_bit != on)
            {
                if (on == 1)
                {
                    bitvalue.score.IncrBy(1);
                    meta.meta.len++;
                }
                else
                {
                    bitvalue.score.IncrBy(-1);
                    meta.meta.len--;
                }
                set_changed = true;
                element_changed = true;
            }
            BatchWriteGuard guard(ctx);
            if (set_changed)
            {
                SetKeyValue(ctx, meta);
            }
            if (element_changed)
            {
                SetKeyValue(ctx, bitvalue);
            }
        }
        else
        {
            meta.type = STRING_META;
            meta.key.type = STRING_META;
            meta.meta.str_value.ToString();
            bitval = SetStringBit(&(meta.meta.str_value.value.sv), offset, on);
            err = SetKeyValue(ctx, meta);
            CHECK_WRITE_RETURN_VALUE(ctx, err);
        }

        fill_int_reply(ctx.reply, bitval != 0 ? 1 : 0);
        return 0;
    }

    int Ardb::BitGet(Context& ctx, const std::string& key, uint64 bitoffset)
    {
        std::string val;
        ValueObject meta;
        int err = GetMetaValue(ctx, key, KEY_END, meta);
        fill_int_reply(ctx.reply, 0);
        if (0 != err)
        {
            return 0;
        }
        if (meta.type == BITSET_META)
        {
            uint64 index = (bitoffset / BIT_SUBSET_SIZE) + 1;
            KeyObject k;
            k.type = BITSET_ELEMENT;
            k.db = ctx.currentDB;
            k.key = key;
            k.score.SetInt64(index);
            ValueObject bitvalue;
            if (-1 == GetKeyValue(ctx, k, &bitvalue))
            {
                return 0;
            }
            bitvalue.element.GetDecodeString(val);
            bitoffset = bitoffset % BIT_SUBSET_SIZE;
        }
        else if (meta.type == STRING_META)
        {
            meta.meta.str_value.GetDecodeString(val);
        }
        else
        {
            err = ERR_INVALID_TYPE;
            CHECK_ARDB_RETURN_VALUE(ctx.reply, err);
        }

        size_t byte, bit;
        size_t bitval = 0;

        byte = bitoffset >> 3;
        if (byte >= val.size())
        {
            return 0;
        }
        int byteval = ((const uint8_t*) (&val[0]))[byte];
        bit = 7 - (bitoffset & 0x7);
        bitval = byteval & (1 << bit);
        fill_int_reply(ctx.reply, bitval != 0 ? 1 : 0);
        return 0;
    }

    int Ardb::GetBit(Context& ctx, RedisCommandFrame& cmd)
    {
        uint64 bitoffset;
        if (!string_touint64(cmd.GetArguments()[1], bitoffset))
        {
            fill_error_reply(ctx.reply, "value is not an integer or out of range");
            return 0;
        }
        BitGet(ctx, cmd.GetArguments()[0], bitoffset);
        return 0;
    }

    int Ardb::BitClear(Context& ctx, ValueObject& meta)
    {
        BatchWriteGuard guard(ctx);
        BitsetIterator iter;
        BitsetIter(ctx, meta, meta.meta.min_index.value.iv, iter);
        while (iter.Valid())
        {
            //DelRaw(ctx, iter.CurrentRawKey());
            KeyObject bk;
            bk.type = BITSET_ELEMENT;
            bk.db = ctx.currentDB;
            bk.key = meta.key.key;
            bk.score.SetInt64(iter.Index());
            DelKeyValue(ctx, bk);
            iter.Next();
        }
        DelKeyValue(ctx, meta.key);
        return 0;
    }

    int Ardb::StringBitSetOP(Context& ctx, uint32 op, ValueObjectArray& metas, const std::string* targetkey)
    {
        uint32 maxlen = 0;
        for (uint32 j = 0; j < metas.size(); j++)
        {
            metas[j].meta.str_value.ToString();
            if (op == BITOP_AND)
            {
                if (metas[j].meta.str_value.StringLength() < maxlen || maxlen == 0)
                {
                    maxlen = metas[j].meta.str_value.StringLength();
                }
            }
            else
            {
                if (metas[j].meta.str_value.StringLength() > maxlen)
                {
                    maxlen = metas[j].meta.str_value.StringLength();
                }
            }
        }
        while (maxlen % 8 != 0)
        {
            maxlen++;
        }
        if (metas[0].meta.str_value.StringLength() < maxlen)
        {
            metas[0].meta.str_value.value.sv = sdsgrowzero(metas[0].meta.str_value.value.sv, maxlen);
        }
        uint64* lres = (uint64*) (metas[0].meta.str_value.value.sv);
        uint32 k = 1;
        if (op == BITOP_NOT)
        {
            k = 0;
        }
        for (; k < metas.size(); k++)
        {
            if (metas[k].meta.str_value.StringLength() < maxlen)
            {
                metas[k].meta.str_value.value.sv = sdsgrowzero(metas[k].meta.str_value.value.sv, maxlen);
            }
            const uint64* lp = (const uint64*) (metas[k].meta.str_value.value.sv);
            uint32 xst = 0;
            uint32 xet = (maxlen / 8);
            while (xst < xet)
            {
                switch (op)
                {
                    case BITOP_AND:
                    {
                        lres[xst] &= lp[xst];
                        break;
                    }
                    case BITOP_OR:
                    {
                        lres[xst] |= lp[xst];
                        break;
                    }
                    case BITOP_XOR:
                    {
                        lres[xst] ^= lp[xst];
                        break;
                    }
                    case BITOP_NOT:
                    {
                        lres[xst] = ~lres[xst];
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
                xst++;
            }
        }
        int64 count = popcount(metas[0].meta.str_value.value.sv, maxlen);
        if (NULL != targetkey)
        {
            ValueObject meta;
            meta.key.type = KEY_META;
            meta.key.key = *targetkey;
            meta.key.db = ctx.currentDB;
            meta.type = STRING_META;
            meta.meta.str_value = metas[0].meta.str_value;
            SetKeyValue(ctx, meta);
        }
        fill_int_reply(ctx.reply, count);
        return 0;
    }

    int Ardb::BitOP(Context& ctx, const std::string& opname, const SliceArray& keys, const std::string* targetkey)
    {
        /* Parse the operation name. */
        uint32 op;
        if ((opname[0] == 'a' || opname[0] == 'A') && !strcasecmp(opname.c_str(), "and"))
            op = BITOP_AND;
        else if ((opname[0] == 'o' || opname[0] == 'O') && !strcasecmp(opname.c_str(), "or"))
            op = BITOP_OR;
        else if ((opname[0] == 'x' || opname[0] == 'X') && !strcasecmp(opname.c_str(), "xor"))
            op = BITOP_XOR;
        else if ((opname[0] == 'n' || opname[0] == 'N') && !strcasecmp(opname.c_str(), "not"))
            op = BITOP_NOT;
        else
        {
            fill_error_reply(ctx.reply, "Syntax error");
            return 0;
        }
        /* Sanity check: NOT accepts only a single key argument. */
        if (op == BITOP_NOT && keys.size() != 1)
        {
            fill_error_reply(ctx.reply, "BITOP NOT must be called with a single source key.");
            return 0;
        }

        /* Lookup keys, and store pointers to the string objects into an array. */
        int64 start_index = 0, end_index = 0;
        ValueObjectArray metas;
        metas.resize(keys.size());
        uint8 all_type = KEY_END;
        for (uint32 j = 0; j < keys.size(); j++)
        {
            int err = GetMetaValue(ctx, keys[j], KEY_END, metas[j]);
            if (0 == err && metas[j].type != BITSET_META && metas[j].type != STRING_META)
            {
                err = ERR_INVALID_TYPE;
            }
            CHECK_ARDB_RETURN_VALUE(ctx.reply, err);
            if (0 != err)
            {
                if (op == BITOP_AND)
                {
                    return 0;
                }
            }
            else
            {
                if (all_type == KEY_END)
                {
                    all_type = metas[j].type;
                }
                else if (all_type != metas[j].type)
                {
                    fill_error_reply(ctx.reply, "Operation against a key holding the wrong kind of value.");
                    return 0;
                }
            }
            if (all_type == BITSET_META)
            {
                if (start_index == 0 || start_index > metas[j].meta.min_index.value.iv)
                {
                    start_index = metas[j].meta.min_index.value.iv;
                }
                if (end_index == 0 || end_index > metas[j].meta.max_index.value.iv)
                {
                    end_index = metas[j].meta.max_index.value.iv;
                }
            }
        }
        if (NULL != targetkey)
        {
            DeleteKey(ctx, *targetkey);
        }
        if (start_index > end_index)
        {
            return 0;
        }
        BatchWriteGuard guard(ctx, targetkey != NULL);
        if (all_type == STRING_META)
        {
            StringBitSetOP(ctx, op, metas, targetkey);
            return 0;
        }
        int64 total_count = 0;
        int64 min_idx = -1;
        int64 max_idx = -1;
        for (int64 i = start_index; i <= end_index; i++)
        {
            StringArray strs;
            uint32 maxlen = 0;
            for (uint32 j = 0; j < metas.size(); j++)
            {
                KeyObject k;
                k.type = BITSET_ELEMENT;
                k.db = ctx.currentDB;
                k.key = metas[j].key.key;
                k.score.SetInt64(i);
                ValueObject bitvalue;
                std::string tmp;
                if (0 == GetKeyValue(ctx, k, &bitvalue))
                {
                    bitvalue.element.GetDecodeString(tmp);
                }
                else
                {
                    if (op == BITOP_AND)
                    {
                        break;
                    }
                }

                if (op == BITOP_AND)
                {
                    if (tmp.size() < maxlen || maxlen == 0)
                    {
                        maxlen = tmp.size();
                    }
                }
                else
                {
                    if (tmp.size() > maxlen)
                    {
                        maxlen = tmp.size();
                    }
                }
                strs.push_back(tmp);
            }
            if (strs.size() != metas.size())
            {
                continue;
            }
            if (-1 == min_idx)
            {
                min_idx = i;
            }
            max_idx = i;
            std::string res = strs[0];
            while (maxlen % 8 != 0)
            {
                maxlen++;
            }
            res.resize(maxlen);
            uint64* lres = (uint64*) &(res[0]);
            uint32 k = 1;
            if (op == BITOP_NOT)
            {
                k = 0;
            }
            for (; k < strs.size(); k++)
            {
                strs[k].resize(maxlen);

                const uint64* lp = (const uint64*) (strs[k].data());
                uint32 xst = 0;
                uint32 xet = (maxlen / 8);
                while (xst < xet)
                {
                    switch (op)
                    {
                        case BITOP_AND:
                        {
                            lres[xst] &= lp[xst];
                            break;
                        }
                        case BITOP_OR:
                        {
                            lres[xst] |= lp[xst];
                            break;
                        }
                        case BITOP_XOR:
                        {
                            lres[xst] ^= lp[xst];
                            break;
                        }
                        case BITOP_NOT:
                        {
                            lres[xst] = ~lres[xst];
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    xst++;
                }
            }
            int64 count = popcount(res.data(), res.size());
            total_count += count;
            if (NULL != targetkey)
            {
                ValueObject bitvalue;
                bitvalue.key.type = BITSET_ELEMENT;
                bitvalue.key.db = ctx.currentDB;
                bitvalue.key.key = *targetkey;
                bitvalue.key.score.SetInt64(i);
                bitvalue.type = BITSET_ELEMENT;
                bitvalue.element.SetString(res, false);
                bitvalue.score.SetInt64(count);
                SetKeyValue(ctx, bitvalue);
            }
        }
        if (NULL != targetkey)
        {
            ValueObject meta;
            meta.key.type = KEY_META;
            meta.key.key = *targetkey;
            meta.key.db = ctx.currentDB;
            meta.type = BITSET_META;
            meta.meta.min_index.SetInt64(min_idx);
            meta.meta.max_index.SetInt64(max_idx);
            meta.meta.len = total_count;
            SetKeyValue(ctx, meta);
        }
        fill_int_reply(ctx.reply, total_count);
        return 0;
    }

    int Ardb::BitopCount(Context& ctx, RedisCommandFrame& cmd)
    {
        SliceArray keys;
        for (uint32 i = 1; i < cmd.GetArguments().size(); i++)
        {
            keys.push_back(cmd.GetArguments()[i]);
        }
        BitOP(ctx, cmd.GetArguments()[0], keys, NULL);
        return 0;
    }

    int Ardb::Bitop(Context& ctx, RedisCommandFrame& cmd)
    {
        SliceArray keys;
        for (uint32 i = 2; i < cmd.GetArguments().size(); i++)
        {
            keys.push_back(cmd.GetArguments()[i]);
        }
        BitOP(ctx, cmd.GetArguments()[0], keys, &(cmd.GetArguments()[1]));
        return 0;
    }

    int Ardb::Bitcount(Context& ctx, RedisCommandFrame& cmd)
    {
        if (cmd.GetArguments().size() == 2)
        {
            fill_error_reply(ctx.reply, "syntax error");
            return 0;
        }
        ValueObject meta;
        int err = GetMetaValue(ctx, cmd.GetArguments()[0], KEY_END, meta);
        if (err == 0 && meta.type != BITSET_META && meta.type != STRING_META)
        {
            err = ERR_INVALID_TYPE;
        }
        CHECK_ARDB_RETURN_VALUE(ctx.reply, err);
        fill_int_reply(ctx.reply, 0);  //default value
        if (0 != err)
        {
            return 0;
        }
        int count = 0;
        int64 start, end;
        if (cmd.GetArguments().size() == 1)
        {
            start = 0;
            end = -1;
        }
        else
        {
            if (!GetInt64Value(ctx, cmd.GetArguments()[1], start) || !GetInt64Value(ctx, cmd.GetArguments()[2], end))
            {
                return 0;
            }
        }

        uint64 total_len = 0;
        if (meta.type == STRING_META)
        {
            meta.meta.str_value.ToString();
            total_len = meta.meta.str_value.StringLength();
        }
        else
        {
            total_len = meta.meta.max_index.value.iv * BIT_SUBSET_BYTES_SIZE;
        }
        if (start < 0)
            start = total_len + start;
        if (end < 0)
            end = total_len + end;
        if (start < 0)
            start = 0;
        if (end < 0)
            end = 0;
        if (end >= total_len)
            end = total_len - 1;

        if (start == 0 && end >= (int64) (total_len))
        {
            fill_int_reply(ctx.reply, meta.meta.len);
            return 0;
        }

        if (start > end)
        {
            return 0;
        }

        if(meta.type == STRING_META)
        {
            count = popcount(meta.meta.str_value.value.sv + start, end - start + 1);
            fill_int_reply(ctx.reply, count);
            return 0;
        }

        uint64 startIndex = (start / BIT_SUBSET_BYTES_SIZE) + 1;
        uint64 endIndex = (end / BIT_SUBSET_BYTES_SIZE) + 1;
        uint32 so = start % BIT_SUBSET_BYTES_SIZE;
        uint32 eo = end % BIT_SUBSET_BYTES_SIZE;

        BitsetIterator iter;
        BitsetIter(ctx, meta, startIndex, iter);
        while (iter.Valid())
        {
            if (iter.Index() > startIndex && iter.Index() < endIndex)
            {
                count += iter.Count();
            }
            else
            {
                if (startIndex == endIndex)
                {
                    count += popcount_bitval(iter.Bits(), so, eo);
                    break;
                }
                else
                {
                    if (iter.Index() == startIndex)
                    {
                        count += popcount_bitval(iter.Bits(), so, -1);
                    }
                    else
                    {
                        count += popcount_bitval(iter.Bits(), 0, eo);
                    }
                }
            }
            if (iter.Index() >= endIndex)
            {
                break;
            }
            iter.Next();
        }

        fill_int_reply(ctx.reply, count);
        return 0;
    }

    int Ardb::RenameBitset(Context& ctx, DBID srcdb, const std::string& srckey, DBID dstdb, const std::string& dstkey)
    {
        Context tmpctx;
        tmpctx.currentDB = srcdb;
        ValueObject v;
        int err = GetMetaValue(tmpctx, srckey, BITSET_META, v);
        CHECK_ARDB_RETURN_VALUE(ctx.reply, err);
        if (0 != err)
        {
            fill_error_reply(ctx.reply, "no such key or some error");
            return 0;
        }
        BitsetIterator iter;
        Data from;
        BitsetIter(ctx, v, v.meta.min_index.value.iv, iter);
        tmpctx.currentDB = dstdb;
        ValueObject dstmeta;
        dstmeta.key.type = KEY_META;
        dstmeta.key.key = dstkey;
        dstmeta.type = BITSET_META;
        BatchWriteGuard guard(ctx);
        while (iter.Valid())
        {
            ValueObject bv;
            bv.key.type = BITSET_ELEMENT;
            bv.key.db = dstdb;
            bv.key.key = dstkey;
            bv.key.score.SetInt64(iter.Index());
            bv.score.SetInt64(iter.Count());
            bv.element.SetString(iter.Bits(), false);
            SetKeyValue(tmpctx, bv);
            iter.Next();
        }
        SetKeyValue(tmpctx, dstmeta);
        tmpctx.currentDB = srcdb;
        DeleteKey(tmpctx, srckey);
        ctx.data_change = true;
        return 0;
    }

OP_NAMESPACE_END

