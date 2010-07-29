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

#include <ctype.h>
#include "includes.h"
#include "common.h"
#include "clif.h"
#include "dcb_types.h"
#include "lldptool.h"
#include "lldp_mand_clif.h"
#include "lldptool_cli.h"
#include "lldpad.h"
#include "lldp.h"
#include "lldp_mod.h"

static char *print_status(cmd_status status);

int render_cmd(struct cmd *cmd, char *arg, char *argvalue)
{
	int len;

	len = sizeof(cmd->obuf);

	/* all command messages begin this way */
	snprintf(cmd->obuf, len, "%c%08x%c%1x%02x%08x%02x%s",
		MOD_CMD, cmd->module_id, CMD_REQUEST, CLIF_MSG_VERSION,
		cmd->cmd, cmd->ops, (unsigned int) strlen(cmd->ifname),
		cmd->ifname);

	/* if the command is a tlv command, add the tlvid to the message */
	if (cmd->cmd == cmd_gettlv || cmd->cmd == cmd_settlv)
		snprintf(cmd->obuf+strlen(cmd->obuf), len-strlen(cmd->obuf),
			"%08x", cmd->tlvid);

	/* add any arg and argvalue to the command message */
	if (arg)
		snprintf(cmd->obuf+strlen(cmd->obuf), len-strlen(cmd->obuf),
			"%02x%s", (unsigned int)strlen(arg), arg);
	if (argvalue)
		snprintf(cmd->obuf+strlen(cmd->obuf), len-strlen(cmd->obuf),
			"%04x%s", (unsigned int)strlen(argvalue), argvalue);

	return strlen(cmd->obuf);
}

int parse_response(char *buf)
{
	return hex2int(buf+CLIF_STAT_OFF);
}

void get_arg_value(char *str, char **arg, char **argvalue)
{
	int i;

	for (i = 0; i < strlen(str); i++)
		if (!isprint(str[i]))
			return;

	for (i = 0; i < strlen(str); i++)
		if (str[i] == '=')
			break;

	if (i < strlen(str)) {
		str[i] = '\0';
		*argvalue = &str[i+1];
	}
	*arg = str;
}

int cli_cmd_getstats(struct clif *clif, int argc, char *argv[],
			struct cmd *cmd, int raw)
{
	char *arg = NULL;
	char *argvalue = NULL;

	render_cmd(cmd, arg, argvalue);
	return clif_command(clif, cmd->obuf, raw);
}

int cli_cmd_gettlv(struct clif *clif, int argc, char *argv[],
			struct cmd *cmd, int raw)
{
	char *arg = NULL;
	char *argvalue = NULL;

	if (argc > 0)
		get_arg_value(argv[0], &arg, &argvalue);

	/* default is local tlv query */
	if (!(cmd->ops & op_neighbor))
		cmd->ops |= op_local;
	if (arg)
		cmd->ops |= op_arg;

	if (argvalue) {
		printf("%s\n", print_status(cmd_invalid));
		return cmd_invalid;
	}

	render_cmd(cmd, arg, argvalue);
	return clif_command(clif, cmd->obuf, raw);
}

int cli_cmd_settlv(struct clif *clif, int argc, char *argv[],
			struct cmd *cmd, int raw)
{
	char *arg = NULL;
	char *argvalue = NULL;

	if (argc > 0)
		get_arg_value(argv[0], &arg, &argvalue);

	if (!argvalue) {
		printf("%s\n", print_status(cmd_invalid));
		return cmd_invalid;
	}

	render_cmd(cmd, arg, argvalue);
	return clif_command(clif, cmd->obuf, raw);
}

int cli_cmd_getlldp(struct clif *clif, int argc, char *argv[],
			struct cmd *cmd, int raw)
{
	char *arg = NULL;
	char *argvalue = NULL;

	if (argc > 0)
		get_arg_value(argv[0], &arg, &argvalue);

	if (!arg || argvalue) {
		printf("%s\n", print_status(cmd_invalid));
		return cmd_invalid;
	}


	render_cmd(cmd, arg, argvalue);
	return clif_command(clif, cmd->obuf, raw);
}

int cli_cmd_setlldp(struct clif *clif, int argc, char *argv[],
			struct cmd *cmd, int raw)
{
	char *arg = NULL;
	char *argvalue = NULL;

	if (argc > 0)
		get_arg_value(argv[0], &arg, &argvalue);

	if (!argvalue) {
		printf("%s\n", print_status(cmd_invalid));
		return cmd_invalid;
	}

	render_cmd(cmd, arg, argvalue);
	return clif_command(clif, cmd->obuf, raw);
}


static char *print_status(cmd_status status)
{
	char *str;

	switch(status) {
	case cmd_success:
		str = "Successful";
		break;
	case cmd_failed:
		str = "Failed";
		break;
	case cmd_device_not_found:
		str = "Device not found or link down";
		break;
	case cmd_invalid:
		str = "Invalid command";
		break;
	case cmd_bad_params:
		str = "Invalid parameters";
		break;
	case cmd_peer_not_present:
		str = "Peer feature not present";
		break;
	case cmd_ctrl_vers_not_compatible:
		str = "Version not compatible";
		break;
	case cmd_not_capable:
		str = "Device not capable";
		break;
	default:
		str = "Unknown status";
		break;
	}
	return str;
}


u32 get_tlvid(u16 tlv_type, char* ibuf)
{
	u32 tlvid;

	if (tlv_type < 127) {
		tlvid = tlv_type;
	} else {
		hexstr2bin(ibuf, (u8 *)&tlvid, sizeof(tlvid));
		tlvid = ntohl(tlvid);
	}
	return tlvid;
}

static int print_arg_value(char *ibuf)
{
	int ilen;
	int ioff =0;
	char *arg = NULL;
	char *argvalue = NULL;
	u8 arglen;
	u16 argvalue_len;

	ilen = strlen(ibuf);

	/* check for an arg and argvalue */
	if (ilen - ioff > 2*sizeof(arglen)) {
		hexstr2bin(ibuf+ioff, &arglen, sizeof(arglen));
		ioff += 2*sizeof(arglen);
		if (ilen - ioff >= arglen) {
			arg = ibuf+ioff;
			ioff += arglen;

			if (ilen - ioff > 2*sizeof(argvalue_len)) {
				hexstr2bin(ibuf+ioff, (u8 *)&argvalue_len,
					   sizeof(argvalue_len));
				argvalue_len = ntohs(argvalue_len);
				ioff += 2*sizeof(argvalue_len);
				if (ilen - ioff >= argvalue_len) {
					argvalue = ibuf+ioff;
					ioff += argvalue_len;
				}
			}
		}
	}

	if (arg) {
		arg[arglen] = '\0';
		printf("%s", arg);
	}
	if (argvalue) {
		argvalue[argvalue_len] = '\0';
		printf(" = %s\n", argvalue);
	}

	return ioff;
}

static void print_lldp(struct cmd *cmd, char *ibuf)
{
	int ioff = 0;

	ioff = print_arg_value(ibuf);
}

static void print_tlvs(struct cmd *cmd, char *ibuf)
{
	int ilen;
	u16 tlv_type;
	u16 tlv_len;
	u32 tlvid;
	int offset = 0;
	int printed;
	struct lldp_module *np;

	ilen = strlen(ibuf);

	if (cmd->ops & op_arg)
		offset = print_arg_value(ibuf);

	ilen = strlen(ibuf + offset);

	while (ilen > 0) {
		if (ilen < 2*sizeof(u16)) {
			printf("corrupted TLV\n");
			break;
		}
		hexstr2bin(ibuf+offset, (u8 *)&tlv_type, sizeof(tlv_type));
		tlv_type = ntohs(tlv_type);
		tlv_len = tlv_type;
		tlv_type >>= 9;
		tlv_len &= 0x01ff;
		offset += 2*sizeof(u16);
		ilen -= 2*sizeof(u16);

		if (ilen < 2*tlv_len) {
			printf("corrupted TLV\n");
			break;
		}

		tlvid = get_tlvid(tlv_type, ibuf+offset);

		if (tlvid > INVALID_TLVID)
			offset += 8;
		
		printed = 0;
		LIST_FOREACH(np, &lldp_head, lldp) {
			if (np->ops->print_tlv)
				if (np->ops->print_tlv(tlvid, tlv_len,
					ibuf+offset)) {
					printed = 1;
					break;
				}
		}

		if (!printed) {
			if (tlvid < INVALID_TLVID)
				printf("Unidentified TLV\n\ttype:%d %*.*s\n",
					tlv_type, tlv_len*2, tlv_len*2,
					ibuf+offset);
			else
				printf("Unidentified Org Specific TLV\n\t"
				      "OUI: 0x%06x, Subtype: %d, Info: %*.*s\n",
					tlvid >> 8, tlvid & 0x0ff,
					tlv_len*2-8, tlv_len*2-8,
					ibuf+offset);
		}

		if (tlvid > INVALID_TLVID)
			offset += (2*tlv_len - 8);
		else
			offset += 2*tlv_len;
		ilen -= 2*tlv_len;

		if (tlvid == END_OF_LLDPDU_TLV)
			break;
	}
}

static void print_port_stats(struct cmd *cmd, char *ibuf)
{
	static char *stat_names[] = {
		"Total Frames Transmitted       ",
		"Total Discarded Frames Received",
		"Total Error Frames Received    ",
		"Total Frames Received          ",
		"Total Discarded TLVs           ",
		"Total Unrecognized TLVs        ",
		"Total Ageouts                  ",
		"" };
	int i;
	int offset = 0;
	u32 value;

	for(i = 0; strlen(stat_names[i]); i++) {
		hexstr2bin(ibuf+offset, (u8 *)&value, sizeof(value));
		value = ntohl(value);
		printf("%s = %u\n", stat_names[i], value);
		offset += 8;
	}
}

void print_cmd_response(char *ibuf, int status)
{
	struct cmd cmd;
	u8 len;
	int ioff;

	if (status != cmd_success) {
		printf("%s\n", print_status(status));
		return;
	}

	hexstr2bin(ibuf+CMD_CODE, (u8 *)&cmd.cmd, sizeof(cmd.cmd));
	hexstr2bin(ibuf+CMD_OPS, (u8 *)&cmd.ops, sizeof(cmd.ops));
	cmd.ops = ntohl(cmd.ops);
	hexstr2bin(ibuf+CMD_IF_LEN, &len, sizeof(len));
	ioff = CMD_IF;
	if (len < sizeof(cmd.ifname)) {
		memcpy(cmd.ifname, ibuf+CMD_IF, len);
	} else {
		printf("Response ifname too long: %*s\n", (int)len, cmd.ifname);
		return;
	}
	cmd.ifname[len] = '\0';
	ioff += len;

	if (cmd.cmd == cmd_gettlv || cmd.cmd == cmd_settlv) {
		hexstr2bin(ibuf+ioff, (u8 *)&cmd.tlvid, sizeof(cmd.tlvid));
		cmd.tlvid = ntohl(cmd.tlvid);
		ioff += 2*sizeof(cmd.tlvid);
	}

	switch (cmd.cmd) {
	case cmd_getstats:
		print_port_stats(&cmd, ibuf+ioff);
		break;
	case cmd_gettlv:
		print_tlvs(&cmd, ibuf+ioff);
		break;
	case cmd_get_lldp:
		print_lldp(&cmd, ibuf+ioff);
		break;
	case cmd_settlv:
	case cmd_set_lldp:
	default:
		return;
	}
}

void print_response(char *buf, int status)
{
	switch(buf[CLIF_RSP_OFF]) {
	case PING_CMD:
		if (status)
			printf("FAILED:%s\n", print_status(status));
		else
			printf("%s\n", buf+CLIF_RSP_OFF);
		break;
	case ATTACH_CMD:
	case DETACH_CMD:
	case LEVEL_CMD:
		if (status)
			printf("FAILED:%s\n", print_status(status));
		else
			printf("OK\n");
		break;
	case CMD_REQUEST:
		print_cmd_response(buf+CLIF_RSP_OFF, status);
		break;
	default:
		printf("Unknown LLDP command response: %s\n", buf);
		break;
	}

	return;
}

void print_event_msg(char *buf)
{
	int level;

	printf("\nEvent Message\n");

	level = buf[EV_LEVEL_OFF] & 0x0f;

	printf("Level:    \t");
	switch (level) {
	case MSG_MSGDUMP:
		printf("DUMP\n");
		break;
	case MSG_DEBUG:
		printf("DEBUG\n");
		break;
	case MSG_INFO:
		printf("INFO\n");
		break;
	case MSG_WARNING:
		printf("WARNING\n");
		break;
	case MSG_ERROR:
		printf("ERROR\n");
		break;
	case MSG_EVENT:
		printf("LLDP EVENT\n");
		break;
	default:
		printf("Unknown event message: %d", level);
		return;
	}

	if (level != MSG_EVENT) {
		printf("Message:  \t%s\n\n", buf+EV_GENMSG_OFF);
		return;
	} else {
		int cmd = atoi(&buf[EV_GENMSG_OFF]);
		switch (cmd) {
		case LLDP_RCHANGE:
			printf("Message:   \tRemote Change\n");
			break;
		default:
			printf("Message:   \tUnknown Event Command\n");
			break;
		}
	}
}
