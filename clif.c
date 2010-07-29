/*******************************************************************************

  LLDP Agent Daemon (LLDPAD) Software 
  Copyright(c) 2007-2010 Intel Corporation.

  Substantially modified from:
  hostapd-0.5.7
  Copyright (c) 2002-2007, Jouni Malinen <jkmaline@cc.hut.fi> and
  contributors

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

#include "includes.h"


#include "clif.h"
#include "clif_msgs.h"
#include "common.h"


#if defined(CONFIG_CLIF_IFACE_UNIX) || defined(CONFIG_CLIF_IFACE_UDP)
#define CLIF_IFACE_SOCKET
#endif /* CONFIG_CLIF_IFACE_UNIX || CONFIG_CLIF_IFACE_UDP */


struct clif  *clif_open(const char *clif_path)
{
	struct clif *clif;
	static int counter = 0;

	clif = os_malloc(sizeof(*clif));
	if (clif == NULL)
		return NULL;
	os_memset(clif, 0, sizeof(*clif));

	clif->s = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (clif->s < 0) {
		os_free(clif);
		return NULL;
	}

	clif->local.sun_family = AF_UNIX;
	os_snprintf(clif->local.sun_path, sizeof(clif->local.sun_path),
		    "/tmp/lldpad_clif_%d-%d", getpid(), counter++);
	if (bind(clif->s, (struct sockaddr *) &clif->local,
		    sizeof(clif->local)) < 0) {
		close(clif->s);
		os_free(clif);
		return NULL;
	}

	clif->dest.sun_family = AF_UNIX;
	os_snprintf(clif->dest.sun_path, sizeof(clif->dest.sun_path), "%s",
		    clif_path);
	if (connect(clif->s, (struct sockaddr *) &clif->dest,
		    sizeof(clif->dest)) < 0) {
		close(clif->s);
		unlink(clif->local.sun_path);
		os_free(clif);
		return NULL;
	}

	return clif;
}

void clif_close(struct clif *clif)
{
	unlink(clif->local.sun_path);
	close(clif->s);
	os_free(clif);
}





int clif_request(struct clif *clif, const char *cmd, size_t cmd_len,
		     char *reply, size_t *reply_len,
		     void (*msg_cb)(char *msg, size_t len))
{
	struct timeval tv;
	int res;
	fd_set rfds;
	const char *_cmd;
	char *cmd_buf = NULL;
	size_t _cmd_len;

	{
		_cmd = cmd;
		_cmd_len = cmd_len;
	}

	if (send(clif->s, _cmd, _cmd_len, 0) < 0) {
		os_free(cmd_buf);
		return -1;
	}
	os_free(cmd_buf);

	for (;;) {
		tv.tv_sec = CMD_RESPONSE_TIMEOUT;
		tv.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_SET(clif->s, &rfds);
		res = select(clif->s + 1, &rfds, NULL, NULL, &tv);
		if (FD_ISSET(clif->s, &rfds)) {
			res = recv(clif->s, reply, *reply_len, 0);
			if (res < 0) {
				printf("less then zero\n");
				return res;
			}
			if (res > 0 && reply[0] == EVENT_MSG) {
				/* This is an unsolicited message from
				 * lldpad, not the reply to the
				 * request. Use msg_cb to report this to the
				 * caller. */
				if (msg_cb) {
					/* Make sure the message is nul
					 * terminated. */
					if ((size_t) res == *reply_len)
						res = (*reply_len) - 1;
					reply[res] = '\0';
					msg_cb(reply, res);
				}
				continue;
			}
			*reply_len = res;
			break;
		} else {
			printf("timeout\n");
			return -2;
		}
	}
	return 0;
}


static int clif_attach_helper(struct clif *clif, char *tlvs_hex, int attach)
{
	char *buf;
	char rbuf[10];
	int ret;
	size_t len = 10;

	/* Allocate maximum buffer usage */
	if (tlvs_hex && attach) {
		buf = malloc(sizeof(char)*(strlen(tlvs_hex) + 1));
		sprintf(buf, "%s%s","A",tlvs_hex);
	} else if (attach) {
		buf = malloc(sizeof(char) * 2);
		sprintf(buf, "A");
	} else {
		buf = malloc(sizeof(char) * 2);
		sprintf(buf, "D");
	}
		
	ret = clif_request(clif, buf, strlen(buf), rbuf, &len, NULL);
	free(buf);
	if (ret < 0)
		return ret;
	if (len == 4 && os_memcmp(rbuf, "R00", 3) == 0)
		return 0;
	return -1;
}


int clif_attach(struct clif *clif, char *tlvs_hex)
{
	return clif_attach_helper(clif, tlvs_hex, 1);
}


int clif_detach(struct clif *clif)
{
	return clif_attach_helper(clif, NULL, 0);
}



int clif_recv(struct clif *clif, char *reply, size_t *reply_len)
{
	int res;

	res = recv(clif->s, reply, *reply_len, 0);
	if (res < 0)
		return res;
	*reply_len = res;
	return 0;
}


int clif_pending(struct clif *clif)
{
	struct timeval tv;
	fd_set rfds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_SET(clif->s, &rfds);
	select(clif->s + 1, &rfds, NULL, NULL, &tv);
	return FD_ISSET(clif->s, &rfds);
}


int clif_get_fd(struct clif *clif)
{
	return clif->s;
}




