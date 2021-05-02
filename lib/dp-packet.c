/*
 * Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2016 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>

#include "dp-packet.h"
#include "netdev-afxdp.h"
#include "netdev-dpdk.h"
#include "openvswitch/dynamic-string.h"
#include "util.h"

static void
dp_packet_init__(struct dp_packet *p, size_t allocated, enum dp_packet_source source)
{
    dp_packet_set_allocated(p, allocated);
    p->source = source;
    dp_packet_reset_offsets(p);
    pkt_metadata_init(&p->md, 0);
    dp_packet_reset_cutlen(p);
    dp_packet_reset_offload(p);
    /* Initialize implementation-specific fields of dp_packet. */
    dp_packet_init_specific(p);
    /* By default assume the packet type to be Ethernet. */
    p->packet_type = htonl(PT_ETH);
}

static void
dp_packet_use__(struct dp_packet *p, void *base, size_t allocated,
             enum dp_packet_source source)
{
    dp_packet_set_base(p, base);
    dp_packet_set_data(p, base);
    dp_packet_set_size(p, 0);

    dp_packet_init__(p, allocated, source);
}

/* Initializes 'p' as an empty dp_packet that contains the 'allocated' bytes of
 * memory starting at 'base'.  'base' should be the first byte of a region
 * obtained from malloc().  It will be freed (with free()) if 'p' is resized or
 * freed. */
void
dp_packet_use(struct dp_packet *p, void *base, size_t allocated)
{
    dp_packet_use__(p, base, allocated, DPBUF_MALLOC);
}

#if HAVE_AF_XDP
/* Initialize 'p' as an empty dp_packet that contains
 * memory starting at AF_XDP umem base.
 */
void
dp_packet_use_afxdp(struct dp_packet *p, void *data, size_t allocated,
                    size_t headroom)
{
    dp_packet_set_base(p, (char *)data - headroom);
    dp_packet_set_data(p, data);
    dp_packet_set_size(p, 0);

    dp_packet_init__(p, allocated, DPBUF_AFXDP);
}
#endif

/* Initializes 'p' as an empty dp_packet that contains the 'allocated' bytes of
 * memory starting at 'base'.  'base' should point to a buffer on the stack.
 * (Nothing actually relies on 'base' being allocated on the stack.  It could
 * be static or malloc()'d memory.  But stack space is the most common use
 * case.)
 *
 * 'base' should be appropriately aligned.  Using an array of uint32_t or
 * uint64_t for the buffer is a reasonable way to ensure appropriate alignment
 * for 32- or 64-bit data.
 *
 * An dp_packet operation that requires reallocating data will copy the provided
 * buffer into a malloc()'d buffer.  Thus, it is wise to call dp_packet_uninit()
 * on an dp_packet initialized by this function, so that if it expanded into the
 * heap, that memory is freed. */
void
dp_packet_use_stub(struct dp_packet *p, void *base, size_t allocated)
{
    dp_packet_use__(p, base, allocated, DPBUF_STUB);
}

/* Initializes 'p' as an dp_packet whose data starts at 'data' and continues for
 * 'size' bytes.  This is appropriate for an dp_packet that will be used to
 * inspect existing data, without moving it around or reallocating it, and
 * generally without modifying it at all.
 *
 * An dp_packet operation that requires reallocating data will assert-fail if this
 * function was used to initialize it. */
void
dp_packet_use_const(struct dp_packet *p, const void *data, size_t size)
{
    dp_packet_use__(p, CONST_CAST(void *, data), size, DPBUF_STACK);
    dp_packet_set_size(p, size);
}

/* Initializes 'p' as a DPDK dp-packet, which must have been allocated from a
 * DPDK memory pool. */
void
dp_packet_init_dpdk(struct dp_packet *p)
{
    p->source = DPBUF_DPDK;
}

/* Initializes 'p' as an empty dp_packet with an initial capacity of 'size'
 * bytes. */
void
dp_packet_init(struct dp_packet *p, size_t size)
{
    dp_packet_use(p, size ? xmalloc(size) : NULL, size);
}

/* Frees memory that 'p' points to. */
void
dp_packet_uninit(struct dp_packet *p)
{
    if (p) {
        if (p->source == DPBUF_MALLOC) {
            free(dp_packet_base(p));
        } else if (p->source == DPBUF_DPDK) {
#ifdef DPDK_NETDEV
            /* If this dp_packet was allocated by DPDK it must have been
             * created as a dp_packet */
            free_dpdk_buf((struct dp_packet*) p);
#endif
        } else if (p->source == DPBUF_AFXDP) {
            free_afxdp_buf(p);
        }
    }
}

/* Creates and returns a new dp_packet with an initial capacity of 'size'
 * bytes. */
struct dp_packet *
dp_packet_new(size_t size)
{
    struct dp_packet *p = xmalloc(sizeof *p);
    dp_packet_init(p, size);
    return p;
}

/* Creates and returns a new dp_packet with an initial capacity of 'size +
 * headroom' bytes, reserving the first 'headroom' bytes as headroom. */
struct dp_packet *
dp_packet_new_with_headroom(size_t size, size_t headroom)
{
    struct dp_packet *p = dp_packet_new(size + headroom);
    dp_packet_reserve(p, headroom);
    return p;
}

/* Creates and returns a new dp_packet that initially contains a copy of the
 * 'dp_packet_size(buffer)' bytes of data starting at 'buffer->data' with no headroom or
 * tailroom. */
struct dp_packet *
dp_packet_clone(const struct dp_packet *p)
{
    return dp_packet_clone_with_headroom(p, 0);
}

/* Creates and returns a new dp_packet whose data are copied from packet 'p'.
 * The returned dp_packet will additionally have 'headroom' bytes of
 * headroom. */
struct dp_packet *
dp_packet_clone_with_headroom(const struct dp_packet *p, size_t headroom)
{
    struct dp_packet *new_buffer;
    uint32_t mark;

    new_buffer = dp_packet_clone_data_with_headroom(dp_packet_data(p),
                                                    dp_packet_size(p),
                                                    headroom);
    /* Copy the following fields into the returned buffer: l2_pad_size,
     * l2_5_ofs, l3_ofs, l4_ofs, cutlen, packet_type and md. */
    memcpy(&new_buffer->l2_pad_size, &p->l2_pad_size,
            sizeof(struct dp_packet) -
            offsetof(struct dp_packet, l2_pad_size));

    *dp_packet_ol_flags_ptr(new_buffer) = *dp_packet_ol_flags_ptr(p);
    *dp_packet_ol_flags_ptr(new_buffer) &= DP_PACKET_OL_SUPPORTED_MASK;

    if (dp_packet_rss_valid(p)) {
        dp_packet_set_rss_hash(new_buffer, dp_packet_get_rss_hash(p));
    }
    if (dp_packet_has_flow_mark(p, &mark)) {
        dp_packet_set_flow_mark(new_buffer, mark);
    }

    return new_buffer;
}

/* Creates and returns a new dp_packet that initially contains a copy of the
 * 'size' bytes of data starting at 'data' with no headroom or tailroom. */
struct dp_packet *
dp_packet_clone_data(const void *data, size_t size)
{
    return dp_packet_clone_data_with_headroom(data, size, 0);
}

/* Creates and returns a new dp_packet that initially contains 'headroom' bytes of
 * headroom followed by a copy of the 'size' bytes of data starting at
 * 'data'. */
struct dp_packet *
dp_packet_clone_data_with_headroom(const void *data, size_t size, size_t headroom)
{
    struct dp_packet *p = dp_packet_new_with_headroom(size, headroom);
    dp_packet_put(p, data, size);
    return p;
}

static void
dp_packet_copy__(struct dp_packet *p, uint8_t *new_base,
              size_t new_headroom, size_t new_tailroom)
{
    const uint8_t *old_base = dp_packet_base(p);
    size_t old_headroom = dp_packet_headroom(p);
    size_t old_tailroom = dp_packet_tailroom(p);
    size_t copy_headroom = MIN(old_headroom, new_headroom);
    size_t copy_tailroom = MIN(old_tailroom, new_tailroom);

    memcpy(&new_base[new_headroom - copy_headroom],
           &old_base[old_headroom - copy_headroom],
           copy_headroom + dp_packet_size(p) + copy_tailroom);
}

/* Reallocates 'p' so that it has exactly 'new_headroom' and 'new_tailroom'
 * bytes of headroom and tailroom, respectively. */
void
dp_packet_resize(struct dp_packet *p, size_t new_headroom, size_t new_tailroom)
{
    void *new_base, *new_data;
    size_t new_allocated;

    new_allocated = new_headroom + dp_packet_size(p) + new_tailroom;

    switch (p->source) {
    case DPBUF_DPDK:
        OVS_NOT_REACHED();

    case DPBUF_MALLOC:
        if (new_headroom == dp_packet_headroom(p)) {
            new_base = xrealloc(dp_packet_base(p), new_allocated);
        } else {
            new_base = xmalloc(new_allocated);
            dp_packet_copy__(p, new_base, new_headroom, new_tailroom);
            free(dp_packet_base(p));
        }
        break;

    case DPBUF_STACK:
        OVS_NOT_REACHED();

    case DPBUF_AFXDP:
        OVS_NOT_REACHED();

    case DPBUF_STUB:
        p->source = DPBUF_MALLOC;
        new_base = xmalloc(new_allocated);
        dp_packet_copy__(p, new_base, new_headroom, new_tailroom);
        break;

    default:
        OVS_NOT_REACHED();
    }

    dp_packet_set_allocated(p, new_allocated);
    dp_packet_set_base(p, new_base);

    new_data = (char *) new_base + new_headroom;
    if (dp_packet_data(p) != new_data) {
        dp_packet_set_data(p, new_data);
    }
}

/* Ensures that 'p' has room for at least 'size' bytes at its tail end,
 * reallocating and copying its data if necessary.  Its headroom, if any, is
 * preserved. */
void
dp_packet_prealloc_tailroom(struct dp_packet *p, size_t size)
{
    if (size > dp_packet_tailroom(p)) {
        dp_packet_resize(p, dp_packet_headroom(p), MAX(size, 64));
    }
}

/* Ensures that 'p' has room for at least 'size' bytes at its head,
 * reallocating and copying its data if necessary.  Its tailroom, if any, is
 * preserved. */
void
dp_packet_prealloc_headroom(struct dp_packet *p, size_t size)
{
    if (size > dp_packet_headroom(p)) {
        dp_packet_resize(p, MAX(size, 64), dp_packet_tailroom(p));
    }
}

/* Shifts all of the data within the allocated space in 'p' by 'delta' bytes.
 * For example, a 'delta' of 1 would cause each byte of data to move one byte
 * forward (from address 'p' to 'p+1'), and a 'delta' of -1 would cause each
 * byte to move one byte backward (from 'p' to 'p-1'). */
void
dp_packet_shift(struct dp_packet *p, int delta)
{
    ovs_assert(delta > 0 ? delta <= dp_packet_tailroom(p)
               : delta < 0 ? -delta <= dp_packet_headroom(p)
               : true);

    if (delta != 0) {
        char *dst = (char *) dp_packet_data(p) + delta;
        memmove(dst, dp_packet_data(p), dp_packet_size(p));
        dp_packet_set_data(p, dst);
    }
}

/* Appends 'size' bytes of data to the tail end of 'p', reallocating and
 * copying its data if necessary.  Returns a pointer to the first byte of the
 * new data, which is left uninitialized. */
void *
dp_packet_put_uninit(struct dp_packet *p, size_t size)
{
    void *tail;
    dp_packet_prealloc_tailroom(p, size);
    tail = dp_packet_tail(p);
    dp_packet_set_size(p, dp_packet_size(p) + size);
    return tail;
}

/* Appends 'size' zeroed bytes to the tail end of 'p'.  Data in 'p' is
 * reallocated and copied if necessary.  Returns a pointer to the first byte of
 * the data's location in the dp_packet. */
void *
dp_packet_put_zeros(struct dp_packet *p, size_t size)
{
    void *dst = dp_packet_put_uninit(p, size);
    memset(dst, 0, size);
    return dst;
}

/* Appends the 'size' bytes of data in 'data' to the tail end of 'p'.
 * Data in 'data' is reallocated and copied if necessary.  Returns
 * a pointer to the first byte of the data's location in the dp_packet. */
void *
dp_packet_put(struct dp_packet *p, const void *data, size_t size)
{
    void *dst = dp_packet_put_uninit(p, size);
    memcpy(dst, data, size);
    return dst;
}

/* Parses as many pairs of hex digits as possible (possibly separated by
 * spaces) from the beginning of 's', appending bytes for their values to 'p'.
 * Returns the first character of 's' that is not the first of a pair of hex
 * digits.  If 'n' is nonnull, stores the number of bytes added to 'p' in
 * '*n'. */
char *
dp_packet_put_hex(struct dp_packet *p, const char *s, size_t *n)
{
    size_t initial_size = dp_packet_size(p);
    for (;;) {
        uint8_t byte;
        bool ok;

        s += strspn(s, " \t\r\n");
        byte = hexits_value(s, 2, &ok);
        if (!ok) {
            if (n) {
                *n = dp_packet_size(p) - initial_size;
            }
            return CONST_CAST(char *, s);
        }

        dp_packet_put(p, &byte, 1);
        s += 2;
    }
}

/* Reserves 'size' bytes of headroom so that they can be later allocated with
 * dp_packet_push_uninit() without reallocating the dp_packet. */
void
dp_packet_reserve(struct dp_packet *p, size_t size)
{
    ovs_assert(!dp_packet_size(p));
    dp_packet_prealloc_tailroom(p, size);
    dp_packet_set_data(p, (char*)dp_packet_data(p) + size);
}

/* Reserves 'headroom' bytes at the head and 'tailroom' at the end so that
 * they can be later allocated with dp_packet_push_uninit() or
 * dp_packet_put_uninit() without reallocating the dp_packet. */
void
dp_packet_reserve_with_tailroom(struct dp_packet *p, size_t headroom,
                             size_t tailroom)
{
    ovs_assert(!dp_packet_size(p));
    dp_packet_prealloc_tailroom(p, headroom + tailroom);
    dp_packet_set_data(p, (char*)dp_packet_data(p) + headroom);
}

/* Prefixes 'size' bytes to the head end of 'p', reallocating and copying its
 * data if necessary.  Returns a pointer to the first byte of the data's
 * location in the dp_packet.  The new data is left uninitialized. */
void *
dp_packet_push_uninit(struct dp_packet *p, size_t size)
{
    dp_packet_prealloc_headroom(p, size);
    dp_packet_set_data(p, (char*)dp_packet_data(p) - size);
    dp_packet_set_size(p, dp_packet_size(p) + size);
    return dp_packet_data(p);
}

/* Prefixes 'size' zeroed bytes to the head end of 'p', reallocating and
 * copying its data if necessary.  Returns a pointer to the first byte of the
 * data's location in the dp_packet. */
void *
dp_packet_push_zeros(struct dp_packet *p, size_t size)
{
    void *dst = dp_packet_push_uninit(p, size);
    memset(dst, 0, size);
    return dst;
}

/* Copies the 'size' bytes starting at 'data' to the head end of 'p',
 * reallocating and copying its data if necessary.  Returns a pointer
 * to the first byte of the data's location in the dp_packet. */
void *
dp_packet_push(struct dp_packet *p, const void *data, size_t size)
{
    void *dst = dp_packet_push_uninit(p, size);
    memcpy(dst, data, size);
    return dst;
}

/* Returns the data in 'p' as a block of malloc()'d memory and frees the buffer
 * within 'p'.  (If 'p' itself was dynamically allocated, e.g. with
 * dp_packet_new(), then it should still be freed with, e.g., dp_packet_delete().) */
void *
dp_packet_steal_data(struct dp_packet *p)
{
    void *data;
    ovs_assert(p->source != DPBUF_DPDK);
    ovs_assert(p->source != DPBUF_AFXDP);

    if (p->source == DPBUF_MALLOC && dp_packet_data(p) == dp_packet_base(p)) {
        data = dp_packet_data(p);
    } else {
        data = xmemdup(dp_packet_data(p), dp_packet_size(p));
        if (p->source == DPBUF_MALLOC) {
            free(dp_packet_base(p));
        }
    }
    dp_packet_set_base(p, NULL);
    dp_packet_set_data(p, NULL);
    return data;
}

static inline void
dp_packet_adjust_layer_offset(uint16_t *offset, int increment)
{
    if (*offset != UINT16_MAX) {
        *offset += increment;
    }
}

/* Adjust the size of the l2_5 portion of the dp_packet, updating the l2
 * pointer and the layer offsets.  The caller is responsible for
 * modifying the contents. */
void *
dp_packet_resize_l2_5(struct dp_packet *p, int increment)
{
    if (increment >= 0) {
        dp_packet_push_uninit(p, increment);
    } else {
        dp_packet_pull(p, -increment);
    }

    /* Adjust layer offsets after l2_5. */
    dp_packet_adjust_layer_offset(&p->l3_ofs, increment);
    dp_packet_adjust_layer_offset(&p->l4_ofs, increment);

    return dp_packet_data(p);
}

/* Adjust the size of the l2 portion of the dp_packet, updating the l2
 * pointer and the layer offsets.  The caller is responsible for
 * modifying the contents. */
void *
dp_packet_resize_l2(struct dp_packet *p, int increment)
{
    dp_packet_resize_l2_5(p, increment);
    dp_packet_adjust_layer_offset(&p->l2_5_ofs, increment);
    return dp_packet_data(p);
}
