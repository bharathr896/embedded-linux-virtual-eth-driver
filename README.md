# Embedded Linux Virtual Ethernet Driver

This project implements a **virtual Ethernet driver** for Linux, built from scratch to explore how the kernel networking stack (`net_device`, SKBs, NAPI, and rings) interacts with drivers.  

Creates a virtual NIC (`virteth0`). Outgoing packets are looped back into the receive path so you can test networking concepts with tools like `ping`, `tcpdump`, and `iperf` — without needing a physical NIC.  

---

- **Network device integration**
  - Registers `virteth0` as a Linux network device.
  - Implements open/stop, transmit (`ndo_start_xmit`), and RX handling.

- **Loopback functionality**
  - Transmitted SKBs are cloned and injected back into the RX path.
  - Uses `eth_type_trans()` to set `skb->protocol` and hand packets correctly to the stack.
  - Ping to self works:  
    ```bash
    ping -I virteth0 192.168.100.1
    ```

- **TX/RX ring simulation**
  - Software ring buffers mimic hardware descriptor queues.
  - Head/tail pointers with wraparound (circular queue).
  - TX queue flow control with `netif_stop_queue()` and `netif_wake_queue()`.

- **NAPI support**
  - RX path uses NAPI polling instead of pure interrupt simulation.
  - `napi_gro_receive()` batches packets (like real drivers).
  - Reduces packet loss under load.

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

# Test loopback
ping -I virteth0 192.168.100.1
```