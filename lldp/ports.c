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

#include "dcb_osdep.h"
#include "states.h"
#include "lldp_tlv.h"
#include "ports.h"
#include "l2_packet.h"
#include "libconfig.h"
#include "lldp_mand_clif.h"
#include "dcb_types.h"
#include "config.h"
#include "drv_cfg.h"

struct port *porthead = NULL; /* Head pointer */
struct port *portcurrent = NULL; /* Working  pointer loaded from ports or
				  * port->next */

extern u8 gdcbx_subtype;

void agent_receive(void *, const u8 *, const u8 *, size_t);

int get_lldp_port_statistics(char *ifname, struct portstats *stats)
{
	struct port *port;

	port = port_find_by_name(ifname);
	if (!port)
		return 1;
	memcpy((void *)stats, (void *)&port->stats, sizeof(struct portstats));
	return 0;
}

int get_local_tlvs(char *ifname, unsigned char *tlvs, int *size)
{
	struct port *port;

	port = port_find_by_name(ifname);
	if (!port)
		return 1;

	if (port->tx.frameout == NULL) {
		*size = 0;
		return 0;
	}

	*size = port->tx.sizeout - sizeof(struct l2_ethhdr);
	if (*size < 0)
		return 1;
	memcpy((void *)tlvs,
	       (void *)port->tx.frameout + sizeof(struct l2_ethhdr), *size);

	return 0;
}

int get_neighbor_tlvs(char *ifname, unsigned char *tlvs, int *size)
{
	struct port *port;

	port = port_find_by_name(ifname);
	if (!port)
		return 1;

	if (port->rx.framein == NULL) {
		*size = 0;
		return 0;
	}

	*size = port->rx.sizein - sizeof(struct l2_ethhdr);
	if (*size < 0)
		return 1;
	memcpy((void *)tlvs,
	       (void *)port->rx.framein + sizeof(struct l2_ethhdr), *size);
	return 0;
}

void set_lldp_port_admin(const char *ifname, int admin)
{
	struct port *port = NULL;
	int all = 0;
	int tmp;

	all = !strlen(ifname);

	port = porthead;
	while (port != NULL) {
		if (all || !strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN)) {

			/* don't change a port which has an explicit setting
			 * on a global setting change
			 */
			if (all && (!get_config_setting(port->ifname,
						      ARG_ADMINSTATUS,
			                             (void *)&tmp,
						      CONFIG_TYPE_INT))) {
				port = port->next;
				continue;
			}

			if (port->adminStatus != admin) {
				port->adminStatus = admin;
				somethingChangedLocal(ifname);
				run_tx_sm(port, false);
				run_rx_sm(port, false);
			}

			if (!all)
				break;
		}
		port = port->next;
	}
}

void set_lldp_port_enable_state(const char *ifname, int enable)
{
	struct port *port = NULL;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN))  /* Localization OK */
			break;
		port = port->next;
	}

	if (port == NULL) {
		return;
	}

	port->portEnabled = (u8)enable;
	if (enable) {
		/* port->adminStatus = enabledRxTx; */
	} else {
		/* port->adminStatus = disabled; */
		port->rx.rxInfoAge = false;
	}
	run_tx_sm(port, false);
	run_rx_sm(port, false);
}

void set_port_oper_delay(const char *ifname)
{
	struct port *port = NULL;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN)) {  /* Localization OK */
			break;
		}
		port = port->next;
	}

	if (port == NULL) {
		return;
	}

	port->timers.dormantDelay = DORMANT_DELAY;
	return;
}

int set_port_hw_resetting(const char *ifname, int resetting)
{
	struct port *port = NULL;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN)) {  /* Localization OK */
			break;
		}
		port = port->next;
	}

	if (port == NULL) {
		return -1;
	}

	port->hw_resetting = (u8)resetting;

	return port->hw_resetting;
}

int get_port_hw_resetting(const char *ifname)
{
	struct port *port = NULL;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN)) {  /* Localization OK */
			break;
		}
		port = port->next;
	}

	if (port)
		return port->hw_resetting;
	else
		return 0;
}

int reinit_port(const char *ifname)
{
	struct port *port;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname, MAX_DEVICE_NAME_LEN))
			break;
		port = port->next;
	}

	if (!port)
		return -1;

	/* Reset relevant port variables */
	port->tx.state  = TX_LLDP_INITIALIZE;
	port->rx.state = LLDP_WAIT_PORT_OPERATIONAL;
	port->hw_resetting = false;
	port->portEnabled = false;
	port->tx.txTTL = 0;
	port->msap.length1 = 0;
	port->msap.msap1 = NULL;
	port->msap.length2 = 0;
	port->msap.msap2 = NULL;
	port->lldpdu = false;
	port->timers.dormantDelay = DORMANT_DELAY;

	/* init & enable RX path */
	rxInitializeLLDP(port);

	/* init TX path */
	txInitializeLLDP(port);
	port->tlvs.last_peer = NULL;
	port->tlvs.cur_peer = NULL;

	return 0;
}

int add_port(const char *ifname)
{
	struct port *newport;

	newport = porthead;
	while (newport != NULL) {
		if (!strncmp(ifname, newport->ifname,
			MAX_DEVICE_NAME_LEN)) {
			return 0;
		}
		newport = newport->next;
	}

	newport  = (struct port *)malloc(sizeof(struct port));
	if (newport == NULL) {
		printf("new port malloc failed\n");
		goto fail;
	}
	memset(newport,0,sizeof(struct port));
	newport->next = NULL;
	newport->ifname = strdup(ifname);
	if (newport->ifname == NULL) {
		printf("new port name malloc failed\n");
		goto fail;
	}

	/* Initialize relevant port variables */
	newport->tx.state  = TX_LLDP_INITIALIZE;
	newport->rx.state = LLDP_WAIT_PORT_OPERATIONAL;
	newport->hw_resetting = false;
	newport->portEnabled = false;
	if (init_cfg()) {
		if (get_config_setting(newport->ifname, ARG_ADMINSTATUS,
			      (void *)&newport->adminStatus, CONFIG_TYPE_INT))
			newport->adminStatus = disabled;
	}
	newport->tx.txTTL = 0;
	newport->msap.length1 = 0;
	newport->msap.msap1 = NULL;
	newport->msap.length2 = 0;
	newport->msap.msap2 = NULL;
	newport->lldpdu = false;
	newport->timers.dormantDelay = DORMANT_DELAY;

	/* init & enable RX path */
	rxInitializeLLDP(newport);
	newport->l2 = l2_packet_init(newport->ifname, NULL, ETH_P_LLDP,
		rxReceiveFrame, newport, 1);
	if (newport->l2 == NULL) {
		printf("Failed to open register layer 2 access to "
			"ETH_P_LLDP\n");
		goto fail;
	}

	/* init TX path */
	txInitializeLLDP(newport);
	newport->tlvs.last_peer = NULL;
	newport->tlvs.cur_peer = NULL;

	/* enable TX path */
	if (porthead) {
		newport->next = porthead;
	}
	porthead = newport;
	return 0;

fail:
	if(newport) {
		if(newport->ifname)
			free(newport->ifname);
		free(newport);
	}
	return -1;
}

int remove_port(const char *ifname)
{
	struct port *port = NULL;    /* Pointer to port to remove */
	struct port *parent = NULL;  /* Pointer to previous on port stack */

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname,
			MAX_DEVICE_NAME_LEN)) {  /* Localization OK */
			printf("In remove_port: Found port %s\n",port->ifname);
			break;
		}
		parent = port;
		port = port->next;
	}

	if (port == NULL) {
		printf("remove_port: port not present\n");
		return -1;
	}
	/* Close down the socket */
	l2_packet_deinit(port->l2);

	/* Re-initialize relevant port variables */
	port->tx.state = TX_LLDP_INITIALIZE;
	port->rx.state = LLDP_WAIT_PORT_OPERATIONAL;
	port->portEnabled  = false;
	port->adminStatus  = disabled;
	port->tx.txTTL = 0;

	/* Take this port out of the chain */
	if (parent == NULL) {
		porthead = port->next;
	} else if (parent->next == port) { /* Sanity check */
		parent->next = port->next;
	} else {
		return -1;
	}

	/* Remove the tlvs */
	if (port->tlvs.cur_peer != NULL) {
		port->tlvs.cur_peer = free_unpkd_tlv(port->tlvs.cur_peer);
	}

	if (port->tlvs.last_peer != NULL) {
		port->tlvs.last_peer =
			free_unpkd_tlv(port->tlvs.last_peer);
	}

	if (port->msap.msap1) {
		free(port->msap.msap1);
		port->msap.msap1 = NULL;
	}

	if (port->msap.msap2) {
		free(port->msap.msap2);
		port->msap.msap2 = NULL;
	}

	if (port->rx.framein)
		free(port->rx.framein);

	if (port->tx.frameout)
		free(port->tx.frameout);

	if (port->ifname)
		free(port->ifname);

	free(port);
	return 0;
}

/*
 * port_needs_shutdown - check if we need send LLDP shutdown frame on this port
 * @port: the port struct
 *
 * Return 1 for yes and 0 for no.
 *
 * No shutdown frame for port that has dcb enabled
 */
int port_needs_shutdown(struct port *port)
{
	return !check_port_dcb_mode(port->ifname);
}
