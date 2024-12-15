/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016-2018 Microsoft Corporation
 * Copyright(c) 2013-2016 Brocade Communications Systems, Inc.
 * All rights reserved.
 */

// #include <stdint.h>
// #include <string.h>
// #include <stdio.h>
// #include <errno.h>
// #include <unistd.h>
// #include <dirent.h>
// #include <net/if.h>
// #include <net/if_arp.h>
// #include <netinet/in.h>
// #include <sys/ioctl.h>

#include <uk/alloc.h>
#include <uk/netdev.h>
#include <uk/netdev_core.h>
#include <uk/netdev_driver.h>
#include <uk/plat/io.h>

// #include "hn_logs.h"
#include "hn_var.h"
#include "hn_rndis.h"
#include "hn_nvs.h"
#include "ndis.h"

#define HN_TX_OFFLOAD_CAPS (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | \
			    RTE_ETH_TX_OFFLOAD_TCP_CKSUM  | \
			    RTE_ETH_TX_OFFLOAD_UDP_CKSUM  | \
			    RTE_ETH_TX_OFFLOAD_TCP_TSO    | \
			    RTE_ETH_TX_OFFLOAD_MULTI_SEGS | \
			    RTE_ETH_TX_OFFLOAD_VLAN_INSERT)

#define HN_RX_OFFLOAD_CAPS (RTE_ETH_RX_OFFLOAD_CHECKSUM | \
			    RTE_ETH_RX_OFFLOAD_VLAN_STRIP | \
			    RTE_ETH_RX_OFFLOAD_RSS_HASH)

#define NETVSC_ARG_LATENCY "latency"
#define NETVSC_ARG_RXBREAK "rx_copybreak"
#define NETVSC_ARG_TXBREAK "tx_copybreak"
#define NETVSC_ARG_RX_EXTMBUF_ENABLE "rx_extmbuf_enable"

/* The max number of retry when hot adding a VF device */
#define NETVSC_MAX_HOTADD_RETRY 10

#define  HN_INTR_EN   (1 << 0)
#define  HN_INTR_EN_MASK   (1)
#define  HN_INTR_USR_EN   (1 << 1)
#define  HN_INTR_USR_EN_MASK   (2)

// struct hn_xstats_name_off {
// 	char name[RTE_ETH_XSTATS_NAME_SIZE];
// 	unsigned int offset;
// };

// static const struct hn_xstats_name_off hn_stat_strings[] = {
// 	{ "good_packets",           offsetof(struct hn_stats, packets) },
// 	{ "good_bytes",             offsetof(struct hn_stats, bytes) },
// 	{ "errors",                 offsetof(struct hn_stats, errors) },
// 	{ "ring full",              offsetof(struct hn_stats, ring_full) },
// 	{ "channel full",           offsetof(struct hn_stats, channel_full) },
// 	{ "multicast_packets",      offsetof(struct hn_stats, multicast) },
// 	{ "broadcast_packets",      offsetof(struct hn_stats, broadcast) },
// 	{ "undersize_packets",      offsetof(struct hn_stats, size_bins[0]) },
// 	{ "size_64_packets",        offsetof(struct hn_stats, size_bins[1]) },
// 	{ "size_65_127_packets",    offsetof(struct hn_stats, size_bins[2]) },
// 	{ "size_128_255_packets",   offsetof(struct hn_stats, size_bins[3]) },
// 	{ "size_256_511_packets",   offsetof(struct hn_stats, size_bins[4]) },
// 	{ "size_512_1023_packets",  offsetof(struct hn_stats, size_bins[5]) },
// 	{ "size_1024_1518_packets", offsetof(struct hn_stats, size_bins[6]) },
// 	{ "size_1519_max_packets",  offsetof(struct hn_stats, size_bins[7]) },
// };

/* The default RSS key.
 * This value is the same as MLX5 so that flows will be
 * received on same path for both VF and synthetic NIC.
 */
// static const uint8_t rss_default_key[NDIS_HASH_KEYSIZE_TOEPLITZ] = {
// 	0x2c, 0xc6, 0x81, 0xd1,	0x5b, 0xdb, 0xf4, 0xf7,
// 	0xfc, 0xa2, 0x83, 0x19,	0xdb, 0x1a, 0x3e, 0x94,
// 	0x6b, 0x9e, 0x38, 0xd9,	0x2c, 0x9c, 0x03, 0xd1,
// 	0xad, 0x99, 0x44, 0xa7,	0xd9, 0x56, 0x3d, 0x59,
// 	0x06, 0x3c, 0x25, 0xf3,	0xfc, 0x1f, 0xdc, 0x2a,
// };

// static struct rte_eth_dev *
// eth_dev_vmbus_allocate(struct rte_vmbus_device *dev, size_t private_data_size)
// {
// 	struct rte_eth_dev *eth_dev;
// 	const char *name;

// 	if (!dev)
// 		return NULL;

// 	name = dev->device.name;

// 	if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
// 		eth_dev = rte_eth_dev_allocate(name);
// 		if (!eth_dev) {
// 			PMD_DRV_LOG(NOTICE, "can not allocate rte ethdev");
// 			return NULL;
// 		}

// 		if (private_data_size) {
// 			eth_dev->data->dev_private =
// 				rte_zmalloc_socket(name, private_data_size,
// 						     RTE_CACHE_LINE_SIZE, dev->device.numa_node);
// 			if (!eth_dev->data->dev_private) {
// 				PMD_DRV_LOG(NOTICE, "can not allocate driver data");
// 				rte_eth_dev_release_port(eth_dev);
// 				return NULL;
// 			}
// 		}
// 	} else {
// 		eth_dev = rte_eth_dev_attach_secondary(name);
// 		if (!eth_dev) {
// 			PMD_DRV_LOG(NOTICE, "can not attach secondary");
// 			return NULL;
// 		}
// 	}

// 	eth_dev->device = &dev->device;

// 	/* interrupt is simulated */
// 	rte_intr_type_set(dev->intr_handle, RTE_INTR_HANDLE_EXT);
// 	eth_dev->data->dev_flags |= RTE_ETH_DEV_INTR_LSC;
// 	eth_dev->intr_handle = dev->intr_handle;

// 	return eth_dev;
// }

// static void
// eth_dev_vmbus_release(struct rte_eth_dev *eth_dev)
// {
// 	/* free ether device */
// 	rte_eth_dev_release_port(eth_dev);

// 	eth_dev->device = NULL;
// 	eth_dev->intr_handle = NULL;
// }

// static int hn_set_parameter(const char *key, const char *value, void *opaque)
// {
// 	struct hn_data *hv = opaque;
// 	char *endp = NULL;
// 	unsigned long v;

// 	v = strtoul(value, &endp, 0);
// 	if (*value == '\0' || *endp != '\0') {
// 		PMD_DRV_LOG(ERR, "invalid parameter %s=%s", key, value);
// 		return -EINVAL;
// 	}

// 	if (!strcmp(key, NETVSC_ARG_LATENCY)) {
// 		/* usec to nsec */
// 		hv->latency = v * 1000;
// 		PMD_DRV_LOG(DEBUG, "set latency %u usec", hv->latency);
// 	} else if (!strcmp(key, NETVSC_ARG_RXBREAK)) {
// 		hv->rx_copybreak = v;
// 		PMD_DRV_LOG(DEBUG, "rx copy break set to %u",
// 			    hv->rx_copybreak);
// 	} else if (!strcmp(key, NETVSC_ARG_TXBREAK)) {
// 		hv->tx_copybreak = v;
// 		PMD_DRV_LOG(DEBUG, "tx copy break set to %u",
// 			    hv->tx_copybreak);
// 	} else if (!strcmp(key, NETVSC_ARG_RX_EXTMBUF_ENABLE)) {
// 		hv->rx_extmbuf_enable = v;
// 		PMD_DRV_LOG(DEBUG, "rx extmbuf enable set to %u",
// 			    hv->rx_extmbuf_enable);
// 	}

// 	return 0;
// }

// /* Parse device arguments */
// static int hn_parse_args(const struct rte_eth_dev *dev)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	struct rte_devargs *devargs = dev->device->devargs;
// 	static const char * const valid_keys[] = {
// 		NETVSC_ARG_LATENCY,
// 		NETVSC_ARG_RXBREAK,
// 		NETVSC_ARG_TXBREAK,
// 		NETVSC_ARG_RX_EXTMBUF_ENABLE,
// 		NULL
// 	};
// 	struct rte_kvargs *kvlist;
// 	int ret;

// 	if (!devargs)
// 		return 0;

// 	PMD_INIT_LOG(DEBUG, "device args %s %s",
// 		     devargs->name, devargs->args);

// 	kvlist = rte_kvargs_parse(devargs->args, valid_keys);
// 	if (!kvlist) {
// 		PMD_DRV_LOG(ERR, "invalid parameters");
// 		return -EINVAL;
// 	}

// 	ret = rte_kvargs_process(kvlist, NULL, hn_set_parameter, hv);
// 	rte_kvargs_free(kvlist);

// 	return ret;
// }

/* Update link status.
 * Note: the DPDK definition of "wait_to_complete"
 *   means block this call until link is up.
 *   which is not worth supporting.
 */
// int
// hn_dev_link_update(struct rte_eth_dev *dev,
// 		   int wait_to_complete __rte_unused)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	struct rte_eth_link link, old;
// 	int error;

// 	old = dev->data->dev_link;

// 	error = hn_rndis_get_linkstatus(hv);
// 	if (error)
// 		return error;

// 	hn_rndis_get_linkspeed(hv);

// 	link = (struct rte_eth_link) {
// 		.link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
// 		.link_autoneg = RTE_ETH_LINK_SPEED_FIXED,
// 		.link_speed = hv->link_speed / 10000,
// 	};

// 	if (hv->link_status == NDIS_MEDIA_STATE_CONNECTED)
// 		link.link_status = RTE_ETH_LINK_UP;
// 	else
// 		link.link_status = RTE_ETH_LINK_DOWN;

// 	if (old.link_status == link.link_status)
// 		return 0;

// 	PMD_INIT_LOG(DEBUG, "Port %d is %s", dev->data->port_id,
// 		     (link.link_status == RTE_ETH_LINK_UP) ? "up" : "down");

// 	return rte_eth_linkstatus_set(dev, &link);
// }

// static int hn_dev_info_get(struct rte_eth_dev *dev,
// 			   struct rte_eth_dev_info *dev_info)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	int rc;

// 	dev_info->speed_capa = RTE_ETH_LINK_SPEED_10G;
// 	dev_info->min_rx_bufsize = HN_MIN_RX_BUF_SIZE;
// 	dev_info->max_rx_pktlen  = HN_MAX_XFER_LEN;
// 	dev_info->max_mac_addrs  = 1;

// 	dev_info->hash_key_size = NDIS_HASH_KEYSIZE_TOEPLITZ;
// 	dev_info->flow_type_rss_offloads = hv->rss_offloads;
// 	dev_info->reta_size = RTE_ETH_RSS_RETA_SIZE_128;

// 	dev_info->max_rx_queues = hv->max_queues;
// 	dev_info->max_tx_queues = hv->max_queues;

// 	dev_info->tx_desc_lim.nb_min = 1;
// 	dev_info->tx_desc_lim.nb_max = 4096;

// 	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
// 		return 0;

// 	/* fills in rx and tx offload capability */
// 	rc = hn_rndis_get_offload(hv, dev_info);
// 	if (rc != 0)
// 		return rc;

// 	/* merges the offload and queues of vf */
// 	return hn_vf_info_get(hv, dev_info);
// }

// static int hn_rss_reta_update(struct rte_eth_dev *dev,
// 			      struct rte_eth_rss_reta_entry64 *reta_conf,
// 			      uint16_t reta_size)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	unsigned int i;
// 	int err;

// 	PMD_INIT_FUNC_TRACE();

// 	if (reta_size != NDIS_HASH_INDCNT) {
// 		PMD_DRV_LOG(ERR, "Hash lookup table size does not match NDIS");
// 		return -EINVAL;
// 	}

// 	for (i = 0; i < NDIS_HASH_INDCNT; i++) {
// 		uint16_t idx = i / RTE_ETH_RETA_GROUP_SIZE;
// 		uint16_t shift = i % RTE_ETH_RETA_GROUP_SIZE;
// 		uint64_t mask = (uint64_t)1 << shift;

// 		if (reta_conf[idx].mask & mask)
// 			hv->rss_ind[i] = reta_conf[idx].reta[shift];
// 	}

// 	err = hn_rndis_conf_rss(hv, NDIS_RSS_FLAG_DISABLE);
// 	if (err) {
// 		PMD_DRV_LOG(NOTICE,
// 			"rss disable failed");
// 		return err;
// 	}

// 	err = hn_rndis_conf_rss(hv, 0);
// 	if (err) {
// 		PMD_DRV_LOG(NOTICE,
// 			    "reta reconfig failed");
// 		return err;
// 	}

// 	return hn_vf_reta_hash_update(dev, reta_conf, reta_size);
// }

// static int hn_rss_reta_query(struct rte_eth_dev *dev,
// 			     struct rte_eth_rss_reta_entry64 *reta_conf,
// 			     uint16_t reta_size)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	unsigned int i;

// 	PMD_INIT_FUNC_TRACE();

// 	if (reta_size != NDIS_HASH_INDCNT) {
// 		PMD_DRV_LOG(ERR, "Hash lookup table size does not match NDIS");
// 		return -EINVAL;
// 	}

// 	for (i = 0; i < NDIS_HASH_INDCNT; i++) {
// 		uint16_t idx = i / RTE_ETH_RETA_GROUP_SIZE;
// 		uint16_t shift = i % RTE_ETH_RETA_GROUP_SIZE;
// 		uint64_t mask = (uint64_t)1 << shift;

// 		if (reta_conf[idx].mask & mask)
// 			reta_conf[idx].reta[shift] = hv->rss_ind[i];
// 	}
// 	return 0;
// }

// static void hn_rss_hash_init(struct hn_data *hv,
// 			     const struct rte_eth_rss_conf *rss_conf)
// {
// 	/* Convert from DPDK RSS hash flags to NDIS hash flags */
// 	hv->rss_hash = NDIS_HASH_FUNCTION_TOEPLITZ;

// 	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV4)
// 		hv->rss_hash |= NDIS_HASH_IPV4;
// 	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV4_TCP)
// 		hv->rss_hash |= NDIS_HASH_TCP_IPV4;
// 	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6)
// 		hv->rss_hash |=  NDIS_HASH_IPV6;
// 	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6_EX)
// 		hv->rss_hash |=  NDIS_HASH_IPV6_EX;
// 	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV6_TCP)
// 		hv->rss_hash |= NDIS_HASH_TCP_IPV6;
// 	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6_TCP_EX)
// 		hv->rss_hash |= NDIS_HASH_TCP_IPV6_EX;

// 	memcpy(hv->rss_key, rss_conf->rss_key ? : rss_default_key,
// 	       NDIS_HASH_KEYSIZE_TOEPLITZ);
// }

// static int hn_rss_hash_update(struct rte_eth_dev *dev,
// 			      struct rte_eth_rss_conf *rss_conf)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	int err;

// 	PMD_INIT_FUNC_TRACE();

// 	err = hn_rndis_conf_rss(hv, NDIS_RSS_FLAG_DISABLE);
// 	if (err) {
// 		PMD_DRV_LOG(NOTICE,
// 			    "rss disable failed");
// 		return err;
// 	}

// 	hn_rss_hash_init(hv, rss_conf);

// 	if (rss_conf->rss_hf != 0) {
// 		err = hn_rndis_conf_rss(hv, 0);
// 		if (err) {
// 			PMD_DRV_LOG(NOTICE,
// 				    "rss reconfig failed (RSS disabled)");
// 			return err;
// 		}
// 	}

// 	return hn_vf_rss_hash_update(dev, rss_conf);
// }

// static int hn_rss_hash_conf_get(struct rte_eth_dev *dev,
// 				struct rte_eth_rss_conf *rss_conf)
// {
// 	struct hn_data *hv = dev->data->dev_private;

// 	PMD_INIT_FUNC_TRACE();

// 	if (hv->ndis_ver < NDIS_VERSION_6_20) {
// 		PMD_DRV_LOG(DEBUG, "RSS not supported on this host");
// 		return -EOPNOTSUPP;
// 	}

// 	rss_conf->rss_key_len = NDIS_HASH_KEYSIZE_TOEPLITZ;
// 	if (rss_conf->rss_key)
// 		memcpy(rss_conf->rss_key, hv->rss_key,
// 		       NDIS_HASH_KEYSIZE_TOEPLITZ);

// 	rss_conf->rss_hf = 0;
// 	if (hv->rss_hash & NDIS_HASH_IPV4)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_IPV4;

// 	if (hv->rss_hash & NDIS_HASH_TCP_IPV4)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_NONFRAG_IPV4_TCP;

// 	if (hv->rss_hash & NDIS_HASH_IPV6)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_IPV6;

// 	if (hv->rss_hash & NDIS_HASH_IPV6_EX)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_IPV6_EX;

// 	if (hv->rss_hash & NDIS_HASH_TCP_IPV6)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_NONFRAG_IPV6_TCP;

// 	if (hv->rss_hash & NDIS_HASH_TCP_IPV6_EX)
// 		rss_conf->rss_hf |= RTE_ETH_RSS_IPV6_TCP_EX;

// 	return 0;
// }

// static int
// hn_dev_promiscuous_enable(struct rte_eth_dev *dev)
// {
// 	struct hn_data *hv = dev->data->dev_private;

// 	hn_rndis_set_rxfilter(hv, NDIS_PACKET_TYPE_PROMISCUOUS);
// 	return hn_vf_promiscuous_enable(dev);
// }

// static int
// hn_dev_promiscuous_disable(struct rte_eth_dev *dev)
// {
// 	struct hn_data *hv = dev->data->dev_private;
// 	uint32_t filter;

// 	filter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
// 	if (dev->data->all_multicast)
// 		filter |= NDIS_PACKET_TYPE_ALL_MULTICAST;
// 	hn_rndis_set_rxfilter(hv, filter);
// 	return hn_vf_promiscuous_disable(dev);
// }

// static int
// hn_dev_allmulticast_enable(struct rte_eth_dev *dev)
// {
// 	struct hn_data *hv = dev->data->dev_private;

// 	hn_rndis_set_rxfilter(hv, NDIS_PACKET_TYPE_DIRECTED |
// 			      NDIS_PACKET_TYPE_ALL_MULTICAST |
// 			NDIS_PACKET_TYPE_BROADCAST);
// 	return hn_vf_allmulticast_enable(dev);
// }

// static int
// hn_dev_allmulticast_disable(struct rte_eth_dev *dev)
// {
// 	struct hn_data *hv = dev->data->dev_private;

// 	hn_rndis_set_rxfilter(hv, NDIS_PACKET_TYPE_DIRECTED |
// 			     NDIS_PACKET_TYPE_BROADCAST);
// 	return hn_vf_allmulticast_disable(dev);
// }

// static int
// hn_dev_mc_addr_list(struct rte_eth_dev *dev,
// 		     struct rte_ether_addr *mc_addr_set,
// 		     uint32_t nb_mc_addr)
// {
// 	/* No filtering on the synthetic path, but can do it on VF */
// 	return hn_vf_mc_addr_list(dev, mc_addr_set, nb_mc_addr);
// }

// /* Setup shared rx/tx queue data */
// static int hn_subchan_configure(struct hn_data *hv,
// 				uint32_t subchan)
// {
// 	struct vmbus_channel *primary = hn_primary_chan(hv);
// 	int err;
// 	unsigned int retry = 0;

// 	PMD_DRV_LOG(DEBUG,
// 		    "open %u subchannels", subchan);

// 	/* Send create sub channels command */
// 	err = hn_nvs_alloc_subchans(hv, &subchan);
// 	if (err)
// 		return  err;

// 	while (subchan > 0) {
// 		struct vmbus_channel *new_sc;
// 		uint16_t chn_index;

// 		err = rte_vmbus_subchan_open(primary, &new_sc);
// 		if (err == -ENOENT && ++retry < 1000) {
// 			/* This can happen if not ready yet */
// 			rte_delay_ms(10);
// 			continue;
// 		}

// 		if (err) {
// 			PMD_DRV_LOG(ERR,
// 				    "open subchannel failed: %d", err);
// 			return err;
// 		}

// 		rte_vmbus_set_latency(hv->vmbus, new_sc, hv->latency);

// 		retry = 0;
// 		chn_index = rte_vmbus_sub_channel_index(new_sc);
// 		if (chn_index == 0 || chn_index > hv->max_queues) {
// 			PMD_DRV_LOG(ERR,
// 				    "Invalid subchannel offermsg channel %u",
// 				    chn_index);
// 			return -EIO;
// 		}

// 		PMD_DRV_LOG(DEBUG, "new sub channel %u", chn_index);
// 		hv->channels[chn_index] = new_sc;
// 		--subchan;
// 	}

// 	return err;
// }

// static void netvsc_hotplug_retry(void *args)
// {
// 	int ret;
// 	struct hv_hotadd_context *hot_ctx = args;
// 	struct hn_data *hv = hot_ctx->hv;
// 	struct rte_eth_dev *dev = &rte_eth_devices[hv->port_id];
// 	struct rte_devargs *d = &hot_ctx->da;
// 	char buf[256];

// 	DIR *di;
// 	struct dirent *dir;
// 	struct ifreq req;
// 	struct rte_ether_addr eth_addr;
// 	int s;

// 	PMD_DRV_LOG(DEBUG, "%s: retry count %d",
// 		    __func__, hot_ctx->eal_hot_plug_retry);

// 	if (hot_ctx->eal_hot_plug_retry++ > NETVSC_MAX_HOTADD_RETRY) {
// 		PMD_DRV_LOG(NOTICE, "Failed to parse PCI device retry=%d",
// 			    hot_ctx->eal_hot_plug_retry);
// 		goto free_hotadd_ctx;
// 	}

// 	snprintf(buf, sizeof(buf), "/sys/bus/pci/devices/%s/net", d->name);
// 	di = opendir(buf);
// 	if (!di) {
// 		PMD_DRV_LOG(DEBUG, "%s: can't open directory %s, "
// 			    "retrying in 1 second", __func__, buf);
// 		goto retry;
// 	}

// 	while ((dir = readdir(di))) {
// 		/* Skip . and .. directories */
// 		if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
// 			continue;

// 		/* trying to get mac address if this is a network device*/
// 		s = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
// 		if (s == -1) {
// 			PMD_DRV_LOG(ERR, "Failed to create socket errno %d",
// 				    errno);
// 			break;
// 		}
// 		strlcpy(req.ifr_name, dir->d_name, sizeof(req.ifr_name));
// 		ret = ioctl(s, SIOCGIFHWADDR, &req);
// 		close(s);
// 		if (ret == -1) {
// 			PMD_DRV_LOG(ERR,
// 				    "Failed to send SIOCGIFHWADDR for device %s",
// 				    dir->d_name);
// 			break;
// 		}
// 		if (req.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
// 			closedir(di);
// 			goto free_hotadd_ctx;
// 		}
// 		memcpy(eth_addr.addr_bytes, req.ifr_hwaddr.sa_data,
// 		       RTE_DIM(eth_addr.addr_bytes));

// 		if (rte_is_same_ether_addr(&eth_addr, dev->data->mac_addrs)) {
// 			PMD_DRV_LOG(NOTICE,
// 				    "Found matching MAC address, adding device %s network name %s",
// 				    d->name, dir->d_name);

// 			/* If this device has been hot removed from this
// 			 * parent device, restore its args.
// 			 */
// 			ret = rte_eal_hotplug_add(d->bus->name, d->name,
// 						  hv->vf_devargs ?
// 						  hv->vf_devargs : "");
// 			if (ret) {
// 				PMD_DRV_LOG(ERR,
// 					    "Failed to add PCI device %s",
// 					    d->name);
// 				break;
// 			}
// 		}
// 		/* When the code reaches here, we either have already added
// 		 * the device, or its MAC address did not match.
// 		 */
// 		closedir(di);
// 		goto free_hotadd_ctx;
// 	}
// 	closedir(di);
// retry:
// 	/* The device is still being initialized, retry after 1 second */
// 	rte_eal_alarm_set(1000000, netvsc_hotplug_retry, hot_ctx);
// 	return;

// free_hotadd_ctx:
// 	rte_spinlock_lock(&hv->hotadd_lock);
// 	LIST_REMOVE(hot_ctx, list);
// 	rte_spinlock_unlock(&hv->hotadd_lock);

// 	rte_free(hot_ctx);
// }

// static void
// netvsc_hotadd_callback(const char *device_name, enum rte_dev_event_type type,
// 		       void *arg)
// {
// 	struct hn_data *hv = arg;
// 	struct hv_hotadd_context *hot_ctx;
// 	struct rte_devargs *d;
// 	int ret;

// 	PMD_DRV_LOG(INFO, "Device notification type=%d device_name=%s",
// 		    type, device_name);

// 	switch (type) {
// 	case RTE_DEV_EVENT_ADD:
// 		/* if we already has a VF, don't check on hot add */
// 		if (hv->vf_ctx.vf_state > vf_removed)
// 			break;

// 		hot_ctx = rte_zmalloc("NETVSC-HOTADD", sizeof(*hot_ctx),
// 				      rte_mem_page_size());

// 		if (!hot_ctx) {
// 			PMD_DRV_LOG(ERR, "Failed to allocate hotadd context");
// 			return;
// 		}

// 		hot_ctx->hv = hv;
// 		d = &hot_ctx->da;

// 		ret = rte_devargs_parse(d, device_name);
// 		if (ret) {
// 			PMD_DRV_LOG(ERR,
// 				    "devargs parsing failed ret=%d", ret);
// 			goto free_ctx;
// 		}

// 		if (!strcmp(d->bus->name, "pci")) {
// 			/* Start the process of figuring out if this
// 			 * PCI device is a VF device
// 			 */
// 			rte_spinlock_lock(&hv->hotadd_lock);
// 			LIST_INSERT_HEAD(&hv->hotadd_list, hot_ctx, list);
// 			rte_spinlock_unlock(&hv->hotadd_lock);
// 			rte_eal_alarm_set(1000000, netvsc_hotplug_retry, hot_ctx);
// 			return;
// 		}

// 		/* We will switch to VF on RDNIS configure message
// 		 * sent from VSP
// 		 */
// free_ctx:
// 		rte_free(hot_ctx);
// 		break;

// 	default:
// 		break;
// 	}
// }

// static int hn_dev_configure(struct rte_eth_dev *dev)
// {
// 	struct rte_eth_conf *dev_conf = &dev->data->dev_conf;
// 	struct rte_eth_rss_conf *rss_conf = &dev_conf->rx_adv_conf.rss_conf;
// 	const struct rte_eth_rxmode *rxmode = &dev_conf->rxmode;
// 	const struct rte_eth_txmode *txmode = &dev_conf->txmode;
// 	struct hn_data *hv = dev->data->dev_private;
// 	uint64_t unsupported;
// 	int i, err, subchan;

// 	PMD_INIT_FUNC_TRACE();

// 	if (dev_conf->rxmode.mq_mode & RTE_ETH_MQ_RX_RSS_FLAG)
// 		dev_conf->rxmode.offloads |= RTE_ETH_RX_OFFLOAD_RSS_HASH;

// 	unsupported = txmode->offloads & ~HN_TX_OFFLOAD_CAPS;
// 	if (unsupported) {
// 		PMD_DRV_LOG(NOTICE,
// 			    "unsupported TX offload: %#" PRIx64,
// 			    unsupported);
// 		return -EINVAL;
// 	}

// 	unsupported = rxmode->offloads & ~HN_RX_OFFLOAD_CAPS;
// 	if (unsupported) {
// 		PMD_DRV_LOG(NOTICE,
// 			    "unsupported RX offload: %#" PRIx64,
// 			    rxmode->offloads);
// 		return -EINVAL;
// 	}

// 	hv->vlan_strip = !!(rxmode->offloads & RTE_ETH_RX_OFFLOAD_VLAN_STRIP);

// 	err = hn_rndis_conf_offload(hv, txmode->offloads,
// 				    rxmode->offloads);
// 	if (err) {
// 		PMD_DRV_LOG(NOTICE,
// 			    "offload configure failed");
// 		return err;
// 	}

// 	hv->num_queues = RTE_MAX(dev->data->nb_rx_queues,
// 				 dev->data->nb_tx_queues);

// 	for (i = 0; i < NDIS_HASH_INDCNT; i++)
// 		hv->rss_ind[i] = i % dev->data->nb_rx_queues;

// 	hn_rss_hash_init(hv, rss_conf);

// 	subchan = hv->num_queues - 1;
// 	if (subchan > 0) {
// 		err = hn_subchan_configure(hv, subchan);
// 		if (err) {
// 			PMD_DRV_LOG(NOTICE,
// 				    "subchannel configuration failed");
// 			return err;
// 		}

// 		err = hn_rndis_conf_rss(hv, NDIS_RSS_FLAG_DISABLE);
// 		if (err) {
// 			PMD_DRV_LOG(NOTICE,
// 				"rss disable failed");
// 			return err;
// 		}

// 		if (rss_conf->rss_hf != 0) {
// 			err = hn_rndis_conf_rss(hv, 0);
// 			if (err) {
// 				PMD_DRV_LOG(NOTICE,
// 					    "initial RSS config failed");
// 				return err;
// 			}
// 		}
// 	}

// 	return hn_vf_configure_locked(dev, dev_conf);
// }

// static int hn_dev_stats_get(struct rte_eth_dev *dev,
// 			    struct rte_eth_stats *stats)
// {
// 	unsigned int i;

// 	hn_vf_stats_get(dev, stats);

// 	for (i = 0; i < dev->data->nb_tx_queues; i++) {
// 		const struct hn_tx_queue *txq = dev->data->tx_queues[i];

// 		if (!txq)
// 			continue;

// 		stats->opackets += txq->stats.packets;
// 		stats->obytes += txq->stats.bytes;
// 		stats->oerrors += txq->stats.errors;

// 		if (i < RTE_ETHDEV_QUEUE_STAT_CNTRS) {
// 			stats->q_opackets[i] = txq->stats.packets;
// 			stats->q_obytes[i] = txq->stats.bytes;
// 		}
// 	}

// 	for (i = 0; i < dev->data->nb_rx_queues; i++) {
// 		const struct hn_rx_queue *rxq = dev->data->rx_queues[i];

// 		if (!rxq)
// 			continue;

// 		stats->ipackets += rxq->stats.packets;
// 		stats->ibytes += rxq->stats.bytes;
// 		stats->ierrors += rxq->stats.errors;
// 		stats->imissed += rxq->stats.ring_full;

// 		if (i < RTE_ETHDEV_QUEUE_STAT_CNTRS) {
// 			stats->q_ipackets[i] = rxq->stats.packets;
// 			stats->q_ibytes[i] = rxq->stats.bytes;
// 		}
// 	}

// 	stats->rx_nombuf = dev->data->rx_mbuf_alloc_failed;
// 	return 0;
// }

// static int
// hn_dev_stats_reset(struct rte_eth_dev *dev)
// {
// 	unsigned int i;

// 	PMD_INIT_FUNC_TRACE();

// 	for (i = 0; i < dev->data->nb_tx_queues; i++) {
// 		struct hn_tx_queue *txq = dev->data->tx_queues[i];

// 		if (!txq)
// 			continue;
// 		memset(&txq->stats, 0, sizeof(struct hn_stats));
// 	}

// 	for (i = 0; i < dev->data->nb_rx_queues; i++) {
// 		struct hn_rx_queue *rxq = dev->data->rx_queues[i];

// 		if (!rxq)
// 			continue;

// 		memset(&rxq->stats, 0, sizeof(struct hn_stats));
// 	}

// 	return 0;
// }

// static int
// hn_dev_xstats_reset(struct rte_eth_dev *dev)
// {
// 	int ret;

// 	ret = hn_dev_stats_reset(dev);
// 	if (ret != 0)
// 		return 0;

// 	return hn_vf_xstats_reset(dev);
// }

// static int
// hn_dev_xstats_count(struct rte_eth_dev *dev)
// {
// 	int ret, count;

// 	count = dev->data->nb_tx_queues * RTE_DIM(hn_stat_strings);
// 	count += dev->data->nb_rx_queues * RTE_DIM(hn_stat_strings);

// 	ret = hn_vf_xstats_get_names(dev, NULL, 0);
// 	if (ret < 0)
// 		return ret;

// 	return count + ret;
// }

// static int
// hn_dev_xstats_get_names(struct rte_eth_dev *dev,
// 			struct rte_eth_xstat_name *xstats_names,
// 			unsigned int limit)
// {
// 	unsigned int i, t, count = 0;
// 	int ret;

// 	if (!xstats_names)
// 		return hn_dev_xstats_count(dev);

// 	/* Note: limit checked in rte_eth_xstats_names() */
// 	for (i = 0; i < dev->data->nb_tx_queues; i++) {
// 		const struct hn_tx_queue *txq = dev->data->tx_queues[i];

// 		if (!txq)
// 			continue;

// 		if (count >= limit)
// 			break;

// 		for (t = 0; t < RTE_DIM(hn_stat_strings); t++)
// 			snprintf(xstats_names[count++].name,
// 				 RTE_ETH_XSTATS_NAME_SIZE,
// 				 "tx_q%u_%s", i, hn_stat_strings[t].name);
// 	}

// 	for (i = 0; i < dev->data->nb_rx_queues; i++)  {
// 		const struct hn_rx_queue *rxq = dev->data->rx_queues[i];

// 		if (!rxq)
// 			continue;

// 		if (count >= limit)
// 			break;

// 		for (t = 0; t < RTE_DIM(hn_stat_strings); t++)
// 			snprintf(xstats_names[count++].name,
// 				 RTE_ETH_XSTATS_NAME_SIZE,
// 				 "rx_q%u_%s", i,
// 				 hn_stat_strings[t].name);
// 	}

// 	ret = hn_vf_xstats_get_names(dev, xstats_names + count,
// 				     limit - count);
// 	if (ret < 0)
// 		return ret;

// 	return count + ret;
// }

// static int
// hn_dev_xstats_get(struct rte_eth_dev *dev,
// 		  struct rte_eth_xstat *xstats,
// 		  unsigned int n)
// {
// 	unsigned int i, t, count = 0;
// 	const unsigned int nstats = hn_dev_xstats_count(dev);
// 	const char *stats;
// 	int ret;

// 	PMD_INIT_FUNC_TRACE();

// 	if (n < nstats)
// 		return nstats;

// 	for (i = 0; i < dev->data->nb_tx_queues; i++) {
// 		const struct hn_tx_queue *txq = dev->data->tx_queues[i];

// 		if (!txq)
// 			continue;

// 		stats = (const char *)&txq->stats;
// 		for (t = 0; t < RTE_DIM(hn_stat_strings); t++, count++) {
// 			xstats[count].id = count;
// 			xstats[count].value = *(const uint64_t *)
// 				(stats + hn_stat_strings[t].offset);
// 		}
// 	}

// 	for (i = 0; i < dev->data->nb_rx_queues; i++) {
// 		const struct hn_rx_queue *rxq = dev->data->rx_queues[i];

// 		if (!rxq)
// 			continue;

// 		stats = (const char *)&rxq->stats;
// 		for (t = 0; t < RTE_DIM(hn_stat_strings); t++, count++) {
// 			xstats[count].id = count;
// 			xstats[count].value = *(const uint64_t *)
// 				(stats + hn_stat_strings[t].offset);
// 		}
// 	}

// 	ret = hn_vf_xstats_get(dev, xstats, count, n);
// 	if (ret < 0)
// 		return ret;

// 	return count + ret;
// }

static int
hn_dev_start(struct uk_netdev *dev)
{
	struct hn_dev *hndev;
	// struct hn_data *hv = dev->data->dev_private;
	struct hn_data *hv;
	int error;
	uk_pr_info("[%s] enter\n", __func__);

	hndev = to_hn_dev(dev);
	hv = (struct hn_data *)hndev->dev_private;

//	PMD_INIT_FUNC_TRACE();

//	/* Register to monitor hot plug events */
//	error = rte_dev_event_callback_register(NULL, netvsc_hotadd_callback,
//						hv);
//	if (error) {
//		PMD_DRV_LOG(ERR, "failed to register device event callback");
//		return error;
//	}

	error = hn_rndis_set_rxfilter(hv,
				      NDIS_PACKET_TYPE_BROADCAST |
				      NDIS_PACKET_TYPE_ALL_MULTICAST |
				      NDIS_PACKET_TYPE_DIRECTED);
	uk_pr_info("[%s] error: %d\n", __func__, error);
	if (error)
		return error;

//	error = hn_vf_start(dev);
//	if (error)
//		hn_rndis_set_rxfilter(hv, 0);

	/* Initialize Link state */
//	if (error == 0)
//		hn_dev_link_update(dev, 0);

	uk_pr_info("[%s] end\n", __func__);
	return error;
}

//static int
//hn_dev_stop(struct rte_eth_dev *dev)
//{
//	struct hn_data *hv = dev->data->dev_private;

//	PMD_INIT_FUNC_TRACE();
//	dev->data->dev_started = 0;

//	rte_dev_event_callback_unregister(NULL, netvsc_hotadd_callback, hv);
//	hn_rndis_set_rxfilter(hv, 0);
//	return hn_vf_stop(dev);
//}

//static int
//hn_dev_close(struct rte_eth_dev *dev)
//{
//	int ret;
//	struct hn_data *hv = dev->data->dev_private;
//	struct hv_hotadd_context *hot_ctx;

//	PMD_INIT_FUNC_TRACE();
//	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
//		return 0;

//	rte_spinlock_lock(&hv->hotadd_lock);
//	while (!LIST_EMPTY(&hv->hotadd_list)) {
//		hot_ctx = LIST_FIRST(&hv->hotadd_list);
//		rte_eal_alarm_cancel(netvsc_hotplug_retry, hot_ctx);
//		LIST_REMOVE(hot_ctx, list);
//		rte_free(hot_ctx);
//	}
//	rte_spinlock_unlock(&hv->hotadd_lock);

//	ret = hn_vf_close(dev);
//	hn_dev_free_queues(dev);

//	return ret;
//}

//static const struct eth_dev_ops hn_eth_dev_ops = {
//	.dev_configure		= hn_dev_configure,
//	.dev_start		= hn_dev_start,
//	.dev_stop		= hn_dev_stop,
//	.dev_close		= hn_dev_close,
//	.dev_infos_get		= hn_dev_info_get,
//	.txq_info_get		= hn_dev_tx_queue_info,
//	.rxq_info_get		= hn_dev_rx_queue_info,
//	.dev_supported_ptypes_get = hn_vf_supported_ptypes,
//	.promiscuous_enable     = hn_dev_promiscuous_enable,
//	.promiscuous_disable    = hn_dev_promiscuous_disable,
//	.allmulticast_enable    = hn_dev_allmulticast_enable,
//	.allmulticast_disable   = hn_dev_allmulticast_disable,
//	.set_mc_addr_list	= hn_dev_mc_addr_list,
//	.reta_update		= hn_rss_reta_update,
//	.reta_query             = hn_rss_reta_query,
//	.rss_hash_update	= hn_rss_hash_update,
//	.rss_hash_conf_get      = hn_rss_hash_conf_get,
//	.tx_queue_setup		= hn_dev_tx_queue_setup,
//	.tx_queue_release	= hn_dev_tx_queue_release,
//	.tx_done_cleanup        = hn_dev_tx_done_cleanup,
//	.rx_queue_setup		= hn_dev_rx_queue_setup,
//	.rx_queue_release	= hn_dev_rx_queue_release,
//	.link_update		= hn_dev_link_update,
//	.stats_get		= hn_dev_stats_get,
//	.stats_reset            = hn_dev_stats_reset,
//	.xstats_get		= hn_dev_xstats_get,
//	.xstats_get_names	= hn_dev_xstats_get_names,
//	.xstats_reset		= hn_dev_xstats_reset,
//};

/*
 * Setup connection between PMD and kernel.
 */
static int
hn_attach(struct hn_data *hv, unsigned int mtu)
{
	int error;

	/* Attach NVS */
	error = hn_nvs_attach(hv, mtu);
	uk_pr_debug("[%s] hn_nvs_attach error: %d\n", __func__, error);
	if (error)
		goto failed_nvs;

	/* Attach RNDIS */
	error = hn_rndis_attach(hv);
	uk_pr_debug("[%s] hn_rndis_attach error: %d\n", __func__, error);
	if (error)
		goto failed_rndis;

	/*
	 * NOTE:
	 * Under certain conditions on certain versions of Hyper-V,
	 * the RNDIS rxfilter is _not_ zero on the hypervisor side
	 * after the successful RNDIS initialization.
	 */
	hn_rndis_set_rxfilter(hv, NDIS_PACKET_TYPE_NONE);
	uk_pr_debug("[%s] hn_rndis_set_rxfilter done\n", __func__);
	return 0;
failed_rndis:
	hn_nvs_detach(hv);
failed_nvs:
	return error;
}

// static void
// hn_detach(struct hn_data *hv)
// {
// 	hn_nvs_detach(hv);
// 	hn_rndis_detach(hv);
// }

// static int
// eth_hn_dev_init(struct rte_eth_dev *eth_dev)
static int
eth_hn_dev_init(struct uk_netdev *eth_dev)
{
// 	struct hn_data *hv = eth_dev->data->dev_private;
// 	struct rte_device *device = eth_dev->device;
// 	struct rte_vmbus_device *vmbus;
// 	unsigned int rxr_cnt;
	int err, max_chan;

// 	PMD_INIT_FUNC_TRACE();

// 	rte_spinlock_init(&hv->hotadd_lock);
// 	LIST_INIT(&hv->hotadd_list);

//	vmbus = container_of(device, struct rte_vmbus_device, device);
//	eth_dev->dev_ops = &hn_eth_dev_ops;
//	eth_dev->rx_queue_count = hn_dev_rx_queue_count;
//	eth_dev->rx_descriptor_status = hn_dev_rx_queue_status;
//	eth_dev->tx_descriptor_status = hn_dev_tx_descriptor_status;
//	eth_dev->tx_pkt_burst = &hn_xmit_pkts;
//	eth_dev->rx_pkt_burst = &hn_recv_pkts;

//	/*
//	 * for secondary processes, we don't initialize any further as primary
//	 * has already done this work.
//	 */
//	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
//		return 0;

//	eth_dev->data->dev_flags |= RTE_ETH_DEV_AUTOFILL_QUEUE_XSTATS;

//	/* Since Hyper-V only supports one MAC address */
//	eth_dev->data->mac_addrs = rte_calloc("hv_mac", HN_MAX_MAC_ADDRS,
//					      sizeof(struct rte_ether_addr), 0);
//	if (eth_dev->data->mac_addrs == NULL) {
//		PMD_INIT_LOG(ERR,
//			     "Failed to allocate memory store MAC addresses");
//		return -ENOMEM;
//	}

//	hv->vmbus = vmbus;
//	hv->rxbuf_res = &vmbus->resource[HV_RECV_BUF_MAP];
//	hv->chim_res  = &vmbus->resource[HV_SEND_BUF_MAP];
//	hv->port_id = eth_dev->data->port_id;
//	hv->latency = HN_CHAN_LATENCY_NS;
//	hv->rx_copybreak = HN_RXCOPY_THRESHOLD;
//	hv->tx_copybreak = HN_TXCOPY_THRESHOLD;
//	hv->rx_extmbuf_enable = HN_RX_EXTMBUF_ENABLE;
//	hv->max_queues = 1;

//	rte_rwlock_init(&hv->vf_lock);
//	hv->vf_ctx.vf_vsc_switched = false;
//	hv->vf_ctx.vf_vsp_reported = false;
//	hv->vf_ctx.vf_attached = false;
//	hv->vf_ctx.vf_state = vf_unknown;

//	err = hn_parse_args(eth_dev);
//	if (err)
//		return err;

//	strlcpy(hv->owner.name, eth_dev->device->name,
//		RTE_ETH_MAX_OWNER_NAME_LEN);
//	err = rte_eth_dev_owner_new(&hv->owner.id);
//	if (err) {
//		PMD_INIT_LOG(ERR, "Can not get owner id");
//		return err;
//	}

	/* Initialize primary channel input for control operations */
	// err = rte_vmbus_chan_open(vmbus, &hv->channels[0]);
	// if (err)
	// 	return err;

//	rte_vmbus_set_latency(hv->vmbus, hv->channels[0], hv->latency);

//	hv->primary = hn_rx_queue_alloc(hv, 0,
//					eth_dev->device->numa_node);

//	if (!hv->primary)
//		return -ENOMEM;

//	err = hn_attach(hv, RTE_ETHER_MTU);
//	if  (err)
//		goto failed;

//	err = hn_chim_init(eth_dev);
//	if (err)
//		goto failed;

//	err = hn_rndis_get_eaddr(hv, eth_dev->data->mac_addrs->addr_bytes);
//	if (err)
//		goto failed;

//	/* Multi queue requires later versions of windows server */
//	if (hv->nvs_ver < NVS_VERSION_5)
//		return 0;

//	max_chan = rte_vmbus_max_channels(vmbus);
//	PMD_INIT_LOG(DEBUG, "VMBus max channels %d", max_chan);
//	if (max_chan <= 0)
//		goto failed;

//	if (hn_rndis_query_rsscaps(hv, &rxr_cnt) != 0)
//		rxr_cnt = 1;

//	hv->max_queues = RTE_MIN(rxr_cnt, (unsigned int)max_chan);

//	/* If VF was reported but not added, do it now */
//	if (hv->vf_ctx.vf_vsp_reported && !hv->vf_ctx.vf_vsc_switched) {
//		PMD_INIT_LOG(DEBUG, "Adding VF device");

//		err = hn_vf_add(eth_dev, hv);
//	}

//	return 0;

failed:
//	PMD_INIT_LOG(NOTICE, "device init failed");
	uk_pr_debug("device init failed");

//	hn_chim_uninit(eth_dev);
//	hn_detach(hv);
	return err;
}

// static int
// eth_hn_dev_uninit(struct rte_eth_dev *eth_dev)
// {
// 	struct hn_data *hv = eth_dev->data->dev_private;
// 	int ret, ret_stop;

// 	PMD_INIT_FUNC_TRACE();

// 	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
// 		return 0;

// 	ret_stop = hn_dev_stop(eth_dev);
// 	hn_dev_close(eth_dev);

// 	free(hv->vf_devargs);
// 	hv->vf_devargs = NULL;

// 	hn_detach(hv);
// 	hn_chim_uninit(eth_dev);
// 	rte_vmbus_chan_close(hv->primary->chan);
// 	rte_free(hv->primary);
// 	ret = rte_eth_dev_owner_delete(hv->owner.id);
// 	if (ret != 0)
// 		return ret;

// 	return ret_stop;
// }

// static int eth_hn_probe(struct rte_vmbus_driver *drv __rte_unused,
// 			struct rte_vmbus_device *dev)
// {
// 	struct rte_eth_dev *eth_dev;
// 	int ret;

// 	PMD_INIT_FUNC_TRACE();

// 	ret = rte_dev_event_monitor_start();
// 	if (ret) {
// 		PMD_DRV_LOG(ERR, "Failed to start device event monitoring");
// 		return ret;
// 	}

// 	eth_dev = eth_dev_vmbus_allocate(dev, sizeof(struct hn_data));
// 	if (!eth_dev)
// 		return -ENOMEM;

// 	ret = eth_hn_dev_init(eth_dev);
// 	if (ret) {
// 		eth_dev_vmbus_release(eth_dev);
// 		rte_dev_event_monitor_stop();
// 	} else {
// 		rte_eth_dev_probing_finish(eth_dev);
// 	}

// 	return ret;
// }

// static int eth_hn_remove(struct rte_vmbus_device *dev)
// {
// 	struct rte_eth_dev *eth_dev;
// 	int ret;

// 	PMD_INIT_FUNC_TRACE();

// 	eth_dev = rte_eth_dev_allocated(dev->device.name);
// 	if (!eth_dev)
// 		return 0; /* port already released */

// 	ret = eth_hn_dev_uninit(eth_dev);
// 	if (ret)
// 		return ret;

// 	eth_dev_vmbus_release(eth_dev);
// 	rte_dev_event_monitor_stop();
// 	return 0;
// }

// /* Network device GUID */
// static const rte_uuid_t hn_net_ids[] = {
// 	/*  f8615163-df3e-46c5-913f-f2d2f965ed0e */
// 	RTE_UUID_INIT(0xf8615163, 0xdf3e, 0x46c5, 0x913f, 0xf2d2f965ed0eULL),
// 	{ 0 }
// };

// static struct rte_vmbus_driver rte_netvsc_pmd = {
// 	.id_table = hn_net_ids,
// 	.probe = eth_hn_probe,
// 	.remove = eth_hn_remove,
// };

// RTE_PMD_REGISTER_VMBUS(net_netvsc, rte_netvsc_pmd);
// RTE_PMD_REGISTER_KMOD_DEP(net_netvsc, "* uio_hv_generic");
// RTE_LOG_REGISTER_SUFFIX(hn_logtype_init, init, NOTICE);
// RTE_LOG_REGISTER_SUFFIX(hn_logtype_driver, driver, NOTICE);
// RTE_PMD_REGISTER_PARAM_STRING(net_netvsc,
// 			      NETVSC_ARG_LATENCY "=<uint32> "
// 			      NETVSC_ARG_RXBREAK "=<uint32> "
// 			      NETVSC_ARG_TXBREAK "=<uint32> "
// 			      NETVSC_ARG_RX_EXTMBUF_ENABLE "=<0|1>");

#define DRIVER_NAME  "netvsc"

static struct uk_alloc *drv_allocator;

static int hn_rxtx_alloc(struct hn_dev *hndev,
		const struct uk_netdev_conf *conf)
{
	int rc = 0, i;
	uk_pr_info("[%s] enter\n", __func__);

	if (conf->nb_tx_queues != conf->nb_rx_queues) {
		uk_pr_err("Different number of queues not supported\n");
		rc = -ENOTSUP;
		goto err_free_txrx;
	}

	hndev->max_queue_pairs =
		MIN(hndev->max_queue_pairs, conf->nb_tx_queues);

	hndev->txqs = uk_calloc(drv_allocator,
		hndev->max_queue_pairs, sizeof(*hndev->txqs));
	if (unlikely(!hndev->txqs)) {
		uk_pr_err("Failed to allocate memory for tx queues\n");
		rc = -ENOMEM;
		goto err_free_txrx;
	}
// 	for (i = 0; i < hndev->max_queue_pairs; i++)
// 		hndev->txqs[i].ring_size = NET_TX_RING_SIZE;

	hndev->rxqs = uk_calloc(drv_allocator,
		hndev->max_queue_pairs, sizeof(*hndev->rxqs));
	if (unlikely(!hndev->rxqs)) {
		uk_pr_err("Failed to allocate memory for rx queues\n");
		rc = -ENOMEM;
		goto err_free_txrx;
	}
// 	for (i = 0; i < hndev->max_queue_pairs; i++)
// 		hndev->rxqs[i].ring_size = NET_RX_RING_SIZE;

	return rc;

err_free_txrx:
	if (!hndev->rxqs)
		uk_free(drv_allocator, hndev->rxqs);
	if (!hndev->txqs)
		uk_free(drv_allocator, hndev->txqs);

	return rc;
}

static int
hn_create_tx_data(struct hn_data *hv, int ring_cnt)
{
// 	int i;

	/*
	 * Create TXBUF for chimney sending.
	 *
	 * NOTE: It is shared by all channels.
	 */
// 	sc->hn_chim = hyperv_dmamem_alloc(bus_get_dma_tag(sc->hn_dev),
// 	    PAGE_SIZE, 0, HN_CHIM_SIZE, &sc->hn_chim_dma,
// 	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
// 	if (sc->hn_chim == NULL) {
// 		device_printf(sc->hn_dev, "allocate txbuf failed\n");
// 		return (ENOMEM);
// 	}
	hv->chim_res.addr = hyperv_mem_alloc(hv->a, HN_CHIM_SIZE);
	if (hv->chim_res.addr == NULL) {
		uk_pr_err("allocate txbuf failed\n");
		return ENOMEM;
	}
	hv->chim_res.phys_addr = ukplat_virt_to_phys(hv->chim_res.addr);
	hv->chim_res.len = HN_CHIM_SIZE;

// 	sc->hn_tx_ring_cnt = ring_cnt;
// 	sc->hn_tx_ring_inuse = sc->hn_tx_ring_cnt;

// 	sc->hn_tx_ring = malloc(sizeof(struct hn_tx_ring) * sc->hn_tx_ring_cnt,
// 	    M_DEVBUF, M_WAITOK | M_ZERO);

// 	for (i = 0; i < sc->hn_tx_ring_cnt; ++i) {
// 		int error;

// 		error = hn_tx_ring_create(sc, i);
// 		if (error)
// 			return error;
// 	}

	return 0;
}

static struct uk_netdev_tx_queue *hn_dev_txq_setup(struct uk_netdev *n,
		uint16_t queue_id,
		uint16_t nb_desc __unused,
		struct uk_netdev_txqueue_conf *conf)
{
	int rc;
	struct hn_dev *hndev;
	struct hn_data *hv;
	struct uk_netdev_tx_queue *txq;
	// netif_tx_sring_t *sring;
	// int err = -ENOMEM;

	uk_pr_info("[%s] start queue_id: %d, nb_desc: %u\n", __func__, queue_id, nb_desc);

	UK_ASSERT(n != NULL);

	hndev = to_hn_dev(n);
	hv = hndev->dev_private;

	if (queue_id >= hndev->max_queue_pairs) {
		uk_pr_err("Invalid queue identifier: %"__PRIu16"\n", queue_id);
		return ERR2PTR(-EINVAL);
	}

	txq  = &hndev->txqs[queue_id];
	UK_ASSERT(!txq->initialized);
	txq->hn_dev = hndev;
	// TODO: Get the list of channels
	txq->chan = hv->channels[queue_id];
	txq->lqueue_id = queue_id;

	struct uk_netdev_info netdev_info;
	uk_netdev_info_get(n, &netdev_info);

	// txq->tx_rndis_pool = uk_allocpool_alloc(uk_alloc_get_default(), 16, sizeof(struct rndis_packet_msg),
    //                       HN_RNDIS_PKT_ALIGNED);
	txq->tx_rndis_pool = uk_allocpool_alloc(hv->a, 16, HN_RNDIS_PKT_ALIGNED,
                          HN_RNDIS_PKT_ALIGNED);
	if (txq->tx_rndis_pool == NULL) {
// 		PMD_DRV_LOG(ERR,
// 			    "mempool %s create failed: %d", name, rte_errno);
		uk_pr_err("tx_rndis mempool %d create failed\n", queue_id);
		goto error;
	}

	// txq->txdesc_pool = rte_mempool_create(name, nb_desc,
	// 				      sizeof(struct hn_txdesc),
	// 				      0, 0, NULL, NULL,
	// 				      hn_txd_init, txq,
	// 				      dev->device->numa_node, 0);
	txq->txdesc_pool = uk_allocpool_alloc(hv->a, 16, sizeof(struct hn_txdesc),
                          netdev_info.ioalign);
	if (txq->txdesc_pool == NULL) {
// 		PMD_DRV_LOG(ERR,
// 			    "mempool %s create failed: %d", name, rte_errno);
		uk_pr_err("txdesc_pool mempool %d create failed\n", queue_id);
		goto error;
	}

	txq->agg_szmax  = MIN(hv->chim_szmax, hv->rndis_agg_size);
	txq->agg_pktmax = hv->rndis_agg_pkts;
	txq->agg_align  = hv->rndis_agg_align;

	txq->initialized = true;
	hndev->txqs_num++;

	uk_pr_info("[%s] end\n", __func__);

	return txq;
error:
	return NULL;
}

static struct uk_netdev_rx_queue *hn_dev_rxq_setup(struct uk_netdev *n,
		uint16_t queue_id,
		uint16_t nb_desc __unused,
		struct uk_netdev_rxqueue_conf *conf)
{
	int rc;
	struct hn_dev *hndev;
	struct hn_data *hv;
	struct uk_netdev_rx_queue *rxq;
	// netif_rx_sring_t *sring;

	uk_pr_info("[%s] start queue_id: %u, nb_desc: %u\n", __func__, queue_id, nb_desc);

	UK_ASSERT(n != NULL);
	UK_ASSERT(conf != NULL);

	hndev = to_hn_dev(n);
	hv = hndev->dev_private;

	if (queue_id >= hndev->max_queue_pairs) {
		uk_pr_err("Invalid queue identifier: %"__PRIu16"\n", queue_id);
		return ERR2PTR(-EINVAL);
	}

	rxq = &hndev->rxqs[queue_id];

	UK_ASSERT(!rxq->initialized);
	rxq->hn_dev = hndev;
	rxq->lqueue_id = queue_id;

	// /* Setup shared ring */
	// sring = uk_palloc(conf->a, 1);
	// if (!sring)
	// 	return ERR2PTR(-ENOMEM);
	// memset(sring, 0, PAGE_SIZE);
	// SHARED_RING_INIT(sring);
	// FRONT_RING_INIT(&rxq->ring, sring, PAGE_SIZE);
	// rxq->ring_size = NET_RX_RING_SIZE;
	// rxq->ring_ref = gnttab_grant_access(nfdev->xendev->otherend_id,
	// 	virt_to_mfn(sring), 0);
	// UK_ASSERT(rxq->ring_ref != GRANT_INVALID_REF);

	// /* Setup event channel */
	// if (nfdev->split_evtchn || !nfdev->txqs[queue_id].initialized) {
	// 	rc = evtchn_alloc_unbound(nfdev->xendev->otherend_id,
	// 			netfront_rxq_handler, rxq,
	// 			&rxq->evtchn);
	// 	if (rc) {
	// 		uk_pr_err("Error creating event channel: %d\n", rc);
	// 		gnttab_end_access(rxq->ring_ref);
	// 		uk_pfree(conf->a, sring, 1);
	// 		return ERR2PTR(rc);
	// 	}
	// } else {
	// 	rxq->evtchn = nfdev->txqs[queue_id].evtchn;
	// 	/* overwriting event handler */
	// 	bind_evtchn(rxq->evtchn, netfront_rxq_handler, rxq);
	// }
	/*
	 * By default, events are disabled and it is up to the user or
	 * network stack to explicitly enable them.
	 */
	// mask_evtchn(rxq->evtchn);
	rxq->intr_enabled = 0;

	rxq->alloc_rxpkts = conf->alloc_rxpkts;
	rxq->alloc_rxpkts_argp = conf->alloc_rxpkts_argp;

	// for (uint16_t i = 0; i < NET_RX_RING_SIZE; i++)
	// 	rxq->gref[i] = GRANT_INVALID_REF;

	// /* Allocate receive buffers for this queue */
	// netfront_rx_fillup(rxq, rxq->ring_size);

	uk_spin_init(&rxq->ring_lock);
	rxq->event_sz = HN_RXQ_EVENT_DEFAULT;
	// rxq->event_buf = rte_malloc_socket("HN_EVENTS", HN_RXQ_EVENT_DEFAULT,
	// 				   RTE_CACHE_LINE_SIZE, socket_id);
	rxq->event_buf = uk_malloc(hv->a, HN_RXQ_EVENT_DEFAULT);
	if (!rxq->event_buf) {
		// rte_free(rxq);
		// uk_free(hv->a, rxq->event_buf);
		return NULL;
	}

	rxq->initialized = true;
	hndev->rxqs_num++;

	uk_pr_info("[%s] end\n", __func__);

	return rxq;
}

static int hn_dev_rx_intr_enable(struct uk_netdev *n __unused,
		struct uk_netdev_rx_queue *rxq)
{
	int rc;

	// TODO: PV - Enable interrupts
	uk_pr_info("[%s] start\n", __func__);

	UK_ASSERT(n != NULL);
	UK_ASSERT(rxq != NULL);
	// UK_ASSERT(&rxq->hn_dev->netdev == n);

	// /* If the interrupt is enabled */
	// if (rxq->intr_enabled & NETFRONT_INTR_EN)
	// 	return 0;

	// /**
	//  * Enable the user configuration bit. This would cause the interrupt to
	//  * be enabled automatically if the interrupt could not be enabled now
	//  * due to data in the queue.
	//  */
	rxq->intr_enabled = HN_INTR_USR_EN;
	// rc = hn_rxq_intr_enable(rxq);
	// if (!rc)
	rxq->intr_enabled |= HN_INTR_EN;

	// return rc;

	uk_pr_info("[%s] end\n", __func__);
	return 0;
}

static int hn_dev_rx_intr_disable(struct uk_netdev *n __unused,
		struct uk_netdev_rx_queue *rxq)
{
	uk_pr_info("[%s] start\n", __func__);

	// TODO: PV - Disable interrupts
	UK_ASSERT(n != NULL);
	UK_ASSERT(rxq != NULL);
	// UK_ASSERT(&rxq->hn_dev->netdev == n);

	// rxq->intr_enabled &= ~(NETFRONT_INTR_USR_EN | NETFRONT_INTR_EN);
	// mask_evtchn(rxq->evtchn);
	rxq->intr_enabled = 0;

	uk_pr_info("[%s] end\n", __func__);
	return 0;
}

static int hn_dev_txq_info_get(struct uk_netdev *n,
		uint16_t queue_id,
		struct uk_netdev_queue_info *qinfo)
{
	struct hn_dev *hndev;
	struct uk_netdev_tx_queue *txq;
	int rc = 0;

	uk_pr_info("[%s] start\n", __func__);

	UK_ASSERT(n != NULL);
	UK_ASSERT(qinfo != NULL);

	// TODO: PV - implemented this function
	// hndev = to_hn_dev(n);
	// if (unlikely(queue_id >= hndev->max_queue_pairs)) {
	// 	uk_pr_err("Invalid queue_id %"__PRIu16"\n", queue_id);
	// 	rc = -EINVAL;
	// 	goto exit;
	// }
	// txq = &hndev->txqs[queue_id];
	// qinfo->nb_min = txq->ring_size;
	// qinfo->nb_max = txq->ring_size;
	// qinfo->nb_align = 1;
	// qinfo->nb_is_power_of_two = 1;

exit:
	uk_pr_info("[%s] end\n", __func__);
	return rc;
}

static int hn_dev_rxq_info_get(struct uk_netdev *n,
		uint16_t queue_id,
		struct uk_netdev_queue_info *qinfo)
{
	struct hn_dev *hndev;
	struct uk_netdev_rx_queue *rxq;
	int rc = 0;

	uk_pr_info("[%s] start\n", __func__);

	UK_ASSERT(n != NULL);
	UK_ASSERT(qinfo != NULL);

	// TODO: PV - Implement this functions
	// hndev = to_hn_dev(n);
	// if (unlikely(queue_id >= hndev->max_queue_pairs)) {
	// 	uk_pr_err("Invalid queue id: %"__PRIu16"\n", queue_id);
	// 	rc = -EINVAL;
	// 	goto exit;
	// }
	// rxq = &hndev->rxqs[queue_id];
	// qinfo->nb_min = rxq->ring_size;
	// qinfo->nb_max = rxq->ring_size;
	// qinfo->nb_align = 1;
	// qinfo->nb_is_power_of_two = 1;

exit:
	uk_pr_info("[%s] enter\n", __func__);
	return rc;
}

static int hn_dev_configure(struct uk_netdev *n,
		const struct uk_netdev_conf *conf)
{
	int rc;
	struct hn_dev *hndev;
	struct hn_data *hv;
	
	uk_pr_info("[%s] start\n", __func__);

	UK_ASSERT(n != NULL);
	UK_ASSERT(conf != NULL);

	hndev = to_hn_dev(n);
	hv = (struct hn_data *)hndev->dev_private;

	rc = hn_rxtx_alloc(hndev, conf);
	if (rc != 0) {
		uk_pr_err("Failed to allocate rx and tx rings %d\n", rc);
		goto out;
	}

	hn_create_tx_data(hndev->dev_private, 1);

	uk_pr_debug("[%s] Before get_eaddr\n", __func__);
 	rc = hn_rndis_get_eaddr(hv, hndev->hw_addr.addr_bytes);
	if (rc) {
		uk_pr_info(
			    "get eaddr failed: %d", rc);
		goto out;
	}
	uk_pr_debug("[%s] After get_eaddr\n", __func__);

	rc = hn_rndis_conf_offload(hv, 0, 0);
	if (rc) {
		uk_pr_info(
			    "offload configure failed: %d\n", rc);
		goto out;
	}

out:
	uk_pr_info("[%s] end\n", __func__);
	return rc;
}

static void hn_dev_info_get(struct uk_netdev *n,
		struct uk_netdev_info *dev_info)
{
	struct hn_dev *hndev;

	uk_pr_info("[%s] start\n", __func__);

	UK_ASSERT(n != NULL);
	UK_ASSERT(dev_info != NULL);

	hndev = to_hn_dev(n);
	dev_info->max_rx_queues = hndev->max_queue_pairs;
	dev_info->max_tx_queues = hndev->max_queue_pairs;
	dev_info->max_mtu = hndev->mtu;
	dev_info->nb_encap_tx = 0;
	dev_info->nb_encap_rx = 0;
	dev_info->ioalign = PAGE_SIZE;
	dev_info->features = UK_NETDEV_F_RXQ_INTR | UK_NETDEV_F_PARTIAL_CSUM;

	uk_pr_info("[%s] end\n", __func__);
}

static const void *hn_dev_einfo_get(struct uk_netdev *n,
		enum uk_netdev_einfo_type einfo_type)
{
	struct hn_dev *hndev;

	uk_pr_info("[%s] start einfo_type: %d\n", __func__, einfo_type);

	UK_ASSERT(n != NULL);

	// TODO: PV - implement this function
	// hndev = to_hn_dev(n);
	// switch (einfo_type) {
	// case UK_NETDEV_IPV4_ADDR_STR:
	// 	return hndev->econf.ipv4addr;
	// case UK_NETDEV_IPV4_MASK_STR:
	// 	return hndev->econf.ipv4mask;
	// case UK_NETDEV_IPV4_GW_STR:
	// 	return hndev->econf.ipv4gw;
	// default:
	// 	break;
	// }

	/* type not supported */
	uk_pr_info("[%s] end\n", __func__);
	return NULL;
}

static const struct uk_hwaddr *hn_dev_mac_get(struct uk_netdev *n)
{
	struct hn_dev *hndev;

	UK_ASSERT(n != NULL);

	hndev = to_hn_dev(n);
	uk_pr_info("[%s] enter hw_addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
			__func__,
			hndev->hw_addr.addr_bytes[0], hndev->hw_addr.addr_bytes[1],
			hndev->hw_addr.addr_bytes[2], hndev->hw_addr.addr_bytes[3],
			hndev->hw_addr.addr_bytes[4], hndev->hw_addr.addr_bytes[5]);
	return &hndev->hw_addr;
}

static uint16_t hn_dev_mtu_get(struct uk_netdev *n)
{
	struct hn_dev *hndev;

	UK_ASSERT(n != NULL);

	hndev = to_hn_dev(n);
	uk_pr_info("[%s] enter hndev->mtu: %u\n", __func__, hndev->mtu);
	return hndev->mtu;
}

static unsigned int hn_dev_promisc_get(struct uk_netdev *n)
{
	struct hn_dev *hndev;

	UK_ASSERT(n != NULL);
	uk_pr_info("[%s] enter\n", __func__);
	hndev = to_hn_dev(n);
	return hndev->promisc;
}

static int hn_dev_probe(struct uk_netdev *n)
{
	struct hn_dev *hndev;
	int rc;

	UK_ASSERT(n != NULL);

	uk_pr_info("[%s] start\n", __func__);

	hndev = to_hn_dev(n);
	rc = 0;
	// /* Xenbus initialization */
	// rc = netfront_xb_init(nfdev, drv_allocator);
	// if (rc) {
	// 	uk_pr_err("Error initializing Xenbus data: %d\n", rc);
	// 	goto out;
	// }

out:
	uk_pr_info("[%s] end\n", __func__);
	return rc;	
}

static const struct uk_netdev_ops hn_ops = {
	.probe = hn_dev_probe,
	.configure = hn_dev_configure,
	.start = hn_dev_start,
	.txq_configure = hn_dev_txq_setup,
	.rxq_configure = hn_dev_rxq_setup,
	.rxq_intr_enable = hn_dev_rx_intr_enable,
	.rxq_intr_disable = hn_dev_rx_intr_disable,
	.txq_info_get = hn_dev_txq_info_get,
	.rxq_info_get = hn_dev_rxq_info_get,
	.info_get = hn_dev_info_get,
	.einfo_get = hn_dev_einfo_get,
	.hwaddr_get = hn_dev_mac_get,
	.mtu_get = hn_dev_mtu_get,
	.promiscuous_get = hn_dev_promisc_get,
};

static int
hn_chan_attach(struct hn_dev *hndev, struct hn_data *hv, struct vmbus_channel *chan)
{
	struct vmbus_chan_br cbr;
	//struct hn_rx_ring *rxr;
	//struct hn_tx_ring *txr = NULL;
	int idx, error;

	uk_pr_debug("[%s] start\n", __func__);

	//idx = vmbus_chan_subidx(chan);
	idx = 0;

	/*
	 * Link this channel to RX/TX ring.
	 */
	//KASSERT(idx >= 0 && idx < sc->hn_rx_ring_inuse,
	//    ("invalid channel index %d, should > 0 && < %d",
	//     idx, sc->hn_rx_ring_inuse));
	//rxr = &hv->hn_rx_ring[idx];
	//KASSERT((rxr->hn_rx_flags & HN_RX_FLAG_ATTACHED) == 0,
	//    ("RX ring %d already attached", idx));
	//rxr->hn_rx_flags |= HN_RX_FLAG_ATTACHED;
	//rxr->hn_chan = chan;

	//if (bootverbose) {
		// if_printf(sc->hn_ifp, "link RX ring %d to chan%u\n",
		//     idx, vmbus_chan_id(chan));
		uk_pr_debug("link RX ring %d to chan%u\n",
		    idx, vmbus_chan_id(chan));
	//}

// 	if (idx < sc->hn_tx_ring_inuse) {
// 		txr = &sc->hn_tx_ring[idx];
// 		KASSERT((txr->hn_tx_flags & HN_TX_FLAG_ATTACHED) == 0,
// 		    ("TX ring %d already attached", idx));
// 		txr->hn_tx_flags |= HN_TX_FLAG_ATTACHED;

// 		txr->hn_chan = chan;
// 		if (bootverbose) {
 			// if_printf(sc->hn_ifp, "link TX ring %d to chan%u\n",
 			//     idx, vmbus_chan_id(chan));
 			uk_pr_info("link TX ring %d to chan%u\n",
 			    idx, vmbus_chan_id(chan));
// 		}
// 	}

// 	/* Bind this channel to a proper CPU. */
// 	vmbus_chan_cpu_set(chan, HN_RING_IDX2CPU(sc, idx));

	// rxr->hn_br = hyperv_dmamem_alloc(bus_get_dma_tag(dev),
	// 	PAGE_SIZE, 0, HN_TXBR_SIZE + HN_RXBR_SIZE,
	// 	&rxr->hn_br_dma, BUS_DMA_WAITOK);
	hndev->br_res[idx].addr = hyperv_mem_alloc(hv->a, HN_TXBR_SIZE + HN_RXBR_SIZE);
	if (hndev->br_res[idx].addr == NULL) {
		uk_pr_err("allocate bufring failed\n");
		return ENOMEM;
	}
	hndev->br_res[idx].phys_addr = ukplat_virt_to_phys(hndev->br_res[idx].addr);
	hndev->br_res[idx].len = HN_TXBR_SIZE + HN_RXBR_SIZE;
	// uk_pr_debug("addr: %p, paddr: %p\n", hndev->br_res[idx].addr, hndev->br_res[idx].phys_addr);

	/*
	 * Open this channel
	 */
	// cbr.cbr = rxr->hn_br;
	cbr.cbr = hndev->br_res[idx].addr;
	// cbr.cbr_paddr = rxr->hn_br_paddr;
	cbr.cbr_paddr = hndev->br_res[idx].phys_addr;
	cbr.cbr_txsz = HN_TXBR_SIZE;
	cbr.cbr_rxsz = HN_RXBR_SIZE;
	// error = vmbus_chan_open_br(chan, &cbr, NULL, 0, hn_chan_callback, rxr);
	error = vmbus_chan_open_br(chan, &cbr, NULL, 0, NULL, NULL);
	if (error) {
		if (error == EISCONN) {
			// if_printf(sc->hn_ifp, "bufring is connected after "
			//     "chan%u open failure\n", vmbus_chan_id(chan));
			uk_pr_info("bufring is connected after "
			    "chan%u open failure\n", vmbus_chan_id(chan));
			// rxr->hn_rx_flags |= HN_RX_FLAG_BR_REF;
		} else {
			// if_printf(sc->hn_ifp, "open chan%u failed: %d\n",
			//     vmbus_chan_id(chan), error);
			uk_pr_info("open chan%u failed: %d\n",
			    vmbus_chan_id(chan), error);
		}
	}

	hndev->br_res[idx].phys_addr = chan->ch_bufring_gpadl;

	uk_pr_debug("[%s] end error: %d\n", __func__, error);

	return (error);
}

static int hn_drv_add_dev(struct vmbus_device *vmbusdev)
{
	uk_pr_debug("[%s] enter\n", __func__);

	struct hn_dev *hndev;
	struct hn_data *hv;
	int rc = 0;
	int err = 0;

	UK_ASSERT(vmbusdev != NULL);

	// eth_dev = eth_dev_vmbus_allocate(dev, sizeof(struct hn_data));
	// if (!eth_dev)
	// 	return -ENOMEM;

	hndev = uk_calloc(drv_allocator, 1, sizeof(*hndev));
	if (!hndev) {
			uk_pr_info("[%s] error uk_alloc\n", __func__);
		rc = -ENOMEM;
		goto err_out;
	}

	hndev->dev_private = uk_calloc(drv_allocator, 1, sizeof(struct hn_data));
	if (!hndev->dev_private) {
		uk_pr_info("[%s] error uk_calloc hn_data\n", __func__);
		rc = -ENOMEM;
		goto err_out;
	}
	hv = (struct hn_data *)hndev->dev_private;
	hv->a = drv_allocator;
	hv->owner = hndev;
	
	hndev->vmbusdev = vmbusdev;
	hndev->mtu = UK_ETH_PAYLOAD_MAXLEN;
	hndev->max_queue_pairs = 1;
	hndev->netdev.tx_one = hn_xmit;
	hndev->netdev.rx_one = hn_recv;
	hndev->netdev.ops = &hn_ops;

	hv->primary = uk_calloc(drv_allocator,
		1, sizeof(*hv->primary));
	if (unlikely(!hv->primary)) {
		uk_pr_err("[%s] Failed to allocate memory for primary Rx queue\n", __func__);
		rc = -ENOMEM;
	}
	hv->channels[0] = (struct vmbus_channel *)vmbusdev->priv;
	hv->primary->chan = hv->channels[0];
	hv->primary->hv = hv;

	// uk_pr_info("[%s] hv->primary: %p, hv->channels[0]: %p\n", __func__, hv->primary, hv->channels[0]);

	err = hn_chan_attach(hndev, hv, hv->channels[0]);
	// uk_pr_debug("[%s] hn_chan_attach err: %d\n", __func__, err);

	hv->rxbuf_res = uk_calloc(drv_allocator, 1, sizeof(*hv->rxbuf_res));
	if (!hv->rxbuf_res) {
		uk_pr_info("[%s] error uk_calloc rxbuf_res\n", __func__);
		rc = -ENOMEM;
	}

	hv->rxbuf_res->addr = hyperv_mem_alloc(hv->a, HN_RXBUF_SIZE);
	if (hv->rxbuf_res->addr == NULL) {
		uk_pr_err("[%s] allocate bufring failed\n", __func__);
		return -ENOMEM;
	}
	hv->rxbuf_res->phys_addr = ukplat_virt_to_phys(hv->rxbuf_res->addr);
	hv->rxbuf_res->len = HN_RXBUF_SIZE;
	// uk_pr_info("[%s] addr: %p, paddr: %p\n", __func__, hv->rxbuf_res->addr, hv->rxbuf_res->phys_addr);

	/*
		* Connect the RXBUF GPADL to the primary channel.
		*
		* NOTE:
		* Only primary channel has RXBUF connected to it.  Sub-channels
		* just share this RXBUF.
		*/
	uint32_t rxbuf_gpadl = 0;
	err = vmbus_chan_gpadl_connect(hv->channels[0],
		hv->rxbuf_res->phys_addr, HN_RXBUF_SIZE, &rxbuf_gpadl);
	if (err) {
			uk_pr_err("rxbuf gpadl conn failed: %d\n",
				err);
			// goto cleanup;
	}
	hv->rxbuf_res->phys_addr = rxbuf_gpadl;

	// uk_pr_debug("[%s] after vmbus_chan_gpadl_connect\n", __func__);

	/* START From FreeBSD */

	/*
	 * Connect chimney sending buffer GPADL to the primary channel.
	 *
	 * NOTE:
	 * Only primary channel has chimney sending buffer connected to it.
	 * Sub-channels just share this chimney sending buffer.
	 */
	hv->chim_res.addr = hyperv_mem_alloc(hv->a, HN_CHIM_SIZE);
	if (hv->chim_res.addr == NULL) {
		uk_pr_err("[%s] allocate bufring failed\n", __func__);
		return -ENOMEM;
	}
	hv->chim_res.phys_addr = ukplat_virt_to_phys(hv->chim_res.addr);
	hv->chim_res.len = HN_RXBUF_SIZE;
	// uk_pr_debug("[%s] chim_res.addr: %p, chim_res.paddr: %p\n", __func__, hv->chim_res.addr, hv->chim_res.phys_addr);

	uint32_t chim_gpadl = 0;
	err = vmbus_chan_gpadl_connect(hv->channels[0],
  	    hv->chim_res.phys_addr, HN_CHIM_SIZE, &chim_gpadl);
	if (err) {
		uk_pr_err("chim gpadl conn failed: %d\n", err);
		// goto cleanup;
	}

	hv->chim_res.phys_addr = chim_gpadl;
	/* END */

	err = hn_attach(hv, RTE_ETHER_MTU);
	// uk_pr_info("[%s] hn_attach err: %d\n", __func__, err);
	if  (err)
		goto failed;

	err = hn_chim_init(&hndev->netdev);
	if (err)
		goto failed;

	//	err = hn_rndis_get_eaddr(hv, eth_dev->data->mac_addrs->addr_bytes);
	//	if (err)
	//		goto failed;

	err = hn_rndis_get_eaddr(hv, hndev->hw_addr.addr_bytes);
	if (err)
		goto failed;

	//uk_pr_debug("[hn_drv_add_dev] Before offload 1\n");
	//err = hn_rndis_conf_offload(hv, 0, 0);
	//if (err) {
	//	uk_pr_info(
	//		    "offload configure failed: %d", err);
	//}

	//uk_pr_debug("[hn_drv_add_dev] Before offload 2\n");
	//err = hn_rndis_conf_offload(hv, 0, 0);
	//if (err) {
	//	uk_pr_info(
	//		    "offload configure failed: %d", err);
	//}

	// uk_pr_info("%s] uk_alloc_availmem: %d, uk_alloc_pavailmem: %d\n", __func__, uk_alloc_availmem(drv_allocator), uk_alloc_pavailmem(drv_allocator));

	rc = uk_netdev_drv_register(&hndev->netdev, drv_allocator, DRIVER_NAME);
	if (rc < 0) {
		uk_pr_err("Failed to register %s device with libuknetdev\n",
			DRIVER_NAME);
		goto err_register;
	}

	// uk_pr_info("[%s] Before offload 3\n", __func__);
	err = hn_rndis_conf_offload(hv, 0, 0);
	if (err) {
		uk_pr_info(
			    "offload configure failed: %d", err);
		//goto out;
	}
	// uk_pr_info("[%s] After offload\n", __func__);

	hndev->uid = rc;	
	rc = 0;

	uk_pr_debug("[%s] end\n", __func__);
out:
	return rc;
err_register:
	uk_free(drv_allocator, hndev);
failed:
err_out:
	goto out;
}

static int hn_drv_init(struct uk_alloc *allocator)
{
	uk_pr_info("[%s] enter\n", __func__);

	/* driver initialization */
	if (!allocator)
		return -EINVAL;

	drv_allocator = allocator;

	return 0;
}

static const struct hyperv_guid	hn_guid = {
	.hv_guid = {
	    0x63, 0x51, 0x61, 0xf8, 0x3e, 0xdf, 0xc5, 0x46,
	    0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e }
};

static struct vmbus_driver hn_driver = {
	.guid         = &hn_guid,
	.init         = hn_drv_init,
	.add_dev      = hn_drv_add_dev
};
VMBUS_REGISTER_DRIVER(&hn_driver);
