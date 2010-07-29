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

#include "includes.h"
#include "common.h"
#include <stdio.h>
#include <syslog.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "lldpad.h"
#include "ctrl_iface.h"
#include "lldp.h"
#include "lldp_basman.h"
#include "lldp_mand_clif.h"
#include "lldp_basman_clif.h"
#include "lldp/ports.h"
#include "libconfig.h"
#include "config.h"
#include "clif_msgs.h"
#include "lldp/states.h"

static int get_arg_ipv4(struct cmd *, char *, char *, char *);
static int set_arg_ipv4(struct cmd *, char *, char *, char *);
static int get_arg_ipv6(struct cmd *, char *, char *, char *);
static int set_arg_ipv6(struct cmd *, char *, char *, char *);
static int get_arg_tlvtxenable(struct cmd *, char *, char *, char *);
static int set_arg_tlvtxenable(struct cmd *, char *, char *, char *);

static struct arg_handlers arg_handlers[] = {
	{ ARG_IPV4_ADDR,   get_arg_ipv4,        set_arg_ipv4 },
	{ ARG_IPV6_ADDR,   get_arg_ipv6,        set_arg_ipv6 },
	{ ARG_TLVTXENABLE, get_arg_tlvtxenable, set_arg_tlvtxenable },
	{ NULL }
};

static int get_arg_tlvtxenable(struct cmd *cmd, char *arg, char *argvalue,
			       char *obuf)
{
	int value;
	char *s;
	char arg_path[256];

	if (cmd->cmd != cmd_gettlv)
		return cmd_invalid;

	switch (cmd->tlvid) {
	case PORT_DESCRIPTION_TLV:
	case SYSTEM_NAME_TLV:
	case SYSTEM_DESCRIPTION_TLV:
	case SYSTEM_CAPABILITIES_TLV:
	case MANAGEMENT_ADDRESS_TLV:
		snprintf(arg_path, sizeof(arg_path), "%s%08x.%s",
			 TLVID_PREFIX, cmd->tlvid, arg);
		
		if (get_config_setting(cmd->ifname, arg_path, (void *)&value,
					CONFIG_TYPE_BOOL))
			value = false;
		break;
	case INVALID_TLVID:
		return cmd_invalid;
	default:
		return cmd_not_applicable;
	}

	if (value)
		s = VAL_YES;
	else
		s = VAL_NO;
	
	sprintf(obuf, "%02x%s%04x%s", (unsigned int)strlen(arg), arg,
		(unsigned int)strlen(s), s);

	return cmd_success;
}

static int set_arg_tlvtxenable(struct cmd *cmd, char *arg, char *argvalue,
			       char *obuf)
{
	int value;
	char arg_path[256];

	if (cmd->cmd != cmd_settlv)
		return cmd_invalid;

	switch (cmd->tlvid) {
	case PORT_DESCRIPTION_TLV:
	case SYSTEM_NAME_TLV:
	case SYSTEM_DESCRIPTION_TLV:
	case SYSTEM_CAPABILITIES_TLV:
	case MANAGEMENT_ADDRESS_TLV:
		break;
	case INVALID_TLVID:
		return cmd_invalid;
	default:
		return cmd_not_applicable;
	}

	if (!strcasecmp(argvalue, VAL_YES))
		value = 1;
	else if (!strcasecmp(argvalue, VAL_NO))
		value = 0;
	else
		return cmd_invalid;

	snprintf(arg_path, sizeof(arg_path), "%s%08x.%s", TLVID_PREFIX,
		 cmd->tlvid, arg);

	if (set_cfg(cmd->ifname, arg_path, (void *)&value, CONFIG_TYPE_BOOL))
		return cmd_failed;

	somethingChangedLocal(cmd->ifname);

	return cmd_success;
}

struct arg_handlers *basman_get_arg_handlers()
{
	return &arg_handlers[0];
}

int get_arg_ipv4(struct cmd *cmd, char *arg, char *argvalue, char *obuf)
{
	char *p;
	char arg_path[256];

	if (cmd->cmd != cmd_gettlv || cmd->tlvid != MANAGEMENT_ADDRESS_TLV)
		return cmd_bad_params;

	snprintf(arg_path, sizeof(arg_path), "%s%08x.%s",
		 TLVID_PREFIX, cmd->tlvid, arg);

	if (get_config_setting(cmd->ifname, arg_path, (void *)&p,
				CONFIG_TYPE_STRING))
		return cmd_failed;

	sprintf(obuf, "%02x%s%04x%s", (unsigned int)strlen(arg), arg,
		(unsigned int)strlen(p), p);
	return cmd_success;
}

int get_arg_ipv6(struct cmd *cmd, char *arg, char *argvalue, char *obuf)
{
	char *p;
	char arg_path[256];

	if (cmd->cmd != cmd_gettlv || cmd->tlvid != MANAGEMENT_ADDRESS_TLV)
		return cmd_bad_params;

	snprintf(arg_path, sizeof(arg_path), "%s%08x.%s",
		 TLVID_PREFIX, cmd->tlvid, arg);
		
	if (get_config_setting(cmd->ifname, arg_path, (void *)&p,
					CONFIG_TYPE_STRING))
		return cmd_failed;

	sprintf(obuf, "%02x%s%04x%s", (unsigned int)strlen(arg), arg,
		(unsigned int)strlen(p), p);

	return cmd_success;
}

int set_arg_ipv4(struct cmd *cmd, char *arg, char *argvalue, char *obuf)
{
	struct in_addr ipv4addr;
	char arg_path[256];
	char *p;

	if (cmd->cmd != cmd_settlv || cmd->tlvid != MANAGEMENT_ADDRESS_TLV)
		return cmd_bad_params;

	if (!inet_pton(AF_INET, argvalue, (void *)&ipv4addr))
		return cmd_bad_params;

	snprintf(arg_path, sizeof(arg_path), "%s%08x.%s", TLVID_PREFIX,
		 cmd->tlvid, arg);

	p = &argvalue[0];
	if (set_config_setting(cmd->ifname, arg_path, (void *)&p,
		    CONFIG_TYPE_STRING))
		return cmd_failed;

	somethingChangedLocal(cmd->ifname);

	return cmd_success;
}

int set_arg_ipv6(struct cmd *cmd, char *arg, char *argvalue, char *obuf)
{
	struct in6_addr ipv6addr;
	char arg_path[256];
	char *p;

	if (cmd->cmd != cmd_settlv || cmd->tlvid != MANAGEMENT_ADDRESS_TLV)
		return cmd_bad_params;

	if (!inet_pton(AF_INET6, argvalue, (void *)&ipv6addr))
		return cmd_bad_params;

	snprintf(arg_path, sizeof(arg_path), "%s%08x.%s", TLVID_PREFIX,
		 cmd->tlvid, arg);

	p = &argvalue[0];
	if (set_config_setting(cmd->ifname, arg_path, (void *)&p,
		    CONFIG_TYPE_STRING))
		return cmd_failed;

	somethingChangedLocal(cmd->ifname);

	return cmd_success;
}
