#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ethtool.h>
#include <linux/mutex.h>

#define DRV_NAME "virt_eth"
#define DRV_VERSION "0.3"
#define RING_SIZE 64 /* TX/RX Ring Size */

/* Private driver structure */

struct virteth_priv {
    struct napi_struct napi;

    /* RX ring */
    struct sk_buff *rx_ring[RING_SIZE];
    unsigned int rx_head;
    unsigned int rx_tail;
    unsigned int rx_count;

    /* TX ring */
    struct sk_buff *tx_ring[RING_SIZE];
    unsigned int tx_head;
    unsigned int tx_tail;
    unsigned int tx_count;

    /* Link settings */
    struct mutex link_lock;
    u32 link_speed;   /* Mbps */
    u8 duplex;        /* 0 = half, 1 = full */
    u8 autoneg;       /* 0 = off, 1 = on */

    struct net_device *dev;
};

static struct net_device *virteth_dev;

/* Ring Buffer Helper Functions */

static bool ring_is_full(unsigned int count){
    return count >= RING_SIZE;
}

static bool ring_is_empty(unsigned int count){
    return count==0;
}

/* NAPI poll */

static int virteth_poll(struct napi_struct *napi, int budget){
    struct virteth_priv *priv = container_of(napi,struct virteth_priv,napi);
    int work_done = 0;

    while(work_done < budget && !ring_is_empty(priv->rx_count)) {
        struct sk_buff *skb;

        /* Pop from RX ring */
        skb = priv->rx_ring[priv->rx_head];
        priv->rx_ring[priv->rx_head] = NULL;
        priv->rx_head = (priv->rx_head + 1) % RING_SIZE;
        priv->rx_count--;

        dev_info(&virteth_dev->dev,"NAPI delivering packet len=%u protocol=0x%04x\n",skb->len, ntohs(skb->protocol));

        /* Deliver via GRO-aware receive */
        napi_gro_receive(napi,skb);

        /* Update RX Stats */
        virteth_dev->stats.rx_packets++;
        virteth_dev->stats.rx_bytes += skb->len;

        work_done++;
    }

    if (ring_is_empty(priv->rx_count)){
        /* No more packets -> complete NAPI */
        dev_info(&virteth_dev->dev,"NAPI poll complete, work_done=%d\n", work_done);
        napi_complete_done(napi,work_done);
    }

    return work_done;
}



static int virteth_open(struct net_device *dev){

    struct virteth_priv *priv = netdev_priv(dev);

    priv->rx_head = priv->rx_tail = priv->rx_count = 0;
    priv->tx_head = priv->tx_tail = priv->tx_count = 0;
    napi_enable(&priv->napi);
    netif_start_queue(dev);
    netif_carrier_on(dev);
    dev_info(&dev->dev,"Device Opened \n");
    return 0;
}

static int	virteth_stop(struct net_device *dev){

    struct virteth_priv *priv = netdev_priv(dev);
    priv->rx_head = priv->rx_tail = priv->rx_count = 0;
    priv->tx_head = priv->tx_tail = priv->tx_count = 0;
    netif_carrier_off(dev);
    netif_stop_queue(dev);
    napi_disable(&priv->napi);

    dev_info(&dev->dev,"Device Stoped (link down) \n");
    return 0;
}

static netdev_tx_t virteth_xmit(struct sk_buff *skb,struct net_device *dev){

    struct virteth_priv *priv = netdev_priv(dev);

    /* Check TX ring space  */
    if(ring_is_full(priv->tx_count)){
        /* TX ring full:stop queue and retyr later */
        dev_warn(&dev->dev, "TX ring full, stopping queue\n");
        netif_stop_queue(dev);
        return NETDEV_TX_BUSY;
    }

    /* Put skb into TX ring */
    priv->tx_ring[priv->tx_tail] = skb;
    priv->tx_tail = (priv->tx_tail + 1) % RING_SIZE;
    priv->tx_count++;

    dev_info(&dev->dev, "TX queued packet len=%u protocol=0x%04x (tx_count=%u)\n",skb->len, ntohs(skb->protocol), priv->tx_count);

    /* Update TX Stats */
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    /* Simulate hardware loopback: move skb into RX ring */
    if(!ring_is_full(priv->rx_count)){
        /* Clone the skb for loopback */
        struct sk_buff *rx_skb = skb_clone(skb,GFP_ATOMIC);
        
        if(rx_skb){
            rx_skb->dev = dev;
            rx_skb->protocol = eth_type_trans(rx_skb,dev);

            priv->rx_ring[priv->rx_tail] = rx_skb;
            priv->rx_tail = (priv->rx_tail + 1) % RING_SIZE;
            priv->rx_count++;

            dev_info(&dev->dev,"Looped packet into RX ring (rx_count=%u)\n",priv->rx_count);

            /* Schedule NAPI to process RX */
            napi_schedule(&priv->napi);

        }
        else{
            dev_warn(&dev->dev,"skb_clone failed, dropping loopback RX\n");
            dev->stats.rx_dropped++;
        }

    }
    else{
        dev_warn(&dev->dev,"RX ring full,dropping packet\n");
        dev->stats.rx_dropped++;
    }

    
    /* Free TX skb */
    dev_kfree_skb(skb);
    priv->tx_head = (priv->tx_head + 1) % RING_SIZE;
    priv->tx_count--;

    /* Wake queue if we stopeed earlier */
    if (netif_queue_stopped(dev) && !ring_is_full(priv->tx_count)){
        dev_info(&dev->dev, "Waking TX queue\n");
        netif_wake_queue(dev);
    }
    
    return NETDEV_TX_OK;
}

/* net_device operations */
static struct net_device_ops virteth_netdev_ops = {
    .ndo_open = virteth_open,
    .ndo_stop = virteth_stop,
    .ndo_start_xmit = virteth_xmit,
};

/* ---- Ethtool ---- */
static void virteth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
    strscpy(info->driver, DRV_NAME, sizeof(info->driver));
    strscpy(info->version, DRV_VERSION, sizeof(info->version));
    strscpy(info->fw_version, "N/A", sizeof(info->fw_version));
    strscpy(info->bus_info, dev_name(&dev->dev), sizeof(info->bus_info));
}

static u32 virteth_get_link(struct net_device *dev)
{
    return netif_carrier_ok(dev) ? 1 : 0;
}

static int virteth_get_link_ksettings(struct net_device *dev,
                                      struct ethtool_link_ksettings *cmd)
{
    struct virteth_priv *priv = netdev_priv(dev);

    mutex_lock(&priv->link_lock);
    cmd->base.speed = priv->link_speed;
    cmd->base.duplex = priv->duplex ? DUPLEX_FULL : DUPLEX_HALF;
    cmd->base.autoneg = priv->autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
    mutex_unlock(&priv->link_lock);

    return 0;
}

static int virteth_set_link_ksettings(struct net_device *dev,
                                      const struct ethtool_link_ksettings *cmd)
{
    struct virteth_priv *priv = netdev_priv(dev);

    mutex_lock(&priv->link_lock);
    priv->link_speed = cmd->base.speed;
    priv->duplex = (cmd->base.duplex == DUPLEX_FULL);
    priv->autoneg = (cmd->base.autoneg == AUTONEG_ENABLE);
    mutex_unlock(&priv->link_lock);

    if (priv->link_speed == 0)
        netif_carrier_off(dev);
    else
        netif_carrier_on(dev);

    return 0;
}

static const struct ethtool_ops virteth_ethtool_ops = {
    .get_drvinfo        = virteth_get_drvinfo,
    .get_link           = virteth_get_link,
    .get_link_ksettings = virteth_get_link_ksettings,
    .set_link_ksettings = virteth_set_link_ksettings,
};



static int __init virteth_init(void){

    int ret;
    struct virteth_priv *priv;

    /* Allocate ethernet device */
    virteth_dev = alloc_etherdev(sizeof(struct virteth_priv));
    if(!virteth_dev){
        pr_err("%s: failed to allocate ethernet device \n",DRV_NAME);
        return -ENOMEM;
    }

    /* Setting up virtual ethernet device name  */
    strncpy(virteth_dev->name,"virteth%d",IFNAMSIZ);

    /* Standard Ethernet Setup (sets type,header ops, addr_len etc)*/
    ether_setup(virteth_dev);

    /* Setup Random MAC Address */
    eth_hw_addr_random(virteth_dev);

    /* Assign NETDEV Ops */
    virteth_dev->netdev_ops = &virteth_netdev_ops;

    /* Set mtu to ETH_DATA_LEN -> 1500 */
    virteth_dev->mtu = ETH_DATA_LEN;

    /* Set TX Queue len to 1000 */
    virteth_dev->tx_queue_len = 1000;
    virteth_dev->ethtool_ops = &virteth_ethtool_ops;

    priv = netdev_priv(virteth_dev);
    memset(priv,0,sizeof(*priv));

    priv->dev = virteth_dev;
    mutex_init(&priv->link_lock);
    priv->link_speed = 100; /* Default 100 Mbps full duplex */
    priv->duplex = 1;
    priv->autoneg = 0;

    /* Add NAPI poll mechanism */
    netif_napi_add(virteth_dev,&priv->napi,virteth_poll);
    priv->napi.weight = 64 ; /* set weight  */

    /* Registers the device with kernel */
    ret = register_netdev(virteth_dev);
    if(ret){
        pr_err("%s: failed to register net_device (err=%d)\n",DRV_NAME,ret);
        free_netdev(virteth_dev);
        return ret;
    }

    pr_info("%s: registered device %s, MAC=%pM\n",DRV_NAME,virteth_dev->name,virteth_dev->dev_addr);
    return 0;
}

static void __exit virteth_exit(void){

    struct virteth_priv *priv = netdev_priv(virteth_dev);

    if(!virteth_dev){
        return;
    }
    netif_napi_del(&priv->napi);
    unregister_netdev(virteth_dev);
    free_netdev(virteth_dev);
    pr_info("%s: unregistered device \n",DRV_NAME);
}

module_init(virteth_init);
module_exit(virteth_exit);

MODULE_AUTHOR("Bharath R");
MODULE_DESCRIPTION("Virtual Ethernet Driver with TX/RX rings + NAPI");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);



