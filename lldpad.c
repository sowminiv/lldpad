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

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"
#include "eloop.h"
#include "lldpad.h"
#include "event_iface.h"
#include "messages.h"
#include "version.h"
#include "lldp_mand.h"
#include "lldp_basman.h"
#include "lldp_dcbx.h"
#include "lldp_med.h"
#include "lldp_8023.h"
#include "config.h"
#include "lldpad_shm.h"
#include "clif.h"
#include "lldp/agent.h"

/*
 * insert to head, so first one is last
 */
struct lldp_module *(*register_tlv_table[])(void) = {
	mand_register,
	basman_register,
	dcbx_register,
	med_register,
	ieee8023_register,
	NULL,
};

extern u8 gdcbx_subtype;

char *cfg_file_name = NULL;

static const char *lldpad_version =
"lldpad v" VERSION_STR "\n"
"Copyright (c) 2007-2010, Intel Corporation\n"
"\nPortions used and/or modified from:  hostapd v 0.5.7\n"
"Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi> and contributors";

void init_modules(char *path)
{
	struct lldp_module *module;
	struct lldp_module *premod = NULL;
	int i = 0;

	LIST_INIT(&lldp_head);
	for (i = 0; register_tlv_table[i]; i++) {
		module = register_tlv_table[i]();
		if (!module)
			continue;
		if (premod)
			LIST_INSERT_AFTER(premod, module, lldp);
		else
			LIST_INSERT_HEAD(&lldp_head, module, lldp);
		premod = module;
	}
}

void deinit_modules(void)
{
	struct lldp_module *module;

	while (lldp_head.lh_first != NULL) {
		module = lldp_head.lh_first;
		LIST_REMOVE(lldp_head.lh_first, lldp);
		module->ops->lldp_mod_unregister(module);
	}
}

static void usage(void)
{
	fprintf(stderr,
		"\n"
		"usage: lldpad [-hdksv] [-f configfile]"
		"\n"
		"options:\n"
		"   -h  show this usage\n"
		"   -f  use configfile instead of default\n"
		"   -d  run daemon in the background\n"
		"   -k  terminate current running lldpad\n"
		"   -s  remove lldpad state records\n"
		"   -v  show version\n");

	exit(1);
}

/*
 * send_event: Send message to attach clients.
 * @moduleid - module identification of sender or 0 for legacy format
 * @msg - string encoded message
 */
void send_event(int level, u32 moduleid, char *msg)
{
	struct clif_data *cd = NULL;

	cd = (struct clif_data *) eloop_get_user_data();
	if (cd) {
		ctrl_iface_send(cd, level, moduleid, msg, strlen(msg));
	}
}


int main(int argc, char *argv[])
{
	int c, daemonize = 0;
	struct clif_data *clifd;
	int fd;
	char buf[32];
	int shm_remove = 0;
	int killme = 0;
	int print_v = 0;
	pid_t pid;
	int cnt;
	int loglvl = LOG_WARNING;

	for (;;) {
		c = getopt(argc, argv, "dhkvsf:V:");
		if (c < 0)
			break;
		switch (c) {
		case 'f':
			if (cfg_file_name) {
				usage();
				break;
			}
			cfg_file_name = strdup(optarg);
			break;
		case 'd':
			daemonize++;
			break;
		case 'k':
			killme = 1;
			break;
		case 's':
			shm_remove = 1;
			break;
		case 'v':
			print_v = 1;
			break;
		case 'V':
			loglvl = atoi(optarg);
			if (loglvl > LOG_DEBUG)
				loglvl = LOG_DEBUG;
			if (loglvl < LOG_EMERG)
				loglvl = LOG_EMERG;
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}
	/* exit if invalid input in the command line */
	if (optind < argc )
		usage();

	if (print_v) {
		printf("%s\n", lldpad_version);
		exit(0);
	}

	if (cfg_file_name == NULL) {
		cfg_file_name = DEFAULT_CFG_FILE;
	}

	if (shm_remove) {
		mark_lldpad_shm_for_removal();
		exit(0);
	}

	if (killme) {
		pid = lldpad_shm_getpid();

		if (pid < 0) {
			perror("lldpad_shm_getpid failed");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "lldpad_shm_getpid failed");
			exit (1);
		} else if (pid == PID_NOT_SET) {
			if (!lldpad_shm_setpid(DONT_KILL_PID)) {
				perror("lldpad_shm_setpid failed");
				log_message(MSG_ERR_SERVICE_START_FAILURE,
					"%s", "lldpad_shm_setpid failed");
				exit (1);
			} else {
				exit(0);
			}
		} else if (pid == DONT_KILL_PID) {
			exit (0);
		}

		if (!kill(pid, 0) && !kill(pid, SIGINT)) {
			cnt = 0;
			while (!kill(pid, 0) && cnt++ < 1000)
				usleep(10000);

			if (cnt >= 1000) {
				fprintf(stderr, "failed to kill lldpad %d\n", pid);
				log_message(MSG_ERR_SERVICE_START_FAILURE,
					"%s", "lldpad failed to die");
				exit (1);
			}
		} else {
			perror("lldpad kill failed");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "kill lldpad failed");
		}
		if (!lldpad_shm_setpid(DONT_KILL_PID)) {
			perror("lldpad_shm_setpid failed after kill");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "lldpad_shm_setpid failed after kill");
			exit (1);
		}

		destroy_cfg();
 
		exit (0);
	}

	fd = open(PID_FILE, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "error opening lldpad lock file");
		exit(1);
	}

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) {
			fprintf(stderr, "lldpad is already running\n");
			if (read(fd, buf, sizeof(buf)) > 0) {
				fprintf(stderr, "pid of existing lldpad is %s\n",
					buf);
			}
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "lldpad already running");
		} else {
			perror("error locking lldpad lock file");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "error locking lldpad lock file");
		}
		exit(1);
	}

	/* initialize lldpad user data */
	clifd = malloc(sizeof(struct clif_data));
	if (clifd == NULL) {
		fprintf(stderr, "failed to malloc user data\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "failed to malloc user data");
		exit(1);
	}

	clifd->ctrl_interface = (char *) CLIF_IFACE_DIR;
	strcpy(clifd->iface, CLIF_IFACE_IFNAME);
	clifd->ctrl_interface_gid_set = 0;
	clifd->ctrl_interface_gid = 0;

	if (eloop_init(clifd)) {
		fprintf(stderr, "failed to initialize event loop\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "failed to initialize event loop");
		exit(1);
	}

	/* initialize the client interface socket before daemonize */
	if (ctrl_iface_init(clifd) < 0) {
		fprintf(stderr, "failed to register client interface\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "failed to register client interface");
		exit(1);
	}

	if (daemonize && os_daemonize(PID_FILE)) {
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "error daemonizing lldpad");
		goto out;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		if (!daemonize)
			fprintf(stderr, "error seeking lldpad lock file\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "error seeking lldpad lock file");
		exit(1);
	}

	memset(buf, 0, sizeof(buf));
	sprintf(buf, "%u\n", getpid());
	if (write(fd, buf, sizeof(buf)) < 0)
		perror("error writing to lldpad lock file");
	if (fsync(fd) < 0)
		perror("error syncing lldpad lock file");

	pid = lldpad_shm_getpid();
	if (pid < 0) {
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "error getting shm pid");
		unlink(PID_FILE);
		exit (1);
	} else if (pid == PID_NOT_SET) {
		if (!lldpad_shm_setpid(getpid())) {
			perror("lldpad_shm_setpid failed");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "error setting shm pid");
			unlink(PID_FILE);
			exit (1);
		}
	} else if (pid != DONT_KILL_PID) {
		if (!kill(pid, 0)) {
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "lldpad already running");
			unlink(PID_FILE);
			exit(1);
		}
		/* pid in shm no longer has a process, go ahead
                 * and let this lldpad instance execute.
		 */
		if (!lldpad_shm_setpid(getpid())) {
			perror("lldpad_shm_setpid failed");
			log_message(MSG_ERR_SERVICE_START_FAILURE,
				"%s", "error overwriting shm pid");
			unlink(PID_FILE);
			exit (1);
		}
	}

	openlog("lldpad", LOG_CONS | LOG_PID, LOG_DAEMON);
	setlogmask(LOG_UPTO(loglvl));

	if (check_cfg_file()) {
		exit(1);
	}

	init_modules("");


	eloop_register_signal_terminate(eloop_terminate, NULL);

	/* setup LLDP agent */
	if (!start_lldp_agent()) {
		if (!daemonize)
			fprintf(stderr, "failed to initialize LLDP agent\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "failed to initialize LLDP agent");
		exit(1);
	}

	ctrl_iface_register(clifd);

	/* Find available interfaces, read in the lldpad.conf file and
	 * add adapters */
	init_ports();

	/* setup event RT netlink interface */
	if (event_iface_init() < 0) {
		if (!daemonize)
			fprintf(stderr, "failed to register event interface\n");
		log_message(MSG_ERR_SERVICE_START_FAILURE,
			"%s", "failed to register event interface");
		exit(1);
	}

	LLDPAD_WARN("%s is starting", argv[0]);
	eloop_run();
	LLDPAD_WARN("%s is stopping", argv[0]);

	clean_lldp_agent();
	deinit_modules();
	remove_all_adapters();
	ctrl_iface_deinit(clifd);  /* free's clifd */
	event_iface_deinit();
	stop_lldp_agent();
 out:
	destroy_cfg();
	closelog();
	unlink(PID_FILE);
	eloop_destroy();
	exit(1);
}
