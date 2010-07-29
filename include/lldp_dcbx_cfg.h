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

#ifndef _LLDP_DCBX_CFG_H_
#define _LLDP_DCBX_CFG_H_

#include "dcb_protocol.h"

#define DCBX_SETTING "dcbx"

void dcbx_default_cfg_file(void);
int get_dcb_enable_state(char *device_name, int *result);
dcb_result save_dcb_enable_state(char *device_name, int dcb_enable);
int get_dcbx_version(int *result);
dcb_result save_dcbx_version(int dcbx_version);

#endif // _LLDP_DCBX_CFG_H_
