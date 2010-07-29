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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "libconfig.h"

#define LLDP_SETTING "lldp"
#define LLDP_COMMON  "common"

#define INI_TIMER	5

#define TYPE_BOOL 0
#define TYPE_PERCENT 1
#define TYPE_INT 2
#define TYPE_PRIORITY 3
#define TYPE_CHAR 4
#define TYPE_DOUBLE 4

void scan_port(void *eloop_data, void *user_ctx);
int get_cfg(const char *ifname, char *path, void *value, int type);
int set_cfg(const char *ifname, char *path, void *value, int type);
int get_config_setting(const char *ifname, char *path, void *value, int type);
int set_config_setting(const char *ifname, char *path, void *value, int type);
int get_config_tlvfield(const char *ifname, u32 tlvid, const char *field, void *value, int type);
int get_config_tlvfield_int(const char *ifname, u32 tlvid, const char *field, int *value);
int get_config_tlvfield_bool(const char *ifname, u32 tlvid, const char *field, int *value);
int get_config_tlvfield_bin(const char *ifname, u32 tlvid, const char *field, void *value, size_t size);
int get_config_tlvfield_str(const char *ifname, u32 tlvid, const char *field, void *value, size_t size);
int get_config_tlvinfo_bin(const char *ifname, u32 tlvid, void *value, size_t size);
int get_config_tlvinfo_str(const char *ifname, u32 tlvid, void *value, size_t size);
int set_config_tlvfield(const char *ifname, u32 tlvid, const char *field, void *value, int type);
int set_config_tlvfield_int(const char *ifname, u32 tlvid, const char *field, int *value);
int set_config_tlvfield_bool(const char *ifname, u32 tlvid, const char *field, int *value);
int set_config_tlvfield_bin(const char *ifname, u32 tlvid, const char *field, void *value, size_t size);
int set_config_tlvfield_str(const char *ifname, u32 tlvid, const char *field, void *value, size_t size);
int set_config_tlvinfo_bin(const char *ifname, u32 tlvid, void *value, size_t size);
int set_config_tlvinfo_str(const char *ifname, u32 tlvid, void *value, size_t size);
int is_tlv_txenabled(const char *ifname, u32 tlvid);
int tlv_enabletx(const char *ifname, u32 tlvid);
int tlv_disabletx(const char *ifname, u32 tlvid);
int get_med_devtype(const char *ifname);
void set_med_devtype(const char *ifname, int devtype);

void create_default_cfg_file(void);
int get_int_config(config_setting_t *s, char *attr, int int_type,
				int *result);
int get_array_config(config_setting_t *s, char *attr, int int_type,
			int *result);

int init_cfg(void);
void destroy_cfg(void);
int check_cfg_file(void);
void init_ports(void);
#endif /* _CONFIG_H_ */
