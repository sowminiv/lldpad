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

#ifndef _LLDP_DCBX_H
#define _LLDP_DCBX_H

#include <sys/un.h>
#include "lldpad.h"
#include "dcb_osdep.h"

#define LLDP_MOD_DCBX 0x001b2101

struct dcbx_manifest {
	struct unpacked_tlv *dcbx1;
	struct unpacked_tlv *dcbx2;
	struct unpacked_tlv *dcbx_ctrl;
	struct unpacked_tlv *dcbx_pg;
	struct unpacked_tlv *dcbx_pfc;
	struct unpacked_tlv *dcbx_app;
	struct unpacked_tlv *dcbx_llink;
};

struct dcbx_tlvs {
	u16  dcbdu;
	u8   dcbx_st;
	char ifname[IFNAMSIZ];
	struct dcbx_manifest *manifest;
	struct unpacked_tlv *dcbx1;
	struct unpacked_tlv *dcbx2;
	struct unpacked_tlv *control;
	struct unpacked_tlv *pg1;
	struct unpacked_tlv *pg2;
	struct unpacked_tlv *pfc1;
	struct unpacked_tlv *pfc2;
	struct unpacked_tlv *app1;
	struct unpacked_tlv *app2;
	struct unpacked_tlv *llink;
	struct port *port;
	LIST_ENTRY(dcbx_tlvs) entry;
};

struct dcbd_user_data {
	LIST_HEAD(dcbx_head, dcbx_tlvs) head;
};

#ifdef __cplusplus
extern "C" {
#endif
struct dcbx_tlvs *dcbx_data(const char *);
#ifdef __cplusplus
}
#endif

struct packed_tlv *dcbx_gettlv(struct port *port);
int dcbx_rchange(struct port *port,  struct unpacked_tlv *tlv);
u8 dcbx_mibDeleteObjects(struct port *port);
void dcbx_ifup(char *device_name);
void dcbx_ifdown(char *device_name);
struct lldp_module *dcbx_register(void);
void dcbx_unregister(struct lldp_module *);
int dcbx_clif_cmd(void *, struct sockaddr_un *,
		  socklen_t , char *, int, char *);

#endif /* _LLDP_DCBX_H */
