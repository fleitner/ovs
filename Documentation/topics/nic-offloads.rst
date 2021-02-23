..
      Licensed under the Apache License, Version 2.0 (the "License"); you may
      not use this file except in compliance with the License. You may obtain
      a copy of the License at

          http://www.apache.org/licenses/LICENSE-2.0

      Unless required by applicable law or agreed to in writing, software
      distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
      WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
      License for the specific language governing permissions and limitations
      under the License.

      Convention for heading levels in Open vSwitch documentation:

      =======  Heading 0 (reserved for the title in a document)
      -------  Heading 1
      ~~~~~~~  Heading 2
      +++++++  Heading 3
      '''''''  Heading 4

      Avoid deeper levels because they do not render well.

============
NIC Offloads
============

This document explains the internals of Open vSwitch support for NIC offloads.

Design
------

The Open vSwitch should strive to forward packets as they arrive regardless
if the checksum is correct, for example. However, it cannot fix existing
problems. Therefore, when the packet has the checksum verified or it the
packet is known to be good, the checksum calculation can be offloaded to
the NIC, otherwise updates can be made as long as the previous situation
doesn't change. For example, a packet has corrupted IP checksum can be
accepted, a flow rule can change the IP destination address to another
address. In that case, OVS needs to partially recompute the checksum
instead of offloading or calculate all of it again which would fix the
existing issue.

The drivers can set flags indicating if the checksum is good or bad.
The checksum is considered unverified if no flag is set.

When a packet ingress the data path with good checksum, OVS should
enable checksum offload by default. This allows the data path to
postpone checksum updates until the packet egress the data path.

When a packet egress the data path, the packet flags and the egress
port flags are verified to make sure all required NIC offload
features to send out the packet are available. If not, the data
path will fall back to equivalent software implementation.


Drivers
-------

When the driver initiates, it should set the flags to tell the data path
which offload features are supported. For example, if the driver supports
IP checksum offloading, then netdev->ol_flags should set the flag
NETDEV_OFFLOAD_TX_IPV4_CSUM.


Rules
-----
1) OVS should strive to forward all packets regardless of checksum.

2) OVS must not correct a bad packet/checksum.

3) Packet with flag DP_PACKET_OL_RX_IP_CSUM_GOOD means that the
   IP checksum is present in the packet and it is good.

4) Packet with flag DP_PACKET_OL_RX_IP_CSUM_BAD means that the
   IP checksum is present in the packet and it is BAD. Extra care
   should be taken to not fix the packet during data path processing.

5) The ingress packet parser can only set DP_PACKET_OL_TX_IP_CSUM
   if the packet has DP_PACKET_OL_RX_L4_CKSUM_GOOD to not violate
   rule #2.

6) Packet with flag DP_PACKET_OL_TX_IPV4 is a IPv4 packet.

7) Packet with flag DP_PACKET_OL_TX_IPV6 is a IPv6 packet.

8) Packet with flag DP_PACKET_OL_TX_IP_CSUM tells the data path
   to skip updating the IP checksum if the packet is modified. The
   IP checksum will be calculated by the egress port if that
   supports IP checksum offload, otherwise the IP checksum will
   be done in software before handing over the packet to the port.

9) When there are modifications to the packet that requires checksum
   update, the data path needs to remove DP_PACKET_OL_RX_IP_CSUM_GOOD
   flag, otherwise the checksum is assumed to be good in the packet.
