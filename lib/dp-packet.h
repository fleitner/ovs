/*
 * Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
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

#ifndef DPBUF_H
#define DPBUF_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef DPDK_NETDEV
#include <rte_config.h>
#include <rte_mbuf.h>
#endif

#include "netdev-afxdp.h"
#include "netdev-dpdk.h"
#include "openvswitch/list.h"
#include "packets.h"
#include "util.h"
#include "flow.h"

#ifdef  __cplusplus
extern "C" {
#endif

enum OVS_PACKED_ENUM dp_packet_source {
    DPBUF_MALLOC,              /* Obtained via malloc(). */
    DPBUF_STACK,               /* Un-movable stack space or static buffer. */
    DPBUF_STUB,                /* Starts on stack, may expand into heap. */
    DPBUF_DPDK,                /* buffer data is from DPDK allocated memory.
                                * ref to dp_packet_init_dpdk() in dp-packet.c.
                                */
    DPBUF_AFXDP,               /* Buffer data from XDP frame. */
};

#define DP_PACKET_CONTEXT_SIZE 64

#ifdef DPDK_NETDEV
#define DEF_OL_FLAG(NAME, DPDK_DEF, GENERIC_DEF) NAME = DPDK_DEF
#else
#define DEF_OL_FLAG(NAME, DPDK_DEF, GENERIC_DEF) NAME = GENERIC_DEF
#endif

/* Bit masks for the 'ol_flags' member of the 'dp_packet' structure. */
enum dp_packet_offload_mask {
    /* Value 0 is not used. */
    /* Is the 'rss_hash' valid? */
    DEF_OL_FLAG(DP_PACKET_OL_RSS_HASH, PKT_RX_RSS_HASH, 0x1),
    /* Is the 'flow_mark' valid? */
    DEF_OL_FLAG(DP_PACKET_OL_FLOW_MARK, PKT_RX_FDIR_ID, 0x2),
    /* Bad L4 checksum in the packet. */
    DEF_OL_FLAG(DP_PACKET_OL_RX_L4_CSUM_BAD, PKT_RX_L4_CKSUM_BAD, 0x4),
    /* Bad IP checksum in the packet. */
    DEF_OL_FLAG(DP_PACKET_OL_RX_IP_CSUM_BAD, PKT_RX_IP_CKSUM_BAD, 0x8),
    /* Valid L4 checksum in the packet. */
    DEF_OL_FLAG(DP_PACKET_OL_RX_L4_CSUM_GOOD, PKT_RX_L4_CKSUM_GOOD, 0x10),
    /* Valid IP checksum in the packet. */
    DEF_OL_FLAG(DP_PACKET_OL_RX_IP_CSUM_GOOD, PKT_RX_IP_CKSUM_GOOD, 0x20),
    /* TCP Segmentation Offload. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_TCP_SEG, PKT_TX_TCP_SEG, 0x40),
    /* Offloaded packet is IPv4. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_IPV4, PKT_TX_IPV4, 0x80),
    /* Offloaded packet is IPv6. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_IPV6, PKT_TX_IPV6, 0x100),
    /* Offload TCP checksum. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_TCP_CSUM, PKT_TX_TCP_CKSUM, 0x200),
    /* Offload UDP checksum. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_UDP_CSUM, PKT_TX_UDP_CKSUM, 0x400),
    /* Offload SCTP checksum. */
    DEF_OL_FLAG(DP_PACKET_OL_TX_SCTP_CSUM, PKT_TX_SCTP_CKSUM, 0x800),
    /* Adding new field requires adding to DP_PACKET_OL_SUPPORTED_MASK. */
};

#define DP_PACKET_OL_SUPPORTED_MASK (DP_PACKET_OL_RSS_HASH         | \
                                     DP_PACKET_OL_FLOW_MARK        | \
                                     DP_PACKET_OL_RX_L4_CSUM_BAD  | \
                                     DP_PACKET_OL_RX_IP_CSUM_BAD  | \
                                     DP_PACKET_OL_RX_L4_CSUM_GOOD | \
                                     DP_PACKET_OL_RX_IP_CSUM_GOOD | \
                                     DP_PACKET_OL_TX_TCP_SEG       | \
                                     DP_PACKET_OL_TX_IPV4          | \
                                     DP_PACKET_OL_TX_IPV6          | \
                                     DP_PACKET_OL_TX_TCP_CSUM     | \
                                     DP_PACKET_OL_TX_UDP_CSUM     | \
                                     DP_PACKET_OL_TX_SCTP_CSUM)

#define DP_PACKET_OL_TX_L4_MASK (DP_PACKET_OL_TX_TCP_CSUM | \
                                 DP_PACKET_OL_TX_UDP_CSUM | \
                                 DP_PACKET_OL_TX_SCTP_CSUM)
#define DP_PACKET_OL_RX_IP_CSUM_MASK (DP_PACKET_OL_RX_IP_CSUM_GOOD | \
                                       DP_PACKET_OL_RX_IP_CSUM_BAD)
#define DP_PACKET_OL_RX_L4_CSUM_MASK (DP_PACKET_OL_RX_L4_CSUM_GOOD | \
                                       DP_PACKET_OL_RX_L4_CSUM_BAD)

/* Buffer for holding packet data.  A dp_packet is automatically reallocated
 * as necessary if it grows too large for the available memory.
 * By default the packet type is set to Ethernet (PT_ETH).
 */
struct dp_packet {
#ifdef DPDK_NETDEV
    struct rte_mbuf mbuf;       /* DPDK mbuf */
#else
    void *base_;                /* First byte of allocated space. */
    uint16_t allocated_;        /* Number of bytes allocated. */
    uint16_t data_ofs;          /* First byte actually in use. */
    uint32_t size_;             /* Number of bytes in use. */
    uint32_t ol_flags;          /* Offloading flags. */
    uint32_t rss_hash;          /* Packet hash. */
    uint32_t flow_mark;         /* Packet flow mark. */
#endif
    enum dp_packet_source source;  /* Source of memory allocated as 'base'. */

    /* All the following elements of this struct are copied in a single call
     * of memcpy in dp_packet_clone_with_headroom. */
    uint16_t l2_pad_size;          /* Detected l2 padding size.
                                    * Padding is non-pullable. */
    uint16_t l2_5_ofs;             /* MPLS label stack offset, or UINT16_MAX */
    uint16_t l3_ofs;               /* Network-level header offset,
                                    * or UINT16_MAX. */
    uint16_t l4_ofs;               /* Transport-level header offset,
                                      or UINT16_MAX. */
    uint32_t cutlen;               /* length in bytes to cut from the end. */
    ovs_be32 packet_type;          /* Packet type as defined in OpenFlow */
    union {
        struct pkt_metadata md;
        uint64_t data[DP_PACKET_CONTEXT_SIZE / 8];
    };
};

#if HAVE_AF_XDP
struct dp_packet_afxdp {
    struct umem_pool *mpool;
    struct dp_packet packet;
};
#endif

static inline void *dp_packet_data(const struct dp_packet *);
static inline void dp_packet_set_data(struct dp_packet *, void *);
static inline void *dp_packet_base(const struct dp_packet *);
static inline void dp_packet_set_base(struct dp_packet *, void *);

static inline uint32_t dp_packet_size(const struct dp_packet *);
static inline void dp_packet_set_size(struct dp_packet *, uint32_t);

static inline uint16_t dp_packet_get_allocated(const struct dp_packet *);
static inline void dp_packet_set_allocated(struct dp_packet *, uint16_t);

void *dp_packet_resize_l2(struct dp_packet *, int increment);
void *dp_packet_resize_l2_5(struct dp_packet *, int increment);
static inline void *dp_packet_eth(const struct dp_packet *);
static inline void dp_packet_reset_offsets(struct dp_packet *);
static inline uint16_t dp_packet_l2_pad_size(const struct dp_packet *);
static inline void dp_packet_set_l2_pad_size(struct dp_packet *, uint16_t);
static inline void *dp_packet_l2_5(const struct dp_packet *);
static inline void dp_packet_set_l2_5(struct dp_packet *, void *);
static inline void *dp_packet_l3(const struct dp_packet *);
static inline void dp_packet_set_l3(struct dp_packet *, void *);
static inline void *dp_packet_l4(const struct dp_packet *);
static inline void dp_packet_set_l4(struct dp_packet *, void *);
static inline size_t dp_packet_l4_size(const struct dp_packet *);
static inline const void *dp_packet_get_tcp_payload(const struct dp_packet *);
static inline const void *dp_packet_get_udp_payload(const struct dp_packet *);
static inline const void *dp_packet_get_sctp_payload(const struct dp_packet *);
static inline const void *dp_packet_get_icmp_payload(const struct dp_packet *);
static inline const void *dp_packet_get_nd_payload(const struct dp_packet *);

void dp_packet_use(struct dp_packet *, void *, size_t);
void dp_packet_use_stub(struct dp_packet *, void *, size_t);
void dp_packet_use_const(struct dp_packet *, const void *, size_t);
#if HAVE_AF_XDP
void dp_packet_use_afxdp(struct dp_packet *, void *, size_t, size_t);
#endif
void dp_packet_init_dpdk(struct dp_packet *);

void dp_packet_init(struct dp_packet *, size_t);
void dp_packet_uninit(struct dp_packet *);

struct dp_packet *dp_packet_new(size_t);
struct dp_packet *dp_packet_new_with_headroom(size_t, size_t headroom);
struct dp_packet *dp_packet_clone(const struct dp_packet *);
struct dp_packet *dp_packet_clone_with_headroom(const struct dp_packet *,
                                                size_t headroom);
struct dp_packet *dp_packet_clone_data(const void *, size_t);
struct dp_packet *dp_packet_clone_data_with_headroom(const void *, size_t,
                                                     size_t headroom);
void dp_packet_resize(struct dp_packet *p, size_t new_headroom,
                      size_t new_tailroom);
static inline void dp_packet_delete(struct dp_packet *);

static inline void *dp_packet_at(const struct dp_packet *, size_t offset,
                                 size_t size);
static inline void *dp_packet_at_assert(const struct dp_packet *,
                                        size_t offset, size_t size);
static inline void *dp_packet_tail(const struct dp_packet *);
static inline void *dp_packet_end(const struct dp_packet *);

void *dp_packet_put_uninit(struct dp_packet *, size_t);
void *dp_packet_put_zeros(struct dp_packet *, size_t);
void *dp_packet_put(struct dp_packet *, const void *, size_t);
char *dp_packet_put_hex(struct dp_packet *, const char *s, size_t *n);
void dp_packet_reserve(struct dp_packet *, size_t);
void dp_packet_reserve_with_tailroom(struct dp_packet *, size_t headroom,
                                     size_t tailroom);
void *dp_packet_push_uninit(struct dp_packet *, size_t);
void *dp_packet_push_zeros(struct dp_packet *, size_t);
void *dp_packet_push(struct dp_packet *, const void *, size_t);

static inline size_t dp_packet_headroom(const struct dp_packet *);
static inline size_t dp_packet_tailroom(const struct dp_packet *);
void dp_packet_prealloc_headroom(struct dp_packet *, size_t);
void dp_packet_prealloc_tailroom(struct dp_packet *, size_t);
void dp_packet_shift(struct dp_packet *, int);

static inline void dp_packet_clear(struct dp_packet *);
static inline void *dp_packet_pull(struct dp_packet *, size_t);
static inline void *dp_packet_try_pull(struct dp_packet *, size_t);

void *dp_packet_steal_data(struct dp_packet *);

static inline bool dp_packet_equal(const struct dp_packet *,
                                   const struct dp_packet *);


/* Frees memory that 'p' points to, as well as 'p' itself. */
static inline void
dp_packet_delete(struct dp_packet *p)
{
    if (p) {
        if (p->source == DPBUF_DPDK) {
            /* If this dp_packet was allocated by DPDK it must have been
             * created as a dp_packet */
            free_dpdk_buf((struct dp_packet*) p);
            return;
        }

        if (p->source == DPBUF_AFXDP) {
            free_afxdp_buf(p);
            return;
        }

        dp_packet_uninit(p);
        free(p);
    }
}

/* If 'p' contains at least 'offset + size' bytes of data, returns a pointer to
 * byte 'offset'.  Otherwise, returns a null pointer. */
static inline void *
dp_packet_at(const struct dp_packet *p, size_t offset, size_t size)
{
    return offset + size <= dp_packet_size(p)
           ? (char *) dp_packet_data(p) + offset
           : NULL;
}

/* Returns a pointer to byte 'offset' in 'p', which must contain at least
 * 'offset + size' bytes of data. */
static inline void *
dp_packet_at_assert(const struct dp_packet *p, size_t offset, size_t size)
{
    ovs_assert(offset + size <= dp_packet_size(p));
    return ((char *) dp_packet_data(p)) + offset;
}

/* Returns a pointer to byte following the last byte of data in use in 'p'. */
static inline void *
dp_packet_tail(const struct dp_packet *p)
{
    return (char *) dp_packet_data(p) + dp_packet_size(p);
}

/* Returns a pointer to byte following the last byte allocated for use (but
 * not necessarily in use) in 'p'. */
static inline void *
dp_packet_end(const struct dp_packet *p)
{
    return (char *) dp_packet_base(p) + dp_packet_get_allocated(p);
}

/* Returns the number of bytes of headroom in 'p', that is, the number of bytes
 * of unused space in dp_packet 'p' before the data that is in use.  (Most
 * commonly, the data in a dp_packet is at its beginning, and thus the
 * dp_packet's headroom is 0.) */
static inline size_t
dp_packet_headroom(const struct dp_packet *p)
{
    return (char *) dp_packet_data(p) - (char *) dp_packet_base(p);
}

/* Returns the number of bytes that may be appended to the tail end of
 * dp_packet 'p' before the dp_packet must be reallocated. */
static inline size_t
dp_packet_tailroom(const struct dp_packet *p)
{
    return (char *) dp_packet_end(p) - (char *) dp_packet_tail(p);
}

/* Clears any data from 'p'. */
static inline void
dp_packet_clear(struct dp_packet *p)
{
    dp_packet_set_data(p, dp_packet_base(p));
    dp_packet_set_size(p, 0);
}

/* Removes 'size' bytes from the head end of 'p', which must contain at least
 * 'size' bytes of data.  Returns the first byte of data removed. */
static inline void *
dp_packet_pull(struct dp_packet *p, size_t size)
{
    void *data = dp_packet_data(p);
    ovs_assert(dp_packet_size(p) - dp_packet_l2_pad_size(p) >= size);
    dp_packet_set_data(p, (char *) dp_packet_data(p) + size);
    dp_packet_set_size(p, dp_packet_size(p) - size);
    return data;
}

/* If 'p' has at least 'size' bytes of data, removes that many bytes from the
 * head end of 'p' and returns the first byte removed.  Otherwise, returns a
 * null pointer without modifying 'p'. */
static inline void *
dp_packet_try_pull(struct dp_packet *p, size_t size)
{
    return dp_packet_size(p) - dp_packet_l2_pad_size(p) >= size
        ? dp_packet_pull(p, size) : NULL;
}

static inline bool
dp_packet_equal(const struct dp_packet *p1, const struct dp_packet *p2)
{
    return dp_packet_size(p1) == dp_packet_size(p2) &&
           !memcmp(dp_packet_data(p1), dp_packet_data(p2), dp_packet_size(p1));
}

static inline bool
dp_packet_is_eth(const struct dp_packet *p)
{
    return p->packet_type == htonl(PT_ETH);
}

/* Get the start of the Ethernet frame. 'l3_ofs' marks the end of the l2
 * headers, so return NULL if it is not set. */
static inline void *
dp_packet_eth(const struct dp_packet *p)
{
    return (dp_packet_is_eth(p) && p->l3_ofs != UINT16_MAX)
            ? dp_packet_data(p) : NULL;
}

/* Resets all layer offsets.  'l3' offset must be set before 'l2' can be
 * retrieved. */
static inline void
dp_packet_reset_offsets(struct dp_packet *p)
{
    p->l2_pad_size = 0;
    p->l2_5_ofs = UINT16_MAX;
    p->l3_ofs = UINT16_MAX;
    p->l4_ofs = UINT16_MAX;
}

static inline uint16_t
dp_packet_l2_pad_size(const struct dp_packet *p)
{
    return p->l2_pad_size;
}

static inline void
dp_packet_set_l2_pad_size(struct dp_packet *p, uint16_t pad_size)
{
    ovs_assert(pad_size <= dp_packet_size(p));
    p->l2_pad_size = pad_size;
}

static inline void *
dp_packet_l2_5(const struct dp_packet *p)
{
    return p->l2_5_ofs != UINT16_MAX
           ? (char *) dp_packet_data(p) + p->l2_5_ofs
           : NULL;
}

static inline void
dp_packet_set_l2_5(struct dp_packet *p, void *l2_5)
{
    p->l2_5_ofs = l2_5
                  ? (char *) l2_5 - (char *) dp_packet_data(p)
                  : UINT16_MAX;
}

static inline void *
dp_packet_l3(const struct dp_packet *p)
{
    return p->l3_ofs != UINT16_MAX
           ? (char *) dp_packet_data(p) + p->l3_ofs
           : NULL;
}

static inline void
dp_packet_set_l3(struct dp_packet *p, void *l3)
{
    p->l3_ofs = l3 ? (char *) l3 - (char *) dp_packet_data(p) : UINT16_MAX;
}

static inline void *
dp_packet_l4(const struct dp_packet *p)
{
    return p->l4_ofs != UINT16_MAX
           ? (char *) dp_packet_data(p) + p->l4_ofs
           : NULL;
}

static inline void
dp_packet_set_l4(struct dp_packet *p, void *l4)
{
    p->l4_ofs = l4 ? (char *) l4 - (char *) dp_packet_data(p) : UINT16_MAX;
}

/* Returns the size of the packet from the beginning of the L3 header to the
 * end of the L3 payload.  Hence L2 padding is not included. */
static inline size_t
dp_packet_l3_size(const struct dp_packet *p)
{
    return OVS_LIKELY(p->l3_ofs != UINT16_MAX)
        ? (const char *)dp_packet_tail(p) - (const char *)dp_packet_l3(p)
        - dp_packet_l2_pad_size(p)
        : 0;
}

/* Returns the size of the packet from the beginning of the L4 header to the
 * end of the L4 payload.  Hence L2 padding is not included. */
static inline size_t
dp_packet_l4_size(const struct dp_packet *p)
{
    return OVS_LIKELY(p->l4_ofs != UINT16_MAX)
        ? (const char *)dp_packet_tail(p) - (const char *)dp_packet_l4(p)
        - dp_packet_l2_pad_size(p)
        : 0;
}

static inline const void *
dp_packet_get_tcp_payload(const struct dp_packet *p)
{
    size_t l4_size = dp_packet_l4_size(p);

    if (OVS_LIKELY(l4_size >= TCP_HEADER_LEN)) {
        struct tcp_header *tcp = dp_packet_l4(p);
        int tcp_len = TCP_OFFSET(tcp->tcp_ctl) * 4;

        if (OVS_LIKELY(tcp_len >= TCP_HEADER_LEN && tcp_len <= l4_size)) {
            return (const char *)tcp + tcp_len;
        }
    }
    return NULL;
}

static inline uint32_t
dp_packet_get_tcp_payload_length(const struct dp_packet *p)
{
    const char *tcp_payload = dp_packet_get_tcp_payload(p);
    if (tcp_payload) {
        return ((char *) dp_packet_tail(p) - dp_packet_l2_pad_size(p)
                - tcp_payload);
    } else {
        return 0;
    }
}

static inline const void *
dp_packet_get_udp_payload(const struct dp_packet *p)
{
    return OVS_LIKELY(dp_packet_l4_size(p) >= UDP_HEADER_LEN)
        ? (const char *)dp_packet_l4(p) + UDP_HEADER_LEN : NULL;
}

static inline const void *
dp_packet_get_sctp_payload(const struct dp_packet *p)
{
    return OVS_LIKELY(dp_packet_l4_size(p) >= SCTP_HEADER_LEN)
        ? (const char *)dp_packet_l4(p) + SCTP_HEADER_LEN : NULL;
}

static inline const void *
dp_packet_get_icmp_payload(const struct dp_packet *p)
{
    return OVS_LIKELY(dp_packet_l4_size(p) >= ICMP_HEADER_LEN)
        ? (const char *)dp_packet_l4(p) + ICMP_HEADER_LEN : NULL;
}

static inline const void *
dp_packet_get_nd_payload(const struct dp_packet *p)
{
    return OVS_LIKELY(dp_packet_l4_size(p) >= ND_MSG_LEN)
        ? (const char *)dp_packet_l4(p) + ND_MSG_LEN : NULL;
}

#ifdef DPDK_NETDEV
static inline uint64_t *
dp_packet_ol_flags_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint64_t *, &p->mbuf.ol_flags);
}

static inline uint32_t *
dp_packet_rss_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint32_t *, &p->mbuf.hash.rss);
}

static inline uint32_t *
dp_packet_flow_mark_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint32_t *, &p->mbuf.hash.fdir.hi);
}

#else
static inline uint32_t *
dp_packet_ol_flags_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint32_t *, &p->ol_flags);
}

static inline uint32_t *
dp_packet_rss_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint32_t *, &p->rss_hash);
}

static inline uint32_t *
dp_packet_flow_mark_ptr(const struct dp_packet *p)
{
    return CONST_CAST(uint32_t *, &p->flow_mark);
}
#endif

#ifdef DPDK_NETDEV
BUILD_ASSERT_DECL(offsetof(struct dp_packet, mbuf) == 0);

static inline void
dp_packet_init_specific(struct dp_packet *p)
{
    /* This initialization is needed for packets that do not come from DPDK
     * interfaces, when vswitchd is built with --with-dpdk. */
    p->mbuf.ol_flags = p->mbuf.tx_offload = p->mbuf.packet_type = 0;
    p->mbuf.nb_segs = 1;
    p->mbuf.next = NULL;
}

static inline void *
dp_packet_base(const struct dp_packet *p)
{
    return p->mbuf.buf_addr;
}

static inline void
dp_packet_set_base(struct dp_packet *p, void *d)
{
    p->mbuf.buf_addr = d;
}

static inline uint32_t
dp_packet_size(const struct dp_packet *p)
{
    return p->mbuf.pkt_len;
}

static inline void
dp_packet_set_size(struct dp_packet *p, uint32_t v)
{
    /* netdev-dpdk does not currently support segmentation; consequently, for
     * all intents and purposes, 'data_len' (16 bit) and 'pkt_len' (32 bit) may
     * be used interchangably.
     *
     * On the datapath, it is expected that the size of packets
     * (and thus 'v') will always be <= UINT16_MAX; this means that there is no
     * loss of accuracy in assigning 'v' to 'data_len'.
     */
    p->mbuf.data_len = (uint16_t)v;  /* Current seg length. */
    p->mbuf.pkt_len = v;             /* Total length of all segments linked to
                                      * this segment. */
}

static inline uint16_t
__packet_data(const struct dp_packet *p)
{
    return p->mbuf.data_off;
}

static inline void
__packet_set_data(struct dp_packet *p, uint16_t v)
{
    p->mbuf.data_off = v;
}

static inline uint16_t
dp_packet_get_allocated(const struct dp_packet *p)
{
    return p->mbuf.buf_len;
}

static inline void
dp_packet_set_allocated(struct dp_packet *p, uint16_t s)
{
    p->mbuf.buf_len = s;
}

#else /* DPDK_NETDEV */

static inline void
dp_packet_init_specific(struct dp_packet *p OVS_UNUSED)
{
    /* There are no implementation-specific fields for initialization. */
}

static inline void *
dp_packet_base(const struct dp_packet *p)
{
    return p->base_;
}

static inline void
dp_packet_set_base(struct dp_packet *p, void *d)
{
    p->base_ = d;
}

static inline uint32_t
dp_packet_size(const struct dp_packet *p)
{
    return p->size_;
}

static inline void
dp_packet_set_size(struct dp_packet *p, uint32_t v)
{
    p->size_ = v;
}

static inline uint16_t
__packet_data(const struct dp_packet *p)
{
    return p->data_ofs;
}

static inline void
__packet_set_data(struct dp_packet *p, uint16_t v)
{
    p->data_ofs = v;
}

static inline uint16_t
dp_packet_get_allocated(const struct dp_packet *p)
{
    return p->allocated_;
}

static inline void
dp_packet_set_allocated(struct dp_packet *p, uint16_t s)
{
    p->allocated_ = s;
}

#endif /* DPDK_NETDEV */

static inline void
dp_packet_reset_cutlen(struct dp_packet *p)
{
    p->cutlen = 0;
}

static inline uint32_t
dp_packet_set_cutlen(struct dp_packet *p, uint32_t max_len)
{
    if (max_len < ETH_HEADER_LEN) {
        max_len = ETH_HEADER_LEN;
    }

    if (max_len >= dp_packet_size(p)) {
        p->cutlen = 0;
    } else {
        p->cutlen = dp_packet_size(p) - max_len;
    }
    return p->cutlen;
}

static inline uint32_t
dp_packet_get_cutlen(const struct dp_packet *p)
{
    /* Always in valid range if user uses dp_packet_set_cutlen. */
    return p->cutlen;
}

static inline uint32_t
dp_packet_get_send_len(const struct dp_packet *p)
{
    return dp_packet_size(p) - dp_packet_get_cutlen(p);
}

static inline void *
dp_packet_data(const struct dp_packet *p)
{
    return __packet_data(p) != UINT16_MAX
           ? (char *) dp_packet_base(p) + __packet_data(p) : NULL;
}

static inline void
dp_packet_set_data(struct dp_packet *p, void *data)
{
    if (data) {
        __packet_set_data(p, (char *) data - (char *) dp_packet_base(p));
    } else {
        __packet_set_data(p, UINT16_MAX);
    }
}

static inline void
dp_packet_reset_packet(struct dp_packet *p, int off)
{
    dp_packet_set_size(p, dp_packet_size(p) - off);
    dp_packet_set_data(p, ((unsigned char *) dp_packet_data(p) + off));
    dp_packet_reset_offsets(p);
}

enum { NETDEV_MAX_BURST = 32 }; /* Maximum number packets in a batch. */

struct dp_packet_batch {
    size_t count;
    bool trunc; /* true if the batch needs truncate. */
    bool do_not_steal; /* Indicate that the packets should not be stolen. */
    struct dp_packet *packets[NETDEV_MAX_BURST];
};

static inline void
dp_packet_batch_init(struct dp_packet_batch *b)
{
    b->count = 0;
    b->trunc = false;
    b->do_not_steal = false;
}

static inline void
dp_packet_batch_add__(struct dp_packet_batch *b,
                      struct dp_packet *packet, size_t limit)
{
    if (b->count < limit) {
        b->packets[b->count++] = packet;
    } else {
        dp_packet_delete(packet);
    }
}

/* When the batch 'b' is full, packet 'p' will be dropped and freed. */
static inline void
dp_packet_batch_add(struct dp_packet_batch *b, struct dp_packet *p)
{
    dp_packet_batch_add__(b, p, NETDEV_MAX_BURST);
}

static inline size_t
dp_packet_batch_size(const struct dp_packet_batch *b)
{
    return b->count;
}

/* Clear batch 'b' for refill. Use dp_packet_batch_refill() to add
 * packets back into the batch 'b'. */
static inline void
dp_packet_batch_refill_init(struct dp_packet_batch *b)
{
    b->count = 0;
};

static inline void
dp_packet_batch_refill(struct dp_packet_batch *b,
                       struct dp_packet *packet, size_t idx)
{
    dp_packet_batch_add__(b, packet, MIN(NETDEV_MAX_BURST, idx + 1));
}

static inline void
dp_packet_batch_init_packet(struct dp_packet_batch *b, struct dp_packet *p)
{
    dp_packet_batch_init(b);
    b->count = 1;
    b->packets[0] = p;
}

static inline bool
dp_packet_batch_is_empty(const struct dp_packet_batch *b)
{
    return !dp_packet_batch_size(b);
}

static inline bool
dp_packet_batch_is_full(const struct dp_packet_batch *b)
{
    return dp_packet_batch_size(b) == NETDEV_MAX_BURST;
}

#define DP_PACKET_BATCH_FOR_EACH(IDX, PACKET, BATCH)                \
    for (size_t IDX = 0; IDX < dp_packet_batch_size(BATCH); IDX++)  \
        if (PACKET = BATCH->packets[IDX], true)

/* Use this macro for cases where some packets in the 'BATCH' may be
 * dropped after going through each packet in the 'BATCH'.
 *
 * For packets to stay in the 'BATCH', they need to be refilled back
 * into the 'BATCH' by calling dp_packet_batch_refill(). Caller owns
 * the packets that are not refilled.
 *
 * Caller needs to supply 'SIZE', that stores the current number of
 * packets in 'BATCH'. It is best to declare this variable with
 * the 'const' modifier since it should not be modified by
 * the iterator.  */
#define DP_PACKET_BATCH_REFILL_FOR_EACH(IDX, SIZE, PACKET, BATCH)       \
    for (dp_packet_batch_refill_init(BATCH), IDX=0; IDX < SIZE; IDX++)  \
         if (PACKET = BATCH->packets[IDX], true)

static inline void
dp_packet_batch_clone(struct dp_packet_batch *dst,
                      struct dp_packet_batch *src)
{
    struct dp_packet *packet;

    dp_packet_batch_init(dst);
    DP_PACKET_BATCH_FOR_EACH (i, packet, src) {
        if (i + 1 < dp_packet_batch_size(src)) {
            OVS_PREFETCH(src->packets[i + 1]);
        }

        uint32_t headroom = dp_packet_headroom(packet);
        struct dp_packet *pkt_clone;

        pkt_clone  = dp_packet_clone_with_headroom(packet, headroom);
        dp_packet_batch_add(dst, pkt_clone);
    }
    dst->trunc = src->trunc;
}

static inline void
dp_packet_delete_batch(struct dp_packet_batch *b, bool should_steal)
{
    if (should_steal) {
        struct dp_packet *packet;

        DP_PACKET_BATCH_FOR_EACH (i, packet, b) {
            dp_packet_delete(packet);
        }
        dp_packet_batch_init(b);
    }
}

static inline void
dp_packet_batch_init_packet_fields(struct dp_packet_batch *b)
{
    struct dp_packet *packet;

    DP_PACKET_BATCH_FOR_EACH (i, packet, b) {
        dp_packet_reset_cutlen(packet);
        packet->packet_type = htonl(PT_ETH);
    }
}

static inline void
dp_packet_batch_apply_cutlen(struct dp_packet_batch *b)
{
    if (b->trunc) {
        struct dp_packet *packet;

        DP_PACKET_BATCH_FOR_EACH (i, packet, b) {
            dp_packet_set_size(packet, dp_packet_get_send_len(packet));
            dp_packet_reset_cutlen(packet);
        }
        b->trunc = false;
    }
}

static inline void
dp_packet_batch_reset_cutlen(struct dp_packet_batch *b)
{
    if (b->trunc) {
        struct dp_packet *packet;

        DP_PACKET_BATCH_FOR_EACH (i, packet, b) {
            dp_packet_reset_cutlen(packet);
        }
        b->trunc = false;
    }
}

/* Returns the RSS hash of the packet 'p'.  Note that the returned value is
 * correct only if 'dp_packet_rss_valid(p)' returns 'true'. */
static inline uint32_t
dp_packet_get_rss_hash(const struct dp_packet *p)
{
    return *dp_packet_rss_ptr(p);
}

static inline void
dp_packet_set_rss_hash(struct dp_packet *p, uint32_t hash)
{
    *dp_packet_rss_ptr(p) = hash;
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_RSS_HASH;
}

static inline bool
dp_packet_rss_valid(const struct dp_packet *p)
{
    return *dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_RSS_HASH;
}

static inline void
dp_packet_reset_offload(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) &= ~DP_PACKET_OL_SUPPORTED_MASK;
}

static inline bool
dp_packet_has_flow_mark(const struct dp_packet *p, uint32_t *mark)
{
    if (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_FLOW_MARK) {
        *mark = *dp_packet_flow_mark_ptr(p);
        return true;
    }

    return false;
}

static inline void
dp_packet_set_flow_mark(struct dp_packet *p, uint32_t mark)
{
    *dp_packet_flow_mark_ptr(p) = mark;
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_FLOW_MARK;
}

/* Returns the L4 cksum offload bitmask. */
static inline uint64_t
dp_packet_ol_l4_mask(const struct dp_packet *p)
{
    return *dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_L4_MASK;
}

/* Return true if the packet 'p' requested L4 checksum offload. */
static inline bool
dp_packet_ol_tx_l4_csum(const struct dp_packet *p)
{
    return !!dp_packet_ol_l4_mask(p);
}

/* Returns 'true' if packet 'p' is marked for TCP segmentation offloading. */
static inline bool
dp_packet_ol_tcp_seg(const struct dp_packet *p)
{
    return !!(*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_TCP_SEG);
}

/* Returns 'true' if packet 'p' is marked for IPv4 checksum offloading. */
static inline bool
dp_packet_ol_is_ipv4(const struct dp_packet *p)
{
    return !!(*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_IPV4);
}

/* Returns 'true' if packet 'p' is marked for TCP checksum offloading. */
static inline bool
dp_packet_ol_l4_is_tcp(const struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_L4_MASK) ==
            DP_PACKET_OL_TX_TCP_CSUM;
}

/* Returns 'true' if packet 'p' is marked for UDP checksum offloading. */
static inline bool
dp_packet_ol_l4_is_udp(struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_L4_MASK) ==
            DP_PACKET_OL_TX_UDP_CSUM;
}

/* Returns 'true' if packet 'p' is marked for SCTP checksum offloading. */
static inline bool
dp_packet_ol_l4_is_sctp(struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_TX_L4_MASK) ==
            DP_PACKET_OL_TX_SCTP_CSUM;
}

/* Mark packet 'p' for IPv4 checksum offloading. */
static inline void
dp_packet_ol_set_tx_ipv4(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_IPV4;
}

/* Mark packet 'p' for IPv6 checksum offloading. */
static inline void
dp_packet_ol_set_tx_ipv6(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_IPV6;
}

/* Mark packet 'p' for TCP checksum offloading.  It implies that either
 * the packet 'p' is marked for IPv4 or IPv6 checksum offloading. */
static inline void
dp_packet_ol_set_csum_tcp(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_TCP_CSUM;
}

/* Mark packet 'p' for UDP checksum offloading.  It implies that either
 * the packet 'p' is marked for IPv4 or IPv6 checksum offloading. */
static inline void
dp_packet_ol_set_csum_udp(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_UDP_CSUM;
}

/* Mark packet 'p' for SCTP checksum offloading.  It implies that either
 * the packet 'p' is marked for IPv4 or IPv6 checksum offloading. */
static inline void
dp_packet_ol_set_csum_sctp(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_SCTP_CSUM;
}

/* Mark packet 'p' for TCP segmentation offloading.  It implies that
 * either the packet 'p' is marked for IPv4 or IPv6 checksum offloading
 * and also for TCP checksum offloading. */
static inline void
dp_packet_ol_set_tcp_seg(struct dp_packet *p)
{
    *dp_packet_ol_flags_ptr(p) |= DP_PACKET_OL_TX_TCP_SEG;
}

static inline bool
dp_packet_ip_csum_good(const struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_RX_IP_CSUM_MASK) ==
            DP_PACKET_OL_RX_IP_CSUM_GOOD;
}

static inline bool
dp_packet_ip_csum_bad(const struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_RX_IP_CSUM_MASK) ==
            DP_PACKET_OL_RX_IP_CSUM_BAD;
}

static inline bool
dp_packet_l4_csum_good(const struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_RX_L4_CSUM_MASK) ==
            DP_PACKET_OL_RX_L4_CSUM_GOOD;
}

static inline bool
dp_packet_l4_csum_bad(const struct dp_packet *p)
{
    return (*dp_packet_ol_flags_ptr(p) & DP_PACKET_OL_RX_L4_CSUM_MASK) ==
            DP_PACKET_OL_RX_L4_CSUM_BAD;
}

#ifdef  __cplusplus
}
#endif

#endif /* dp-packet.h */
