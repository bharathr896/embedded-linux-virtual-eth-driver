#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/skbuff.h>

#define DRV_NAME "virt_eth"
#define DRV_VERSION "0.1"

static int	virteth_open(struct net_device *dev);
static int	virteth_stop(struct net_device *dev);
static netdev_tx_t	virteth_xmit(struct sk_buff *skb,struct net_device *dev);

static int virteth_open(struct net_device *dev){
    dev_info(&dev->dev,"Device Opened \n");
    netif_start_queue(dev);
    return 0;
}

static int	virteth_stop(struct net_device *dev){
    netif_stop_queue(dev);
    dev_info(&dev->dev,"Device Stoped \n");
    return 0;
}

static netdev_tx_t virteth_xmit(struct sk_buff *skb,struct net_device *dev){
    dev_info(&dev->dev,"xmit len=%u protocol=0x%04x\n",skb->len,ntohs(skb->protocol));
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    dev_kfree_skb(skb);

    return NETDEV_TX_OK;
}

/* net_device operations */
static struct net_device_ops virteth_netdev_ops = {
    .ndo_open = virteth_open,
    .ndo_stop = virteth_stop,
    .ndo_start_xmit = virteth_xmit,
};

static struct net_device *virteth_dev;


static int __init virteth_init(void){

    int ret;

    /* Allocate ethernet device */
    virteth_dev = alloc_etherdev(0);
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
    if(!virteth_dev){
        return;
    }
    unregister_netdev(virteth_dev);
    free_netdev(virteth_dev);
    pr_info("%s: unregistered device \n",DRV_NAME);
}

module_init(virteth_init);
module_exit(virteth_exit);

MODULE_AUTHOR("Bharath R");
MODULE_DESCRIPTION("Virtual Ethernet Driver (skeleton)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);



