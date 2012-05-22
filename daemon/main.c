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

#include "xmppd.h"

static sig_atomic_t xmppd_shutdown = 0;
static sig_atomic_t xmppd_logrotate = 0;

static void xmppd_signal_shutdown(int signum)
{
    xmppd_shutdown = 1;
}

static void xmppd_signal_logrotate(int signum)
{
    xmppd_logrotate = 1;
}

static void xmppd_signal_debug_off(int signum)
{
    set_debug_flag(0);
}

static void xmppd_signal_debug_on(int signum)
{
    set_debug_flag(1);
}

/** store the process id */
static void _xmppd_pidfile(xmppd_t x) {
    const char *pidfile;
    FILE *f;
    pid_t pid;

    pidfile = config_get_one(x->config, "pidfile", 0);
    if(pidfile == NULL)
        return;

    pid = getpid();

    if((f = fopen(pidfile, "w+")) == NULL) {
        log_write(x->log, LOG_ERR, "couldn't open %s for writing: %s", pidfile, strerror(errno));
        return;
    }

    if(fprintf(f, "%d", pid) < 0) {
        log_write(x->log, LOG_ERR, "couldn't write to %s: %s", pidfile, strerror(errno));
        fclose(f);
        return;
    }

    fclose(f);

    log_write(x->log, LOG_INFO, "process id is %d, written to %s", pid, pidfile);
}

/** pull values out of the config file */
static void _xmppd_config_expand(xmppd_t x)
{
    char *str, *name, *target;
    config_elem_t elem;
    int i;
    alias_t alias;

    set_debug_log_from_config(x->config);

    x->log_type = log_STDOUT;
    if(config_get(x->config, "log") != NULL) {
        if((str = config_get_attr(x->config, "log", 0, "type")) != NULL) {
            if(strcmp(str, "file") == 0)
                x->log_type = log_FILE;
            else if(strcmp(str, "syslog") == 0)
                x->log_type = log_SYSLOG;
        }
    }

    if(x->log_type == log_SYSLOG) {
        x->log_facility = config_get_one(x->config, "log.facility", 0);
        x->log_ident = config_get_one(x->config, "log.ident", 0);
        if(x->log_ident == NULL)
            x->log_ident = "jabberd/xmppd";
    } else if(x->log_type == log_FILE)
        x->log_ident = config_get_one(x->config, "log.file", 0);

    x->io_max_fds = j_atoi(config_get_one(x->config, "io.max_fds", 0), 1024);

    /* aliases */
    elem = config_get(x->config, "aliases.alias");
    if(elem != NULL)
        for(i = 0; i < elem->nvalues; i++) {
            name = j_attr((const char **) elem->attrs[i], "name");
            target = j_attr((const char **) elem->attrs[i], "target");

            if(name == NULL || target == NULL)
                continue;

            alias = (alias_t) calloc(1, sizeof(struct alias_st));

            alias->name = name;
            alias->target = target;

            alias->next = x->aliases;
            x->aliases = alias;
        }
}

JABBER_MAIN("jabberd2daemon", "Jabber 2 Daemon", "Jabber Open Source Server: Daemon", NULL)
{
    xmppd_t x;
    char *config_file;
    int optchar;

#ifdef POOL_DEBUG
    time_t pool_time = 0;
#endif

#ifdef HAVE_UMASK
    umask((mode_t) 0027);
#endif

    srand(time(NULL));

#ifdef HAVE_WINSOCK2_H
/* get winsock running */
    {
        WORD wVersionRequested;
        WSADATA wsaData;
        int err;

        wVersionRequested = MAKEWORD( 2, 2 );

        err = WSAStartup( wVersionRequested, &wsaData );
        if ( err != 0 ) {
            /* !!! tell user that we couldn't find a usable winsock dll */
            return 0;
        }
    }
#endif

    jabber_signal(SIGINT, xmppd_signal_shutdown);
    jabber_signal(SIGTERM, xmppd_signal_shutdown);
#ifdef SIGHUP
    jabber_signal(SIGHUP, xmppd_signal_logrotate);
#endif
#ifdef SIGPIPE
    jabber_signal(SIGPIPE, SIG_IGN);
#endif
    jabber_signal(SIGUSR1, xmppd_signal_debug_off);
    jabber_signal(SIGUSR2, xmppd_signal_debug_on);

    x = (xmppd_t) calloc(1, sizeof(struct xmppd_st));

    /* load our config */
    x->config = config_new();

    config_file = CONFIG_DIR "/jabberd.xml";

    /* cmdline parsing */
    while((optchar = getopt(argc, argv, "Dc:h?")) >= 0)
    {
        switch(optchar)
        {
            case 'c':
                config_file = optarg;
                break;
            case 'D':
#ifdef DEBUG
                set_debug_flag(1);
#else
                fprintf(stderr, "WARN: Debugging not enabled.  Ignoring -D.\n");
#endif
                break;
            case 'h': case '?': default:
                fputs(
                    "xmppd - jabberd daemon (" VERSION ")\n"
                    "Usage: xmppd <options>\n"
                    "Options are:\n"
                    "   -c <config>     config file to use [default: " CONFIG_DIR "/jabberd.xml]\n"
#ifdef DEBUG
                    "   -D              Show debug output\n"
#endif
                    ,
                    stdout);
                config_free(x->config);
                free(x);
                return 1;
        }
    }

    if(config_load(x->config, config_file) != 0)
    {
        fputs("xmppd: couldn't load config, aborting\n", stderr);
        config_free(x->config);
        free(x);
        return 2;
    }

    _xmppd_config_expand(x);

    x->log = log_new(x->log_type, x->log_ident, x->log_facility);
    log_write(x->log, LOG_NOTICE, "starting up");

    _xmppd_pidfile(x);

    x->aci = aci_load(x);

    if(filter_load(x)) exit(1);

    x->log_sinks = xhash_new(101);

    x->mio = mio_new(x->io_max_fds);

    while(!xmppd_shutdown)
    {
        mio_run(x->mio, 5);

        if(xmppd_logrotate)
        {
            set_debug_log_from_config(x->config);

            log_write(x->log, LOG_NOTICE, "reopening log ...");
            log_free(x->log);
            x->log = log_new(x->log_type, x->log_ident, x->log_facility);
            log_write(x->log, LOG_NOTICE, "log started");

            log_write(x->log, LOG_NOTICE, "reloading filter ...");
            filter_unload(x);
            filter_load(x);

            // TODO - reload plugins

            xmppd_logrotate = 0;
        }

        /* time checks */
        // TODO - cal expired plugin time checks

#ifdef POOL_DEBUG
        if(time(NULL) > pool_time + 60) {
            pool_stat(1);
            pool_time = time(NULL);
        }
#endif
    }

    log_write(x->log, LOG_NOTICE, "shutting down");

    xhash_free(x->log_sinks);

    /* unload acls */
    aci_unload(x->aci);

    /* unload filter */
    filter_unload(x);

    log_free(x->log);

    config_free(x->config);

    free(x);

#ifdef POOL_DEBUG
    pool_stat(1);
#endif

#ifdef HAVE_WINSOCK2_H
    WSACleanup();
#endif

    return 0;
}
