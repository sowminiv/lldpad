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
#include "lldp.h"
#include "lldp/l2_packet.h"
#include "dcb_types.h"

void somethingChangedLocal(char *ifname)
{
	struct port *port = porthead;

	while (port != NULL) {
		if (!strncmp(ifname, port->ifname, MAX_DEVICE_NAME_LEN))
			break;
		port = port->next;
	}

	if (!port)
		return;

	port->tx.localChange = 1;
	return;
}

int tlv_ok(struct unpacked_tlv *tlv)
{
	if (!tlv || (!tlv->length && tlv->type))
		return 0;
	else
		return 1;
}

struct packed_tlv *pack_tlv(struct unpacked_tlv *tlv)
{
	u16 tl = 0;
	struct packed_tlv *pkd_tlv = NULL;

	if (!tlv_ok(tlv))
		return NULL;

	tl = tlv->type;
	tl = tl << 9;
	tl |= tlv->length & 0x01ff;
	tl = htons(tl);

	pkd_tlv = (struct packed_tlv *)malloc(sizeof(struct packed_tlv));
	if(!pkd_tlv) {
		printf("pack_tlv: Failed to malloc pkd_tlv\n");
		return NULL;
	}
	memset(pkd_tlv,0,sizeof(struct packed_tlv));
	pkd_tlv->size = tlv->length + sizeof(tl);
	pkd_tlv->tlv = (u8 *)malloc(pkd_tlv->size);
	if(pkd_tlv->tlv) {
		memset(pkd_tlv->tlv,0, pkd_tlv->size);
		memcpy(pkd_tlv->tlv, &tl, sizeof(tl));
		if (tlv->length)
			memcpy(&pkd_tlv->tlv[sizeof(tl)], tlv->info,
				tlv->length);
	} else {
		printf("pack_tlv: Failed to malloc tlv\n");
		free(pkd_tlv);
		pkd_tlv = NULL;
		return NULL;
	}
	return pkd_tlv;
}

/*
 * pack the input tlv and put it at the end of the already packed tlv mtlv
 * update the total size of the out put mtlv
 */
int pack_tlv_after(struct unpacked_tlv *tlv, struct packed_tlv *mtlv, int length)
{
	struct packed_tlv *ptlv;

	if (!tlv)
		return 0;  /* no TLV is ok */

	if (!tlv_ok(tlv))
		return -1;

	ptlv =  pack_tlv(tlv);
	if (!ptlv)
		return -1;

	if (ptlv->size + mtlv->size > length) {
		ptlv = free_pkd_tlv(ptlv);
		return -1;
	}

	memcpy(&mtlv->tlv[mtlv->size], ptlv->tlv, ptlv->size);
	mtlv->size += ptlv->size;
	ptlv = free_pkd_tlv(ptlv);
	return 0;
}


struct unpacked_tlv *unpack_tlv(struct packed_tlv *tlv)
{
	u16 tl = 0;
	struct unpacked_tlv *upkd_tlv = NULL;

	if(!tlv) {
		return NULL;
	}

	memcpy(&tl,tlv->tlv, sizeof(tl));
	tl = ntohs(tl);

	upkd_tlv = (struct unpacked_tlv *)malloc(sizeof(struct unpacked_tlv));
	if(upkd_tlv) {
		memset(upkd_tlv,0, sizeof(struct unpacked_tlv));
		upkd_tlv->length = tl & 0x01ff;
		upkd_tlv->info = (u8 *)malloc(upkd_tlv->length);
		if(upkd_tlv->info) {
			memset(upkd_tlv->info,0, upkd_tlv->length);
			tl = tl >> 9;
			upkd_tlv->type = (u8)tl;
			memcpy(upkd_tlv->info, &tlv->tlv[sizeof(tl)],
				upkd_tlv->length);
		} else {
			printf("unpack_tlv: Failed to malloc info\n");
			free (upkd_tlv);
			return NULL;
		}
	} else {
		printf("unpack_tlv: Failed to malloc upkd_tlv\n");
		return NULL;
	}
	return upkd_tlv;
}

struct unpacked_tlv *free_unpkd_tlv(struct unpacked_tlv *tlv)
{
	if (tlv != NULL) {
		if (tlv->info != NULL) {
			free(tlv->info);
			tlv->info = NULL;
		}
		free(tlv);
		tlv = NULL;
	}
	return NULL;
}

struct packed_tlv *free_pkd_tlv(struct packed_tlv *tlv)
{
	if (tlv != NULL) {
		if (tlv->tlv != NULL) {
			free(tlv->tlv);
			tlv->tlv = NULL;
		}
		free(tlv);
		tlv = NULL;
	}
	return NULL;
}

struct packed_tlv *create_ptlv()
{
	struct packed_tlv *ptlv =
		(struct packed_tlv *)malloc(sizeof(struct packed_tlv));

	if(ptlv)
		memset(ptlv, 0, sizeof(struct packed_tlv));
	return ptlv;
}

struct unpacked_tlv *create_tlv()
{
	struct unpacked_tlv *tlv =
		(struct unpacked_tlv *)malloc(sizeof(struct unpacked_tlv));

	if(tlv) {
		memset(tlv,0, sizeof(struct unpacked_tlv));
		return tlv;
	} else {
		printf("create_tlv: Failed to malloc tlv\n");
		return NULL;
	}
}

struct unpacked_tlv *bld_end_tlv()
{
	struct unpacked_tlv *tlv = create_tlv();

	if(tlv) {
		tlv->type = END_OF_LLDPDU_TLV;
		tlv->length = 0;
		tlv->info = NULL;
	}
	return tlv;
}

struct packed_tlv *pack_end_tlv()
{
	struct unpacked_tlv *tlv;
	struct packed_tlv *ptlv;

	tlv = bld_end_tlv();
	ptlv = pack_tlv(tlv);
	free(tlv);
	return ptlv;
}
