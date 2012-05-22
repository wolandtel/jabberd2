/*
 * jabberd - Jabber Open Source Server
 * Copyright (c) 2012 Tomasz Sterna
 *
 * This file is part of jabberd.
 *
 * Jabberd is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Jabberd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Jabberd; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*! \mainpage jabberd - Jabber Open Source Server
 *
 * \section intro Introduction
 *
 * The jabberd project aims to provide an open-source server
 * implementation of the Jabber protocols for instant messaging
 * and XML routing. The goal of this project is to provide a
 * scalable, reliable, efficient and extensible server that
 * provides a complete set of features and is up to date with
 * the latest protocol revisions.
 *
 * The project web page:\n
 * http://jabberd2.org/
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "mio/mio.h"
#include "util/util.h"

#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

typedef struct xmppd_st     *xmppd_t;
typedef struct alias_st     *alias_t;

typedef struct acl_s *acl_t;
struct acl_s {
    int error;
    char *redirect;
    int redirect_len;
    char *what;
    char *from;
    char *to;
    int log;
    acl_t next;
};

struct xmppd_st {
    /** config */
    config_t            config;

    /** filter table */
    acl_t               filter;
    time_t              filter_load;

    /** logging */
    log_t               log;

    /** log data */
    log_type_t          log_type;
    const char         *log_facility;
    const char         *log_ident;

    /** max file descriptors */
    int                 io_max_fds;

    /** managed io */
    mio_t               mio;

    /** default route, only one */
    const char         *default_route;

    /** log sinks, key is route name, var is component_t */
    xht                 log_sinks;

    /** configured aliases */
    alias_t             aliases;

    /** access control lists */
    xht                 aci;

    /** simple message logging */
	int                 message_logging_enabled;
	const char         *message_logging_file;
};

struct alias_st {
    char                *name;
    char                *target;

    alias_t             next;
};

int     xmppd_mio_callback(mio_t m, mio_action_t a, mio_fd_t fd, void *data, void *arg);

xht     aci_load(xmppd_t r);
void    aci_unload(xht aci);
int     aci_check(xht acls, const char *type, const char *name);

int     filter_load(xmppd_t r);
void    filter_unload(xmppd_t r);
int     filter_packet(xmppd_t r, nad_t nad);

int     message_log(nad_t nad, xmppd_t r, const unsigned char *msg_from, const unsigned char *msg_to);
