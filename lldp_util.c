/*******************************************************************************

  LLDP Agent Daemon (LLDPAD) Software
  Copyright(c) 2007-2010 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-eedc Mailing List <e1000-eedc@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_bonding.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/wireless.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <dirent.h>
#include "lldp.h"
#include "lldp_util.h"
#include "messages.h"
#include "drv_cfg.h"


int is_valid_lldp_device(const char *device_name)
{
	if (is_loopback(device_name))
		return 0;
	if (is_vlan(device_name))
		return 0;
	if (is_bridge(device_name))
		return 0;
	return 1;
}

/**
 *	is_bond - check if interface is a bond interface
 *	@ifname: name of the interface
 *
 *	Returns 0 if ifname is not a bond, 1 if it is a bond.
 */
int is_bond(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	struct ifreq ifr;
	ifbond ifb;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, ifname);
		memset(&ifb, 0, sizeof(ifb));
		ifr.ifr_data = (caddr_t)&ifb;
		if (ioctl(fd, SIOCBONDINFOQUERY, &ifr) == 0)
			rc = 1;
		close(fd);
	} else {
		perror("is_bond() socket create failed");
	}
	return rc;
}

/**
 *	is_san_mac - check if address is a san mac addr
 *	@addr: san mac address
 *
 *	Returns 0 if addr is NOT san mac, 1 if it is a san mac.
 *
 *	BUG: this does not do anything to prove it's a sanmac!!!!
 *	SAN MAC is no different than LAN MAC, no way to tell!!!
 */
int is_san_mac(u8 *addr)
{
	int i; 

	for ( i = 0; i < ETH_ALEN; i++) {
		if ( addr[i]!= 0xff )
			return 1;
	}
	return 0;
}

/**
 *	get_src_mac_from_bond - select a source MAC to use for slave
 *	@bond_port: pointer to port structure for a bond interface
 *	@ifname: interface name of the slave port
 *	@addr: address of buffer in which to return the selected MAC address
 *
 *	Checks to see if ifname is a slave of the bond port.  If it is,
 *	then a 
 *	Returns 0 if a source MAC from the bond could not be found. 1 is
 *	returned if the slave was found in the bond.  addr is updated with
 *	the source MAC that should be used.
*/
int	get_src_mac_from_bond(struct port *bond_port, char *ifname, u8 *addr)
{
	int fd;
	struct ifreq ifr;
	ifbond ifb;
	ifslave ifs;
	char act_ifname[IFNAMSIZ];
	unsigned char bond_mac[ETH_ALEN], san_mac[ETH_ALEN];
	int found = 0;
	int i;

	fd = socket(PF_INET, SOCK_DGRAM, 0);

	if (fd <= 0) {
		perror("get_src_mac_from_bond failed");
		return 0;
	}

	memset(bond_mac, 0, sizeof(bond_mac));
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, bond_port->ifname);
	memset(&ifb, 0, sizeof(ifb));
	ifr.ifr_data = (caddr_t)&ifb;
	if (ioctl(fd,SIOCBONDINFOQUERY, &ifr) == 0) {
		/* get the MAC address for the current bond port */
		if (ioctl(fd,SIOCGIFHWADDR, &ifr) == 0)
			memcpy(bond_mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
		else
			perror("error getting bond MAC address");

		/* scan the bond's slave ports and looking for the
		 * current port and the active slave port.
		*/
		memset(act_ifname, 0, sizeof(act_ifname));
		for (i = 0; i < ifb.num_slaves; i++) {
			memset(&ifs, 0, sizeof(ifs));
			ifs.slave_id = i;
			ifr.ifr_data = (caddr_t)&ifs;

			if (ioctl(fd,SIOCBONDSLAVEINFOQUERY, &ifr) == 0) {
				if (!strncmp(ifs.slave_name, ifname,
					IFNAMSIZ))
					found = 1;

				if (ifs.state == BOND_STATE_ACTIVE)
					strncpy(act_ifname, ifs.slave_name,
						IFNAMSIZ);
			}
		}
	}

	/* current port is not a slave of the bond */
	if (!found) {
		close(fd);
		return 0;
	}

	/* Get slave port's current perm MAC address
	 * This will be the default return value
	*/
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, ifname);
	if (ioctl(fd,SIOCGIFHWADDR, &ifr) == 0) {
		memcpy(addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	}
	else {
		perror("error getting slave MAC address");
		close(fd);
		return 0;
	}

	switch (ifb.bond_mode) {
	case BOND_MODE_ACTIVEBACKUP:
		/* If current port is not the active slave, then 
		 * if the bond MAC is equal to the port's
		 * permanent MAC, then find and return
		 * the permanent MAC of the active
		 * slave port. Otherwise, return the
		 * permanent MAC of the port.
		*/
		if (strncmp(ifname, act_ifname, IFNAMSIZ))
			if (get_perm_hwaddr(ifname, addr, san_mac) == 0)
				if (!memcmp(bond_mac, addr, ETH_ALEN))
					get_perm_hwaddr(act_ifname, addr, 
								san_mac);
		break;
	default:
		/* Use the current MAC of the port */
		break;
	}

	close(fd);

	return 1;
}

int is_valid_mac(const u8 *mac)
{
	return !!(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]);
}

int read_int(const char *path)
{
	int rc = -1;
	char buf[256];
	FILE *f = fopen(path, "r");

	if (f) {
		if (fgets(buf, sizeof(buf), f))
			rc = atoi(buf);
		fclose(f);
	}
	return rc;
}

int read_bool(const char *path)
{
	return read_int(path) > 0;
}

int get_ifflags(const char *ifname)
{
	int fd = -1;
	int flags = 0;
	struct ifreq ifr;

	/* use ioctl */	
	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
			flags = ifr.ifr_flags;
		} else {
		}
		close(fd);
	}
	return flags;
}

int get_ifpflags(const char *ifname)
{
	int fd = -1;
	int flags = 0;
	struct ifreq ifr;

	/* use ioctl */	
	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFPFLAGS, &ifr) == 0) {
			flags = ifr.ifr_flags;
		} else {
		}
		close(fd);
	}
	return flags;
}

int get_iftype(const char *ifname)
{
	char path[256];

	snprintf(path, sizeof(path), "/sys/class/net/%s/type", ifname);
	return read_int(path);
}

int get_ifindex(const char *ifname)
{
	char path[256];

	snprintf(path, sizeof(path), "/sys/class/net/%s/ifindex", ifname);
	return read_int(path);

}

int get_iffeatures(const char *ifname)
{
	char path[256];

	snprintf(path, sizeof(path), "/sys/class/net/%s/features", ifname);
	return read_int(path);

}

int get_iflink(const char *ifname)
{
	char path[256];

	snprintf(path, sizeof(path), "/sys/class/net/%s/iflink", ifname);
	return read_int(path);
}
	
int is_ether(const char *ifname)
{
	/* check for bridge in sysfs */
	int type = get_iftype(ifname);

	return (type == ARPHRD_ETHER) || (type == ARPHRD_EETHER);
}


int is_loopback(const char *ifname)
{
	return get_ifflags(ifname) & IFF_LOOPBACK;
}

int is_p2p(const char *ifname)
{
	return get_ifflags(ifname) & IFF_POINTOPOINT;
}

int is_noarp(const char *ifname)
{
	return get_ifflags(ifname) & IFF_NOARP;
}

int is_mbond(const char *ifname)
{
	return get_ifflags(ifname) & IFF_MASTER;
}

int is_sbond(const char *ifname)
{
	return get_ifflags(ifname) & IFF_SLAVE;
}

int is_slave(const char *ifmaster, const char *ifslave)
{
	int i;
	int rc = 0;
	int fd;
	struct ifreq ifr;
	struct ifbond ifb;
	struct ifslave ifs;

	if (!is_mbond(ifmaster))
		goto out_done;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd <= 0)
		goto out_done;

	memset(&ifr, 0, sizeof(ifr));
	memset(&ifb, 0, sizeof(ifb));
	strncpy(ifr.ifr_name, ifmaster, IFNAMSIZ);
	ifr.ifr_data = (caddr_t)&ifb;
	if (ioctl(fd, SIOCBONDINFOQUERY, &ifr))
		goto out_close;

	for (i = 0; i < ifb.num_slaves; i++) {
		memset(&ifs, 0, sizeof(ifs));
		ifs.slave_id = i;
		ifr.ifr_data = (caddr_t)&ifs;
		if (ioctl(fd, SIOCBONDSLAVEINFOQUERY, &ifr) == 0) {
			if (!strncmp(ifs.slave_name, ifslave, IFNAMSIZ)) {
				rc = 1;
				break;
			}
		}
	}
	
out_close:
	close(fd);
out_done:
	return rc;
}

int get_ifidx(const char *ifname)
{
	int fd;
	int idx = 0;
	struct ifreq ifreq;

	fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifreq, 0, sizeof(ifreq));
		strncpy(ifreq.ifr_name, ifname, IFNAMSIZ);
		if (ioctl(fd, SIOCGIFINDEX, &ifreq) == 0)
			idx = ifreq.ifr_ifindex;
		close(fd);
	}
	return idx;
}

int get_master(const char *ifname)
{
	int i;
	int idx = 0;
	int fd = -1;
	int cnt = 0;
	struct ifreq *ifr = NULL;
	struct ifconf ifc;
	char ifcbuf[sizeof(struct ifreq) * 32];

	/* if it's a master bond, return its own index */
	if (is_mbond(ifname))
		return get_ifidx(ifname);

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifc, 0, sizeof(ifc));
		memset(ifcbuf, 0, sizeof(ifcbuf));
		ifc.ifc_buf = ifcbuf;
		ifc.ifc_len = sizeof(ifcbuf);
		if (ioctl(fd, SIOCGIFCONF, (caddr_t)&ifc) == 0) {
			ifr = ifc.ifc_req;
			cnt = ifc.ifc_len/sizeof(struct ifreq);
			for (i = 0; i < cnt; i++, ifr++) {
				if (!strncmp(ifr->ifr_name, ifname, IFNAMSIZ))
					continue;
				if (!is_mbond(ifr->ifr_name))
					continue;
				if (!is_slave(ifr->ifr_name, ifname))
					continue;
				if (ioctl(fd, SIOCGIFINDEX, ifr) == 0)
					idx = ifr->ifr_ifindex;
				break;
			}
		}
		close(fd);
	}
	return idx;
}

int is_bridge(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	char path[256];
	DIR *dirp;

	if (!is_ether(ifname)) {
		return 0;
	}
	/* check for bridge in sysfs */
	snprintf(path, sizeof(path), "/sys/class/net/%s/bridge", ifname);
	dirp = opendir(path);
	if (dirp) {
		closedir(dirp);
		rc = 1;
	} else { 
		/* use ioctl */	
		fd = socket(AF_LOCAL, SOCK_STREAM, 0);
		if (fd > 0) {
			struct ifreq ifr;
			struct __bridge_info bi;
			unsigned long args[4] = { BRCTL_GET_BRIDGE_INFO, 
						 (unsigned long) &bi, 0, 0 };

			ifr.ifr_data = (char *)args;
			strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
			if (ioctl(fd, SIOCDEVPRIVATE, &ifr) == 0) {
				rc = 1;
			}
			close(fd);
		} else {
			perror("is_bridge() open socket failed");
		}
	}
	return rc;
}

int is_vlan(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	struct vlan_ioctl_args ifv;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifv, 0, sizeof(ifv));
		ifv.cmd = GET_VLAN_REALDEV_NAME_CMD;
		strncpy(ifv.device1, ifname, sizeof(ifv.device1));
		if (ioctl(fd, SIOCGIFVLAN, &ifv) == 0)
			rc = 1;
		close(fd);
	} else {
		perror("is_vlan() open socket error");
	}
	return rc;
}

int is_vlan_capable(const char *ifname)
{

	int features = get_iffeatures(ifname);

	#ifndef NETIF_F_VLAN_CHALLENGED
	#define NETIF_F_VLAN_CHALLENGED 1024
	#endif
	return !(features & NETIF_F_VLAN_CHALLENGED);
}

int is_wlan(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	struct iwreq iwreq;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&iwreq, 0, sizeof(iwreq));
		strncpy(iwreq.ifr_name, ifname, sizeof(iwreq.ifr_name));
		if (ioctl(fd, SIOCGIWNAME, &iwreq) == 0)
			rc = 1;
		close(fd);
	} else {
		perror("is_wlan() open socket error");
	}
	return rc;
}

int is_router(const char *ifname)
{
	int rc = 0;
	char path[256];

	snprintf(path, sizeof(path), "/proc/sys/net/ipv4/conf/all/forwarding");
	rc = read_bool(path);

	snprintf(path, sizeof(path), "/proc/sys/net/ipv6/conf/all/forwarding");
	rc |= read_bool(path);

	return rc;
}

int is_active(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	struct ifreq ifr;

	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
			if (ifr.ifr_flags & IFF_UP)
				rc = 1;
		close(fd);
	} else {
		perror("is_active() open socket error");
	}
	return rc;
}

int is_autoneg_supported(const char *ifname)
{
	int rc = 0;
	int fd = -1;
	struct ifreq ifr;
	struct ethtool_cmd cmd;
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = ETHTOOL_GSET;
		ifr.ifr_data = &cmd;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCETHTOOL, &ifr) == 0)
			if (cmd.supported & SUPPORTED_Autoneg)
				rc = 1;
		close(fd);
	} else {	
		perror("is_autoneg_supported() open socket failed");
	}
	return rc;
}

int is_autoneg_enabled(const char *ifname)
{
	int rc = 0;
	int fd = -1;
	struct ifreq ifr;
	struct ethtool_cmd cmd;
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = ETHTOOL_GSET;
		ifr.ifr_data = &cmd;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCETHTOOL, &ifr) == 0)
			rc = cmd.autoneg;
		close(fd);
	} else {	
		perror("is_autoneg_enabled() open socket failed");
	}
	return rc;
}

/* IETF RFC 3636 dot3MauType: http://www.rfc-editor.org/rfc/rfc3636.txt */
#define MAUCAPADV_bOther	(1 << 0) /* other or unknown */
#define MAUCAPADV_b10baseT	(1 << 1) /* 10BASE-T  half duplex mode */
#define MAUCAPADV_b10baseTFD	(1 << 2) /* 10BASE-T  full duplex mode */
#define MAUCAPADV_b100baseT4	(1 << 3) /* 100BASE-T4 */
#define MAUCAPADV_b100baseTX	(1 << 4) /* 100BASE-TX half duplex mode */
#define MAUCAPADV_b100baseTXFD	(1 << 5) /* 100BASE-TX full duplex mode */
#define MAUCAPADV_b100baseT2	(1 << 6) /* 100BASE-T2 half duplex mode */
#define MAUCAPADV_b100baseT2FD	(1 << 7) /* 100BASE-T2 full duplex mode */
#define MAUCAPADV_bFdxPause	(1 << 8) /* PAUSE for full-duplex links */
#define MAUCAPADV_bFdxAPause	(1 << 9) /* Asymmetric PAUSE for full-duplex links */
#define MAUCAPADV_bFdxSPause	(1 << 10) /* Symmetric PAUSE for full-duplex links */
#define MAUCAPADV_bFdxBPause	(1 << 11) /* Asymmetric and Symmetric PAUSE for full-duplex links */
#define MAUCAPADV_b1000baseX	(1 << 12) /* 1000BASE-X, -LX, -SX, -CX half duplex mode */
#define MAUCAPADV_b1000baseXFD	(1 << 13) /* 1000BASE-X, -LX, -SX, -CX full duplex mode */
#define MAUCAPADV_b1000baseT	(1 << 14) /* 1000BASE-T half duplex mode */
#define MAUCAPADV_b1000baseTFD	(1 << 15) /* 1000BASE-T full duplex mode */
int get_maucaps(const char *ifname)
{
	int fd = -1;
	u16 caps = MAUCAPADV_bOther;
	struct ifreq ifr;
	struct ethtool_cmd cmd;
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = ETHTOOL_GSET;
		ifr.ifr_data = &cmd;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
			if (cmd.advertising & ADVERTISED_10baseT_Half)
				caps |= MAUCAPADV_b10baseT;
			if (cmd.advertising & ADVERTISED_10baseT_Full)
				caps |= MAUCAPADV_b10baseTFD;
			if (cmd.advertising & ADVERTISED_100baseT_Half)
				caps |= MAUCAPADV_b100baseTX;
			if (cmd.advertising & ADVERTISED_100baseT_Full)
				caps |= MAUCAPADV_b100baseTXFD;
			if (cmd.advertising & ADVERTISED_1000baseT_Half)
				caps |= MAUCAPADV_b1000baseT;
			if (cmd.advertising & ADVERTISED_1000baseT_Full)
				caps |= MAUCAPADV_b1000baseTFD;
			if (cmd.advertising & ADVERTISED_Pause)
				caps |= (MAUCAPADV_bFdxPause | MAUCAPADV_bFdxSPause);
			if (cmd.advertising & ADVERTISED_Asym_Pause)
				caps |= MAUCAPADV_bFdxAPause;
			if (cmd.advertising & (ADVERTISED_Asym_Pause | ADVERTISED_Pause))
				caps |= MAUCAPADV_bFdxBPause;
		}
		close(fd);
	} else {	
		perror("is_autoneg() open socket failed");
	}
	return caps;
}

/* IETF RFC 2668 dot3MauType: http://www.rfc-editor.org/rfc/rfc2668.txt */
#define DOT3MAUTYPE_AUI 1
#define DOT3MAUTYPE_10Base5 2
#define DOT3MAUTYPE_Foirl 3
#define DOT3MAUTYPE_10Base2 4
#define DOT3MAUTYPE_10BaseT 5
#define DOT3MAUTYPE_10BaseFP 6
#define DOT3MAUTYPE_10BaseFB 7
#define DOT3MAUTYPE_10BaseFL 8
#define DOT3MAUTYPE_10Broad36 9
#define DOT3MAUTYPE_10BaseTHD 10
#define DOT3MAUTYPE_10BaseTFD 11
#define DOT3MAUTYPE_10BaseFLHD 12
#define DOT3MAUTYPE_10BaseFLFD 13
#define DOT3MAUTYPE_100BaseT4 14
#define DOT3MAUTYPE_100BaseTXHD 15
#define DOT3MAUTYPE_100BaseTXFD 16
#define DOT3MAUTYPE_100BaseFXHD 17
#define DOT3MAUTYPE_100BaseFXFD 18
#define DOT3MAUTYPE_100BaseT2HD 19
#define DOT3MAUTYPE_100BaseT2FD 20
#define DOT3MAUTYPE_1000BaseXHD 21
#define DOT3MAUTYPE_1000BaseXFD 22
#define DOT3MAUTYPE_1000BaseLXHD 23
#define DOT3MAUTYPE_1000BaseLXFD 24
#define DOT3MAUTYPE_1000BaseSXHD 25
#define DOT3MAUTYPE_1000BaseSXFD 26
#define DOT3MAUTYPE_1000BaseCXHD 27
#define DOT3MAUTYPE_1000BaseCXFD 28
#define DOT3MAUTYPE_1000BaseTHD 29
#define DOT3MAUTYPE_1000BaseTFD 30
int get_mautype(const char *ifname)
{
	int rc = 0;
	int fd = -1;
	struct ifreq ifr;
	struct ethtool_cmd cmd;
	u32 speed;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = ETHTOOL_GSET;
		ifr.ifr_data = &cmd;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCETHTOOL, &ifr) == 0) {
			/* TODO: too many dot3MauTypes,
			 * should check duplex, speed, and port */
			speed = (cmd.speed_hi << 16) | cmd.speed;
			if (cmd.port == PORT_AUI)
				rc = DOT3MAUTYPE_AUI;
			else if (speed == SPEED_10)
				rc = DOT3MAUTYPE_10BaseT;
			else if (speed == SPEED_100)
				rc = DOT3MAUTYPE_100BaseTXFD;
			else if (speed == SPEED_1000)
				rc = DOT3MAUTYPE_1000BaseTFD;
		}
		close(fd);
	} else {	
		perror("is_autoneg() open socket failed");
	}
	return rc;
}

int get_mtu(const char *ifname)
{
	int fd = -1;
	int rc = 0;
	struct ifreq ifr;

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd > 0) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFMTU, &ifr) == 0) {
			rc = ifr.ifr_mtu;
		}
		close(fd);
	} else {
		perror("get_mtu() socket create failed");
	}
	return rc;
}

int get_mfs(const char *ifname)
{
	int mfs = get_mtu(ifname);

	#ifndef VLAN_HLEN
	#define VLAN_HLEN	4
	#endif
	if (mfs) {
		mfs += ETH_HLEN + ETH_FCS_LEN;
		if (is_vlan_capable(ifname))
			mfs += VLAN_HLEN;
	}
	return mfs;
}

int get_mac(const char *ifname, u8 mac[])
{
	int fd = -1;
	int rc = EINVAL;
	struct ifreq ifr;

	memset(mac, 0, 6);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd > 0) {
		ifr.ifr_addr.sa_family = AF_INET;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (!ioctl(fd, SIOCGIFHWADDR, &ifr)) {
			memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
			rc = 0;
		}
		close(fd);
	} else {
		perror("get_mac() socket create failed");
	}
	return rc;
}

int get_macstr(const char *ifname, char *addr, size_t size)
{
	u8 mac[6];
	int rc;

	rc = get_mac(ifname, mac);
	if (rc == 0) {
		snprintf(addr, size, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1],	mac[2],	mac[3],	mac[4],	mac[5]);
	}
	return rc;
}


u16 get_caps(const char *ifname)
{
	u16 caps = 0;

	/* how to find TPID to determine C-VLAN vs. S-VLAN ? */
	if (is_vlan(ifname))
		caps |= SYSCAP_CVLAN; 

	if (is_bridge(ifname))
		caps |= SYSCAP_BRIDGE;

	if (is_router(ifname))
		caps |= SYSCAP_ROUTER;

	if (is_wlan(ifname))
		caps |= SYSCAP_WLAN;
#if 0
	if (is_phone(ifname))
		caps |= SYSCAP_PHONE;
	if (is_docsis(ifname))
		caps |= SYSCAP_DOCSIS;
	if (is_repeater(ifname))
		caps |= SYSCAP_REPEATER;
	if (is_tpmr(ifname))
		caps |= SYSCAP_TPMR;
	if (is_other(ifname))
		caps |= SYSCAP_OTHER;

#endif
	if (!caps)
		caps = SYSCAP_STATION;

	return caps;
}

int get_saddr(const char *ifname, struct sockaddr_in *saddr)
{
	int fd = -1;
	int rc = EIO;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd > 0) {
		ifr.ifr_addr.sa_family = AF_INET;
		strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
			memcpy(saddr, &ifr.ifr_addr, sizeof(*saddr));
			rc = 0;
		}
		close(fd);
	}
	return rc;
}

int get_ipaddr(const char *ifname, struct in_addr *in)
{
	int rc;
	struct sockaddr_in sa;

	rc = get_saddr(ifname, &sa);
	if (rc == 0)
		memcpy(in, &sa.sin_addr, sizeof(struct in_addr));
	return rc;
}


int get_ipaddrstr(const char *ifname, char *ipaddr, size_t size)
{
	int rc;
	struct sockaddr_in sa;

	rc = get_saddr(ifname, &sa);
	if (rc == 0) {
		memset(ipaddr, 0, size);
		strncpy(ipaddr, inet_ntoa(sa.sin_addr), size);
	}
	return rc;
}

int get_saddr6(const char *ifname, struct sockaddr_in6 *saddr)
{
	int rc = 0;
	struct ifaddrs *ifa;
	struct ifaddrs *ifaddr;

	rc = getifaddrs(&ifaddr);
	if (rc == 0) {
		for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
			if ((ifa->ifa_addr->sa_family == AF_INET6) &&
			    (strncmp(ifa->ifa_name, ifname, IFNAMSIZ) == 0)) {
				memcpy(saddr, ifa->ifa_addr, sizeof(*saddr));
				rc = 0;
				break;
			}
		}
	}
	freeifaddrs(ifaddr);
	return rc;
}

int get_ipaddr6(const char *ifname, struct in6_addr *in6)
{
	int rc;
	struct sockaddr_in6 sa;

	rc = get_saddr6(ifname, &sa);
	if (rc == 0)
		memcpy(in6, &sa.sin6_addr, sizeof(struct in6_addr));
	return rc;
}

int get_ipaddr6str(const char *ifname, char *ip, size_t size)
{
	#define ifa_sia(i, f) (((f) == AF_INET) ? \
		 ((void *) &((struct sockaddr_in *) (i))->sin_addr) : \
		 ((void *) &((struct sockaddr_in6 *) (i))->sin6_addr))

	#define ifa_sin(i) \
		 ((void *) &((struct sockaddr_in *) (i)->ifa_addr)->sin_addr)

	#define ifa_sin6(i) \
		 (&((struct sockaddr_in6 *) (i)->ifa_addr)->sin6_addr)

	int rc = 0;
	struct sockaddr_in6 sa;

	rc = get_saddr6(ifname, &sa);
	if (rc == 0)
		if (inet_ntop(sa.sin6_family, &sa.sin6_addr, ip, size) == NULL)
			rc = EIO;
	return rc;
}

int get_addr(const char *ifname, int domain, void *buf)
{
	if (domain == AF_INET)
		return get_ipaddr(ifname, (struct in_addr *)buf);
	else if (domain == AF_INET6)
		return get_ipaddr6(ifname, (struct in6_addr *)buf);
	else if (domain == AF_UNSPEC)
		return get_mac(ifname, (u8 *)buf);
	else
		return -1;
}

/* MAC_ADDR_STRLEN = strlen("00:11:22:33:44:55") */
#define MAC_ADDR_STRLEN	17
int mac2str(const u8 *mac, char *dst, size_t size)
{
	if (dst && size > MAC_ADDR_STRLEN) {
		snprintf(dst, size, "%02X:%02X:%02X:%02X:%02X:%02X",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		return 0;
	}
	return -1;
}

int str2mac(const char *src, u8 *mac, size_t size)
{
	int i = 0;
	int rc = -1;

	if (size < 6)
		goto out_err;

	if (!src)
		goto out_err;

	if (strlen(src) != MAC_ADDR_STRLEN)
		goto out_err;

	memset(mac, 0, size);
	for (i = 0; i < 6; i++, mac++)
		if (1 != sscanf(&src[i * 3], "%02X", mac))
			goto out_err;
	rc = 0;
out_err:
	return rc;
}

int str2addr(int domain, const char *src, void *dst, size_t size)
{
	if ((domain == AF_INET) || (domain == AF_INET6)) {
		if (1 == inet_pton(domain, src, dst))
			return 0;
		else
			return -1;
	}

	if (domain == AF_UNSPEC)
		return str2mac(src, (u8 *)dst, size);

	return -1;
}

int addr2str(int domain, const void *src, char *dst, size_t size)
{
	if ((domain == AF_INET) || (domain == AF_INET6)) {
		if (inet_ntop(domain, src, dst, size))
			return 0;
		else
			return -1;
	}

	if (domain == AF_UNSPEC)
		return mac2str((u8 *)src, dst, size);

	return -1;
}

/*
 * check_link_status - check the link status of the port
 * @ifname: the port name
 *
 * Returns: 0 if error or no link and non-zero if interface has link
 */
int check_link_status(const char *ifname)
{
	int fd;
	struct ifreq ifr;
	int retval = 0;
	struct link_value
	{
		u32 cmd ;
		u32 data;
	} linkstatus = { ETHTOOL_GLINK, 0};

	fd = socket(PF_INET, SOCK_DGRAM, 0);

	if (fd<=0)
		return retval;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, ifname);
	ifr.ifr_data = (caddr_t)&linkstatus;
	if (ioctl(fd,SIOCETHTOOL, &ifr) == 0)
		retval = linkstatus.data;

	close(fd);

	return retval;
}
