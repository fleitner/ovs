/*
 * Copyright (c) 2017 Red Hat Inc.
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

#ifndef NETNS_H
#define NETNS_H 1

#include <stdbool.h>

#ifdef HAVE_LINUX_NET_NAMESPACE_H
#include <linux/net_namespace.h>
#define NETNS_NOT_ASSIGNED NETNSA_NSID_NOT_ASSIGNED
#else
#define NETNS_NOT_ASSIGNED -1
#endif

enum netns_state {
    NETNS_INVALID,      /* not initialized yet */
    NETNS_LOCAL,        /* local or not supported on older kernels */
    NETNS_REMOTE        /* on another network namespace with valid ID */
};

struct netns {
    enum netns_state state;
    int id;
};

/* Prototypes */
static inline void netns_set_id(struct netns *ns, int id);
static inline void netns_set_invalid(struct netns *ns);
static inline bool netns_is_invalid(struct netns *ns);
static inline void netns_set_local(struct netns *ns);
static inline bool netns_is_local(struct netns *ns);
static inline bool netns_is_remote(struct netns *ns);
static inline bool netns_eq(const struct netns *a, const struct netns *b);
static inline void netns_copy(struct netns *dst, const struct netns *src);

/* Functions */
static inline void
netns_set_id(struct netns *ns, int id)
{
    if (!ns) {
        return;
    }

    if (id == NETNS_NOT_ASSIGNED) {
        ns->state = NETNS_LOCAL;
    } else {
        ns->state = NETNS_REMOTE;
        ns->id = id;
    }
}

static inline void
netns_set_invalid(struct netns *ns)
{
    ns->state = NETNS_INVALID;
}

static inline bool
netns_is_invalid(struct netns *ns)
{
    return ns->state == NETNS_INVALID;
}

static inline void
netns_set_local(struct netns *ns)
{
    ns->state = NETNS_LOCAL;
}

static inline bool
netns_is_local(struct netns *ns)
{
    return (ns->state == NETNS_LOCAL);
}

static inline bool
netns_is_remote(struct netns *ns)
{
    return (ns->state == NETNS_REMOTE);
}

static inline void
netns_copy(struct netns *dst, const struct netns *src)
{
    if (src->state == NETNS_LOCAL || src->state == NETNS_REMOTE) {
        *dst = *src;
    }
}

static inline bool
netns_eq(const struct netns *a, const struct netns *b)
{
    if (a->state == NETNS_LOCAL && b->state == NETNS_LOCAL) {
        return true;
    }

    if (a->state == NETNS_REMOTE && b->state == NETNS_REMOTE &&
        a->id == b->id) {
        return true;
    }

    return false;
}

#endif
