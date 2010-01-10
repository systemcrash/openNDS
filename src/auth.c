/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/* $Id: auth.c 1104 2006-10-09 00:58:46Z acv $ */
/** @file auth.c
    @brief Authentication handling thread
    @author Copyright (C) 2004 Alexandre Carmel-Veilleux <acv@miniguru.ca>
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <syslog.h>

#include "httpd.h"
#include "http.h"
#include "safe.h"
#include "conf.h"
#include "debug.h"
#include "auth.h"
#include "fw_iptables.h"
#include "firewall.h"
#include "client_list.h"
#include "util.h"

/* Defined in clientlist.c */
extern	pthread_mutex_t	client_list_mutex;

/* Defined in util.c */
extern long authenticated_this_session;

/** Launches a thread that periodically checks if any of the connections has timed out
@param arg Must contain a pointer to a string containing the IP adress of the client to check to check
@todo Also pass MAC adress? 
@todo This thread loops infinitely, need a watchdog to verify that it is still running?
*/  
void
thread_client_timeout_check(void *arg)
{
	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t		cond_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct	timespec	timeout;
	
	while (1) {
		debug(LOG_DEBUG, "Running fw_refresh_client_list()");
	
		fw_refresh_client_list();

		/* Sleep for config.checkinterval seconds... */
		timeout.tv_sec = time(NULL) + config_get_config()->checkinterval;
		timeout.tv_nsec = 0;

		/* Mutex must be locked for pthread_cond_timedwait... */
		pthread_mutex_lock(&cond_mutex);
		
		/* Thread safe "sleep" */
		pthread_cond_timedwait(&cond, &cond_mutex, &timeout);

		/* No longer needs to be locked */
		pthread_mutex_unlock(&cond_mutex);
	
	}
}

/** Take action on a client, identified in a request.
 * Alter the firewall rules and client list accordingly.
@param r httpd request struct
*/
void
auth_client_action(char *ip, char *mac, t_authaction action) {
  t_client	*client;
  s_config	*config = NULL;

  LOCK_CLIENT_LIST();

  client = client_list_find_by_ip(ip);

  /* Client should already have hit the splash server and be on the client list */
  if (client == NULL) {
    debug(LOG_ERR, "Could not find client for %s", ip);
    UNLOCK_CLIENT_LIST();
    return;
  }

  /* Make sure MAC's match */
  if (strcmp(client->mac,mac)) {
    debug(LOG_ERR, "MAC's do not match: %s, %s", client->mac, mac);
    UNLOCK_CLIENT_LIST();
    return;
  }
  

  /* Prepare some variables we'll need below */
  config = config_get_config();

  switch(action) {

  case AUTH_MAKE_AUTHENTICATED:
    debug(LOG_INFO, "AUTHENTICATE  %s at %s ", client->ip, client->mac);
    if(client->fw_connection_state != FW_MARK_AUTHENTICATED) {
      client->fw_connection_state = FW_MARK_AUTHENTICATED;
      iptables_fw_access(AUTH_MAKE_AUTHENTICATED,client->ip,client->mac);
      authenticated_this_session++;
    } else {
      debug(LOG_INFO, "Nothing to do, %s at %s already authenticated", client->ip, client->mac);
    }
    break;

  case AUTH_MAKE_DEAUTHENTICATED:
    debug(LOG_INFO, "DEAUTHENTICATE  %s at %s ", client->ip, client->mac);
    if(client->fw_connection_state == FW_MARK_AUTHENTICATED) {
      iptables_fw_access(AUTH_MAKE_DEAUTHENTICATED, client->ip, client->mac);
    }
    client_list_delete(client);
    
  default:
    debug(LOG_ERR, "Unknown auth action: %d",action);
  }
  UNLOCK_CLIENT_LIST();
  return;

}


