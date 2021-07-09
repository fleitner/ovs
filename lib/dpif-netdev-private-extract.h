/*
 * Copyright (c) 2021 Intel.
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

#ifndef MFEX_AVX512_EXTRACT
#define MFEX_AVX512_EXTRACT 1

#include <sys/types.h>

/* Forward declarations. */
struct dp_packet;
struct miniflow;
struct dp_netdev_pmd_thread;
struct dp_packet_batch;
struct netdev_flow_key;

/* Function pointer prototype to be implemented in the optimized miniflow
 * extract code.
 * returns the hitmask of the processed packets on success.
 * returns zero on failure.
 */
typedef uint32_t (*miniflow_extract_func)(struct dp_packet_batch *batch,
                                          struct netdev_flow_key *keys,
                                          uint32_t keys_size,
                                          odp_port_t in_port,
                                          struct dp_netdev_pmd_thread
                                          *pmd_handle);

BUILD_ASSERT_DECL(NETDEV_MAX_BURST == 32);

/* Assert if there is flow map units change. */
BUILD_ASSERT_DECL(FLOWMAP_UNITS == 2);

/* Probe function is used to detect if this CPU has the ISA required
 * to run the optimized miniflow implementation.
 * returns one on successful probe.
 * returns zero on failure.
 */
typedef int (*miniflow_extract_probe)(void);

/* Structure representing the attributes of an optimized implementation. */
struct dpif_miniflow_extract_impl {
    /* When non-zero, this impl has passed the probe() checks. */
    bool available;

    /* Probe function is used to detect if this CPU has the ISA required
     * to run the optimized miniflow implementation.
     */
    miniflow_extract_probe probe;

    /* Optional function to call to extract miniflows for a burst of packets.
     */
    miniflow_extract_func extract_func;

    /* Name of the optimized implementation. */
    char *name;
};


/* Enum to hold implementation indexes. The list is traversed
 * linearly as from the ISA perspective, the VBMI version
 * should always come before the generic AVX512-F version.
 */
enum dpif_miniflow_extract_impl_idx {
    MFEX_IMPL_AUTOVALIDATOR,
    MFEX_IMPL_SCALAR,
    MFEX_IMPL_STUDY,
    MFEX_IMPL_MAX
};

/* Define a index which points to the first traffic optimized MFEX
 * option from the enum list else holds max value.
 */

#define MFEX_IMPL_START_IDX MFEX_IMPL_MAX

/* This function returns all available implementations to the caller. The
 * quantity of implementations is returned by the int return value.
 */
void
dp_mfex_impl_get(struct ds *reply, struct dp_netdev_pmd_thread **pmd_list,
                 size_t pmd_list_size) OVS_REQUIRES(dp_netdev_mutex);

/* This function checks all available MFEX implementations, and selects the
 * returns the function pointer to the one requested by "name".
 */
int
dp_mfex_impl_get_by_name(const char *name, miniflow_extract_func *out_func);

/* Returns the default MFEX which is first ./configure selected, but can be
 * overridden at runtime. */
miniflow_extract_func dp_mfex_impl_get_default(void);

/* Overrides the default MFEX with the user set MFEX. */
int dp_mfex_impl_set_default_by_name(const char *name);

/* Retrieve the array of miniflow implementations for iteration.
 * On error, returns a negative number.
 * On success, returns the size of the arrays pointed to by the out parameter.
 */
int
dpif_mfex_impl_info_get(struct dpif_miniflow_extract_impl **out_ptr);


/* Initializes the available miniflow extract implementations by probing for
 * the CPU ISA requirements. As the runtime available CPU ISA does not change
 * and the required ISA of the implementation also does not change, it is safe
 * to cache the probe() results, and not call probe() at runtime.
 */
void
dpif_miniflow_extract_init(void);

/* Retrieve the hitmask of the batch of pakcets which is obtained by comparing
 * different miniflow implementations with linear miniflow extract.
 * Key_size need to be at least the size of the batch.
 * On error, returns a zero.
 * On success, returns the number of packets in the batch compared.
 */
uint32_t
dpif_miniflow_extract_autovalidator(struct dp_packet_batch *batch,
                                    struct netdev_flow_key *keys,
                                    uint32_t keys_size, odp_port_t in_port,
                                    struct dp_netdev_pmd_thread *pmd_handle);

/* Retrieve the number of packets by studying packets using different miniflow
 * implementations to choose the best implementation using the maximum hitmask
 * count.
 * On error, returns a zero for no packets.
 * On success, returns mask of the packets hit.
 */
uint32_t
mfex_study_traffic(struct dp_packet_batch *packets,
                   struct netdev_flow_key *keys,
                   uint32_t keys_size, odp_port_t in_port,
                   struct dp_netdev_pmd_thread *pmd_handle);

#endif /* MFEX_AVX512_EXTRACT */
