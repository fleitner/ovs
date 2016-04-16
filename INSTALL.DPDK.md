OVS DPDK INSTALL GUIDE
================================

## Contents

1. [Overview](#overview)
2. [Building and Installation](#build)
3. [Setup OVS DPDK datapath](#ovssetup)
4. [DPDK in the VM](#builddpdk)
5. [OVS Testcases](#ovstc)
6. [Limitations ](#ovslimits)

## <a name="overview"></a> 1. Overview

Open vSwitch can use DPDK lib to operate entirely in userspace.
This file provides information on installation and use of Open vSwitch
using DPDK datapath.  This version of Open vSwitch should be built manually
with `configure` and `make`.

The DPDK support of Open vSwitch is considered 'experimental'.

### Prerequisites

* Required: DPDK 16.04
* Hardware: [DPDK Supported NICs] when physical ports in use

## <a name="build"></a> 2. Building and Installation

### 2.1 Configure & build the Linux kernel

On Linux Distros running kernel version >= 3.0, kernel rebuild is not required
and only grub cmdline needs to be updated for enabling IOMMU [VFIO support - 3.2].
For older kernels, check if kernel is built with  UIO, HUGETLBFS, PROC_PAGE_MONITOR,
HPET, HPET_MMAP support.

Details system requirements can be found at [DPDK requirements]

### 2.2 Install DPDK
  1. [Download DPDK] and extract the file, for example in to /usr/src
     and set DPDK_DIR

     ```
     cd /usr/src/
     unzip dpdk-16.04.zip

     export DPDK_DIR=/usr/src/dpdk-16.04
     cd $DPDK_DIR
     ```

  2. Configure, Install DPDK

     Build and install the DPDK library.

     ```
     export DPDK_BUILD=$DPDK_DIR/x86_64-native-linuxapp-gcc
     make install T=x86_64-native-linuxapp-gcc DESTDIR=install
     ```

     Note: For previous DPDK releases, Set `CONFIG_RTE_BUILD_COMBINE_LIBS=y` in
     `config/common_linuxapp` to generate single library file.

### 2.3 Install OVS
  OVS can be downloaded in compressed format from the OVS release page (or)
  cloned from git repository if user intends to develop and contribute
  patches upstream.

  - [Download OVS] tar ball and extract the file, for example in to /usr/src
     and set OVS_DIR

     ```
     cd /usr/src/
     tar -zxvf openvswitch-2.5.0.tar.gz
     export OVS_DIR=/usr/src/openvswitch-2.5.0
     ```

  - Clone the Git repository for OVS, for example in to /usr/src

     ```
     cd /usr/src/
     git clone https://github.com/openvswitch/ovs.git
     export OVS_DIR=/usr/src/ovs
     ```

  - Install OVS dependencies

     GNU make, GCC 4.x (or) Clang 3.4  (Mandatory)
     libssl, libcap-ng, Python 2.7  (Optional)
     More information can be found at [Build Requirements]

  - Configure, Install OVS

     ```
     cd $OVS_DIR
     ./boot.sh
     ./configure --with-dpdk
     make install
     ```

## <a name="ovssetup"></a> 3. Setup OVS with DPDK datapath

### 3.1 Setup Hugepages

  Allocate and mount 2M Huge pages

  ```
  echo N > /proc/sys/vm/nr_hugepages, where N = No. of huge pages allocated
  mount -t hugetlbfs none /dev/hugepages
  ```

### 3.2 Setup DPDK devices using VFIO

  - Supported with DPDK release >= 1.7 and kernel version >= 3.6
  - VFIO needs support from BIOS and kernel.
  - BIOS changes:

    Enable VT-d, can be verified from `dmesg | grep -e DMAR -e IOMMU` output

  - GRUB bootline:

    Add `iommu=pt intel_iommu=on`, can be verified from `cat /proc/cmdline` output

  - Load modules and bind the NIC to VFIO driver

    ```
    modprobe vfio-pci
    sudo /usr/bin/chmod a+x /dev/vfio
    sudo /usr/bin/chmod 0666 /dev/vfio/*
    $DPDK_DIR/tools/dpdk_nic_bind.py --bind=vfio-pci eth1
    $DPDK_DIR/tools/dpdk_nic_bind.py --status
    ```

  Note: If using older DPDK release (or) running kernels < 3.6 UIO drivers to be used,
  please check section 4 (DPDK devices using UIO) for the steps.

### 3.3 Setup OVS

  1. DB creation (One time step)

     ```
     mkdir -p /usr/local/etc/openvswitch
     mkdir -p /usr/local/var/run/openvswitch
     rm /usr/local/etc/openvswitch/conf.db
     ovsdb-tool create /usr/local/etc/openvswitch/conf.db  \
            /usr/local/share/openvswitch/vswitch.ovsschema
     ```

  2. Start ovsdb-server

     No SSL support

     ```
     ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
         --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
         --pidfile --detach
     ```

     SSL support

     ```
     ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock \
         --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
         --private-key=db:Open_vSwitch,SSL,private_key \
         --certificate=Open_vSwitch,SSL,certificate \
         --bootstrap-ca-cert=db:Open_vSwitch,SSL,ca_cert --pidfile --detach
     ```

  3. Initialize DB (One time step)

     ```
     ovs-vsctl --no-wait init
     ```

  4. Start vswitchd

     DPDK configuration arguments can be passed to vswitchd via `--dpdk` option.
     This needs to be first argument passed to vswitchd process. On a dual socket
     system, socket-mem option below allocates 1GB from socket 0 and nothing on
     socket 1. The user should tune it accordingly.

     ```
     export DB_SOCK=/usr/local/var/run/openvswitch/db.sock
     ovs-vswitchd --dpdk -c 0x1 -n 4 --socket-mem 1024,0 \
     -- unix:$DB_SOCK --pidfile --detach
     ```

     To better scale the work loads across cores, Multiple pmd threads can be
     created and pinned to CPU cores by explicity specifying pmd-cpu-mask.
     eg: To spawn 2 pmd threads and pin them to cores 1, 2

     ```
     ovs-vsctl set Open_vSwitch . other_config:pmd-cpu-mask=6
     ```

  5. Create bridge & add DPDK devices

     create a bridge with datapath_type "netdev" in the configuration database

     `ovs-vsctl add-br br0 -- set bridge br0 datapath_type=netdev`

     Now you can add DPDK devices. OVS expects DPDK device names to start with
     "dpdk" and end with a device id.

     ```
     ovs-vsctl add-port br0 dpdk0 -- set Interface dpdk0 type=dpdk
     ovs-vsctl add-port br0 dpdk1 -- set Interface dpdk1 type=dpdk
     ```

     After the DPDK ports get added to switch, a polling thread continuously polls
     DPDK devices and consumes 100% of the core as can be checked from 'top' and 'ps' cmds.

     ```
     top -H
     ps -eLo pid,psr,comm | grep pmd
     ```

     Note: creating bonds of DPDK interfaces is slightly different to creating
     bonds of system interfaces.  For DPDK, the interface type must be explicitly
     set, for example:

     ```
     ovs-vsctl add-bond br0 dpdkbond dpdk0 dpdk1 -- set Interface dpdk0 type=dpdk -- set Interface dpdk1 type=dpdk
     ```

  6. PMD thread statistics

     ```
     # Check current stats
       ovs-appctl dpif-netdev/pmd-stats-show

     # Show port/rxq assignment
       ovs-appctl dpif-netdev/pmd-rxq-show

     # Clear previous stats
       ovs-appctl dpif-netdev/pmd-stats-clear
     ```

  7. Stop vswitchd & Delete bridge

     ```
     ovs-appctl -t ovs-vswitchd exit
     ovs-appctl -t ovsdb-server exit
     ovs-vsctl del-br br0
     ```

## <a name="builddpdk"></a> 4. DPDK in the VM

DPDK 'testpmd' application can be run in the Guest VM for high speed
packet forwarding between vhost ports. This needs DPDK, testpmd to be
compiled along with kernel modules. Below are the steps to be followed
for running testpmd application in the VM

  * Export the DPDK loc $DPDK_LOC to the Guest VM(/dev/sdb on VM)
    and instantiate the Guest.

  ```
  export VM_NAME=Centos-vm
  export GUEST_MEM=4096M
  export QCOW2_LOC=<Dir of Qcow2>
  export QCOW2_IMAGE=$QCOW2_LOC/CentOS7_x86_64.qcow2
  export DPDK_LOC=/usr/src/dpdk-16.04
  export VHOST_SOCK_DIR=/usr/local/var/run/openvswitch

  qemu-system-x86_64 -name $VM_NAME -cpu host -enable-kvm -m $GUEST_MEM -object memory-backend-file,id=mem,size=$GUEST_MEM,mem-path=/dev/hugepages,share=on -numa node,memdev=mem -mem-prealloc -smp sockets=1,cores=2 -drive file=$QCOW2_IMAGE -drive file=fat:rw:$DPDK_LOC,snapshot=off -chardev socket,id=char0,path=$VHOST_SOCK_DIR/dpdkvhostuser0 -netdev type=vhost-user,id=mynet1,chardev=char0,vhostforce -device virtio-net-pci,mac=00:00:00:00:00:01,netdev=mynet1,mrg_rxbuf=off -chardev socket,id=char1,path=$VHOST_SOCK_DIR/dpdkvhostuser1 -netdev type=vhost-user,id=mynet2,chardev=char1,vhostforce -device virtio-net-pci,mac=00:00:00:00:00:02,netdev=mynet2,mrg_rxbuf=off --nographic -snapshot
  ```

  * Copy the DPDK Srcs to VM and build DPDK

  ```
  mkdir -p /mnt/dpdk
  mount -o iocharset=utf8 /dev/sdb1 /mnt/dpdk
  cp -a /mnt/dpdk /root/dpdk
  cd /root/dpdk/
  export DPDK_DIR=/root/dpdk/
  export DPDK_BUILD=/root/dpdk/x86_64-native-linuxapp-gcc
  make install T=x86_64-native-linuxapp-gcc DESTDIR=install
  ```

  * Build the test-pmd application

  ```
  cd app/test-pmd
  export RTE_SDK=/root/dpdk
  export RTE_TARGET=x86_64-native-linuxapp-gcc
  make
  ```

  * Setup Huge pages and DPDK devices using UIO

  ```
  sysctl vm.nr_hugepages=1024
  mkdir -p /dev/hugepages
  mount -t hugetlbfs hugetlbfs /dev/hugepages
  modprobe uio
  insmod $DPDK_BUILD/kmod/igb_uio.ko
  $DPDK_DIR/tools/dpdk_nic_bind.py --status
  $DPDK_DIR/tools/dpdk_nic_bind.py -b igb_uio 00:03.0 00:04.0
  ```

  vhost ports pci ids can be retrieved using `lspci | grep Ethernet` cmd.

## <a name="ovstc"></a> 5. OVS Testcases

  Below are few testcases and the list of steps to be followed.

### 5.1 PHY-PHY

  The steps (1-5) in 3.3 section will create & initialize DB, start vswitchd and also
  add DPDK devices to bridge 'br0'.

  1. Add Test flows to forward packets betwen DPDK port 0 and port 1

       ```
       # Clear current flows
       ovs-ofctl del-flows br0

       # Add flows between port 1 (dpdk0) to port 2 (dpdk1)
       ovs-ofctl add-flow br0 in_port=1,action=output:2
       ovs-ofctl add-flow br0 in_port=2,action=output:1
       ```

### 5.2 PHY-VM-PHY [VHOST LOOPBACK]

  The steps (1-5) in 3.3 section will create & initialize DB, start vswitchd and also
  add DPDK devices to bridge 'br0'.

  1. Add dpdkvhostuser ports to bridge 'br0'

       ```
       ovs-vsctl add-port br0 dpdkvhostuser0 -- set Interface dpdkvhostuser0 type=dpdkvhostuser
       ovs-vsctl add-port br0 dpdkvhostuser1 -- set Interface dpdkvhostuser1 type=dpdkvhostuser
       ```

  2. Add Test flows to forward packets betwen DPDK devices and VM ports

       ```
       # Clear current flows
       ovs-ofctl del-flows br0

       # Add flows
       ovs-ofctl add-flow br0 idle_timeout=0,in_port=1,action=output:3
       ovs-ofctl add-flow br0 idle_timeout=0,in_port=3,action=output:1
       ovs-ofctl add-flow br0 idle_timeout=0,in_port=4,action=output:2
       ovs-ofctl add-flow br0 idle_timeout=0,in_port=2,action=output:4

       # Dump flows
       ovs-ofctl dump-flows br0
       ```

  3. start Guest VM

       Guest Configuration

       ```
       | configuration        | values | comments
       |----------------------|--------|-----------------
       | qemu thread affinity | core 5 | taskset 0x20
       | memory               | 4GB    | -
       | cores                | 2      | -
       | Qcow2 image          | CentOS7| -
       | mrg_rxbuf            | off    | -
       | export DPDK sources  | yes    | -drive file=fat:rw:$DPDK_LOC(seen as /dev/sdb in VM)
       ```

       ```
       export VM_NAME=vhost-vm
       export GUEST_MEM=4096M
       export QCOW2_IMAGE=/root/CentOS7_x86_64.qcow2
       export DPDK_LOC=/usr/src/dpdk-16.04
       export VHOST_SOCK_DIR=/usr/local/var/run/openvswitch

       taskset 0x20 qemu-system-x86_64 -name $VM_NAME -cpu host -enable-kvm -m $GUEST_MEM -object memory-backend-file,id=mem,size=$GUEST_MEM,mem-path=/dev/hugepages,share=on -numa node,memdev=mem -mem-prealloc -smp sockets=1,cores=2 -drive file=$QCOW2_IMAGE -drive file=fat:rw:$DPDK_LOC,snapshot=off -chardev socket,id=char0,path=$VHOST_SOCK_DIR/dpdkvhostuser0 -netdev type=vhost-user,id=mynet1,chardev=char0,vhostforce -device virtio-net-pci,mac=00:00:00:00:00:01,netdev=mynet1,mrg_rxbuf=off -chardev socket,id=char1,path=$VHOST_SOCK_DIR/dpdkvhostuser1 -netdev type=vhost-user,id=mynet2,chardev=char1,vhostforce -device virtio-net-pci,mac=00:00:00:00:00:02,netdev=mynet2,mrg_rxbuf=off --nographic -snapshot
       ```

  4. DPDK Packet forwarding in Guest VM

     To accomplish this, DPDK and testpmd application has to be first compiled
     on the VM and the steps have been listed in section 4(DPDK in the VM).

       * Run test-pmd application

       ```
       cd $DPDK_DIR/app/test-pmd;
       ./testpmd -c 0x3 -n 4 --socket-mem 1024 -- --burst=64 -i --txqflags=0xf00 --disable-hw-vlan
       set fwd mac_retry
       start
       ```

       * Bind vNIC back to kernel once the test is completed.

       ```
       $DPDK_DIR/tools/dpdk_nic_bind.py --bind=virtio-pci 0000:00:03.0
       $DPDK_DIR/tools/dpdk_nic_bind.py --bind=virtio-pci 0000:00:04.0
       ```
       Note: Appropriate PCI IDs to be passed in above example. The PCI IDs can be
       retrieved using '$DPDK_DIR/tools/dpdk_nic_bind.py --status' cmd.

### 5.3 PHY-VM-PHY [IVSHMEM]

  IVSHMEM is supported only with 1GB huge pages. The steps for this testcase are listed
  in section 5.2(PVP - IVSHMEM) ADVANCED DPDK install guide.

## <a name="ovslimits"></a> 6. Limitations

  - Supports MTU size 1500, needs few changes in DPDK lib to fix this issue.
  - Currently DPDK ports does not use HW offload functionality.
  - DPDK IVSHMEM support works with 1G huge pages.

Bug Reporting:
--------------

Please report problems to bugs@openvswitch.org.


[DPDK requirements]: http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html
[Download DPDK]: http://dpdk.org/browse/dpdk/refs/
[Download OVS]: http://openvswitch.org/releases/
[DPDK Supported NICs]: http://dpdk.org/doc/nics
[Build Requirements]: https://github.com/openvswitch/ovs/blob/master/INSTALL.md#build-requirements
