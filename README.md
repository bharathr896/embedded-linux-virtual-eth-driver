# Embedded Linux Virtual Ethernet Driver

This project delivers a **production-style virtual Ethernet driver** for the Linux kernel.  
The driver registers a network interface (`virteth0`) and simulates NIC behavior with **TX/RX rings, NAPI support, and ethtool integration**.  

Packets transmitted are looped back into the receive path, enabling complete testing with tools like `ping`, `tcpdump`, and `iperf3` — all without requiring physical hardware.  

---

## Features

- **Network device integration**
  - Registers `virteth0` as a Linux network device.
  - Implements open/stop, transmit (`ndo_start_xmit`), and RX handling.

- **Loopback functionality**
  - Outgoing packets are cloned and injected into the RX path
  - Works seamlessly with `ping`, `iperf3`, and higher-layer protocols

- **TX/RX ring simulation**
  - Software ring buffers mimic hardware descriptor queues.
  - Head/tail pointers with wraparound (circular queue).
  - TX queue flow control with `netif_stop_queue()` and `netif_wake_queue()`.

- **NAPI support**
  - RX path leverages NAPI polling with `napi_gro_receive()`
  - Efficient under load, mimicking real NIC drivers

- **Ethtool support**
  - `ethtool` commands expose driver and link details
  - Configurable **speed, duplex, autonegotiation**
  - Query driver name, version, and bus info  

---

## How it works 

```bash
App (ping) → Socket → TCP/IP stack
 → dev_queue_xmit() → virteth_xmit()
    - skb enqueued in TX ring
    - cloned into RX ring
 → NAPI poll dequeues from RX ring
    - eth_type_trans() sets skb->protocol
    - napi_gro_receive() hands packet to kernel
 → IP/TCP stack → Socket buffer → App recv()
```

## Build & Run

```bash
# Build
cd src/
make 

# Load driver
sudo insmod virt_eth.ko

# Create interface
sudo ip addr add 192.168.100.1/24 dev virteth0
sudo ip link set virteth0 up
```

## Testing

### 1. Ping (loopback path)
```bash
ping -I virteth0 192.168.100.1
```
### 2. Ethtool
```bash
ethtool -i virteth0
ethtool virteth0
sudo ethtool -s virteth0 speed 1000 duplex full autoneg on
ethtool virteth0
```

### 3.iperf3
```bash
iperf3 -s -B 192.168.100.1      # terminal 1
iperf3 -c 192.168.100.1 -t 10   # terminal 2
```

## Example Results 

### Ping

```bash
$ ping -I virteth0 192.168.100.1 -c 3
PING 192.168.100.1 (192.168.100.1) from 192.168.100.1 virteth0: 56(84) bytes of data.
64 bytes from 192.168.100.1: icmp_seq=1 ttl=64 time=0.022 ms
64 bytes from 192.168.100.1: icmp_seq=2 ttl=64 time=0.014 ms
64 bytes from 192.168.100.1: icmp_seq=3 ttl=64 time=0.015 ms

--- 192.168.100.1 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2041ms
rtt min/avg/max/mdev = 0.014/0.017/0.022/0.003 ms

```
### Ethtool

```bash
$ ethtool virteth0

Settings for virteth0:
        Supported ports: [  ]
        Supported link modes:   Not reported
        Supported pause frame use: No
        Supports auto-negotiation: No
        Supported FEC modes: Not reported
        Advertised link modes:  Not reported
        Advertised pause frame use: No
        Advertised auto-negotiation: No
        Advertised FEC modes: Not reported
        Speed: 1000Mb/s
        Duplex: Full
        Auto-negotiation: on
        Port: Twisted Pair
        PHYAD: 0
        Transceiver: internal
        MDI-X: Unknown
netlink error: Operation not permitted

```

### iperf3

```bash
$ iperf3 -c 192.168.100.1 -t 10

Connecting to host 192.168.100.1, port 5201
[  5] local 192.168.100.1 port 58564 connected to 192.168.100.1 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec  8.29 GBytes  71.2 Gbits/sec    0   2.37 MBytes
[  5]   1.00-2.00   sec  8.47 GBytes  72.8 Gbits/sec    0   2.37 MBytes
[  5]   2.00-3.00   sec  8.53 GBytes  73.3 Gbits/sec    0   2.37 MBytes
[  5]   3.00-4.00   sec  8.50 GBytes  73.0 Gbits/sec    0   2.37 MBytes
[  5]   4.00-5.00   sec  8.47 GBytes  72.7 Gbits/sec    0   2.37 MBytes
[  5]   5.00-6.00   sec  8.44 GBytes  72.5 Gbits/sec    0   2.37 MBytes
[  5]   6.00-7.00   sec  8.45 GBytes  72.6 Gbits/sec    0   2.37 MBytes
[  5]   7.00-8.00   sec  8.43 GBytes  72.4 Gbits/sec    0   2.37 MBytes
[  5]   8.00-9.00   sec  8.65 GBytes  74.3 Gbits/sec    0   2.37 MBytes
[  5]   9.00-10.00  sec  8.24 GBytes  70.8 Gbits/sec    0   2.37 MBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  84.5 GBytes  72.5 Gbits/sec    0             sender
[  5]   0.00-10.00  sec  84.5 GBytes  72.5 Gbits/sec                  receiver

iperf Done.
```




