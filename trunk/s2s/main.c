/*
 * jabberd - Jabber Open Source Server
 * Copyright (c) 2002 Jeremie Miller, Thomas Muldowney,
 *                    Ryan Eatmon, Robert Norris
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

#include "s2s.h"

static sig_atomic_t s2s_shutdown = 0;
sig_atomic_t s2s_lost_router = 0;
static sig_atomic_t s2s_logrotate = 0;

static void _s2s_signal(int signum) {
    s2s_shutdown = 1;
    s2s_lost_router = 0;
}

static void _s2s_signal_hup(int signum) {
    s2s_logrotate = 1;
}

/** store the process id */
static void _s2s_pidfile(s2s_t s2s) {
    char *pidfile;
    FILE *f;
    pid_t pid;

    pidfile = config_get_one(s2s->config, "pidfile", 0);
    if(pidfile == NULL)
        return;

    pid = getpid();

    if((f = fopen(pidfile, "w+")) == NULL) {
        log_write(s2s->log, LOG_ERR, "couldn't open %s for writing: %s", pidfile, strerror(errno));
        return;
    }

    if(fprintf(f, "%d", pid) < 0) {
        log_write(s2s->log, LOG_ERR, "couldn't write to %s: %s", pidfile, strerror(errno));
        return;
    }

    fclose(f);

    log_write(s2s->log, LOG_INFO, "process id is %d, written to %s", pid, pidfile);
}

/** pull values out of the config file */
static void _s2s_config_expand(s2s_t s2s) {
    char *str, secret[41];
    int i, r;

    s2s->id = config_get_one(s2s->config, "id", 0);
    if(s2s->id == NULL)
        s2s->id = "s2s";

    s2s->router_ip = config_get_one(s2s->config, "router.ip", 0);
    if(s2s->router_ip == NULL)
        s2s->router_ip = "127.0.0.1";

    s2s->router_port = j_atoi(config_get_one(s2s->config, "router.port", 0), 5347);

    s2s->router_user = config_get_one(s2s->config, "router.user", 0);
    if(s2s->router_user == NULL)
        s2s->router_user = "jabberd";
    s2s->router_pass = config_get_one(s2s->config, "router.pass", 0);
    if(s2s->router_pass == NULL)
        s2s->router_pass = "secret";

    s2s->router_pemfile = config_get_one(s2s->config, "router.pemfile", 0);

    s2s->retry_init = j_atoi(config_get_one(s2s->config, "router.retry.init", 0), 3);
    s2s->retry_lost = j_atoi(config_get_one(s2s->config, "router.retry.lost", 0), 3);
    if((s2s->retry_sleep = j_atoi(config_get_one(s2s->config, "router.retry.sleep", 0), 2)) < 1)
        s2s->retry_sleep = 1;

    s2s->router_default = config_count(s2s->config, "router.non-default") ? 0 : 1;

    s2s->log_type = log_STDOUT;
    if(config_get(s2s->config, "log") != NULL) {
        if((str = config_get_attr(s2s->config, "log", 0, "type")) != NULL) {
            if(strcmp(str, "file") == 0)
                s2s->log_type = log_FILE;
            else if(strcmp(str, "syslog") == 0)
                s2s->log_type = log_SYSLOG;
        }
    }

    if(s2s->log_type == log_SYSLOG) {
        s2s->log_facility = config_get_one(s2s->config, "log.facility", 0);
        s2s->log_ident = config_get_one(s2s->config, "log.ident", 0);
        if(s2s->log_ident == NULL)
            s2s->log_ident = "jabberd/s2s";
    } else if(s2s->log_type == log_FILE)
        s2s->log_ident = config_get_one(s2s->config, "log.file", 0);

    s2s->local_ip = config_get_one(s2s->config, "local.ip", 0);
    if(s2s->local_ip == NULL)
        s2s->local_ip = "0.0.0.0";

    s2s->local_port = j_atoi(config_get_one(s2s->config, "local.port", 0), 0);

    s2s->local_resolver = config_get_one(s2s->config, "local.resolver", 0);
    if(s2s->local_resolver == NULL)
        s2s->local_resolver = "resolver";

    if(config_get(s2s->config, "local.secret") != NULL)
        s2s->local_secret = strdup(config_get_one(s2s->config, "local.secret", 0));
    else {
        for(i = 0; i < 40; i++) {
            r = (int) (36.0 * rand() / RAND_MAX);
            secret[i] = (r >= 0 && r <= 9) ? (r + 48) : (r + 87);
        }
        secret[40] = '\0';

        s2s->local_secret = strdup(secret);
    }

    if(s2s->local_secret == NULL)
        s2s->local_secret = "secret";

    s2s->local_pemfile = config_get_one(s2s->config, "local.pemfile", 0);
    if (s2s->local_pemfile != NULL)
 	log_debug(ZONE,"loaded local pemfile for peer s2s connections");

    s2s->check_interval = j_atoi(config_get_one(s2s->config, "check.interval", 0), 60);
    s2s->check_queue = j_atoi(config_get_one(s2s->config, "check.queue", 0), 60);
    s2s->check_keepalive = j_atoi(config_get_one(s2s->config, "check.keepalive", 0), 0);
    s2s->check_idle = j_atoi(config_get_one(s2s->config, "check.idle", 0), 86400);

}

static int _s2s_router_connect(s2s_t s2s) {
    log_write(s2s->log, LOG_NOTICE, "attempting connection to router at %s, port=%d", s2s->router_ip, s2s->router_port);

    if(s2s->fd != NULL)
        mio_close(s2s->mio, s2s->fd);

    s2s->fd = mio_connect(s2s->mio, s2s->router_port, s2s->router_ip, s2s_router_mio_callback, (void *) s2s);
    if(s2s->fd == NULL) {
        if(errno == ECONNREFUSED)
            s2s_lost_router = 1;
        log_write(s2s->log, LOG_NOTICE, "connection attempt to router failed: %s (%d)", MIO_STRERROR(MIO_ERROR), MIO_ERROR);
        return 1;
    }

    s2s->router = sx_new(s2s->sx_env, s2s->fd->fd, s2s_router_sx_callback, (void *) s2s);
    sx_client_init(s2s->router, 0, NULL, NULL, NULL, "1.0");

    return 0;
}

int _s2s_check_conn_routes(s2s_t s2s, conn_t conn, const char *direction)
{
  char *rkey;
  conn_state_t state;
  time_t now, dialback_time;

  now = time(NULL);

  if(xhash_iter_first(conn->states))
     do {
           /* retrieve state in a separate operation, as sizeof(int) != sizeof(void *) on 64-bit platforms,
              so passing a pointer to state in xhash_iter_get is unsafe */
           xhash_iter_get(conn->states, (const char **) &rkey, NULL);
           state = (conn_state_t) xhash_get(conn->states, rkey);

           if (state == conn_INPROGRESS) {
              dialback_time = (time_t) xhash_get(conn->states_time, rkey);

              if(now > dialback_time + s2s->check_queue) {
                 log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] dialback for %s route '%s' timed out", conn->fd->fd, conn->ip, conn->port, direction, rkey);

                 xhash_zap(conn->states, rkey);
                 xhash_zap(conn->states_time, rkey);

                 /* stream error */
                 sx_error(conn->s, stream_err_CONNECTION_TIMEOUT, "dialback timed out");

                 /* close connection as per XMPP/RFC3920 */
                 sx_close(conn->s);

                 /* indicate that we closed the connection */
                 return 0;
              }
           }
     } while(xhash_iter_next(conn->states));

  /* all ok */
  return 1;
}

static void _s2s_time_checks(s2s_t s2s) {
    conn_t conn;
    time_t now;
    char *domain, ipport[INET6_ADDRSTRLEN + 17], *key;
    jqueue_t q;
    dnscache_t dns;
    union xhashv xhv;

    now = time(NULL);

    /* queue expiry */
    if(s2s->check_queue > 0) {
        if(xhash_iter_first(s2s->outq))
            do {
                xhv.jq_val = &q;
                xhash_iter_get(s2s->outq, (const char **) &domain, xhv.val);

                log_debug(ZONE, "running time checks for %s", domain);

                /* dns lookup timeout check first */
                dns = xhash_get(s2s->dnscache, domain);
                if(dns == NULL)
                    continue;

                if(dns->pending) {
                    log_debug(ZONE, "dns lookup pending for %s", domain);
                    if(now > dns->init_time + s2s->check_queue) {
                        log_write(s2s->log, LOG_NOTICE, "dns lookup for %s timed out", domain);

                        /* bounce queue */
                        out_bounce_queue(s2s, domain, stanza_err_REMOTE_SERVER_NOT_FOUND);

                        /* expire pending dns entry */
                        xhash_zap(s2s->dnscache, dns->name);
                        free(dns);
                    }

                    continue;
                }

                /* generate the ip/port pair */
                snprintf(ipport, INET6_ADDRSTRLEN + 16, "%s/%d", dns->ip, dns->port);

                /* get the conn */
                conn = xhash_get(s2s->out, ipport);
                if(conn == NULL) {
                    if(jqueue_size(q) > 0) {
                       /* no pending conn? perhaps it failed? */
                       log_debug(ZONE, "no pending connection for %s, bouncing %i packets in queue", domain, jqueue_size(q));

                       /* bounce queue */
                       out_bounce_queue(s2s, domain, stanza_err_REMOTE_SERVER_TIMEOUT);
                    }

                    continue;
                }

                /* connect timeout check */
                if(!conn->online && now > conn->init_time + s2s->check_queue) {
                    log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] connection to %s timed out", conn->fd->fd, conn->ip, conn->port, domain);

                    /* bounce queue */
                    out_bounce_queue(s2s, domain, stanza_err_REMOTE_SERVER_TIMEOUT);

                    /* close connection as per XMPP/RFC3920 */
                    sx_close(conn->s);

                }
            } while(xhash_iter_next(s2s->outq));
    }

    /* expiry of connected routes in conn_INPROGRESS state */
    if(s2s->check_queue > 0) {

        /* outgoing connections */
        if(xhash_iter_first(s2s->out))
            do {
                xhv.conn_val = &conn;
                xhash_iter_get(s2s->out, (const char **) &key, xhv.val);
                log_debug(ZONE, "checking dialback state for outgoing conn %s", key);
                if (_s2s_check_conn_routes(s2s, conn, "outgoing")) {
                    log_debug(ZONE, "checking pending verify requests for outgoing conn %s", key);
                    if (conn->verify > 0 && now > conn->last_verify + s2s->check_queue) {
                        log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] dialback verify request timed out", conn->fd->fd, conn->ip, conn->port);
                        sx_error(conn->s, stream_err_CONNECTION_TIMEOUT, "dialback verify request timed out");
                        sx_close(conn->s);
                    }
                }
            } while(xhash_iter_next(s2s->out));

        /* incoming open streams */
        if(xhash_iter_first(s2s->in))
            do {
                xhv.conn_val = &conn;
                xhash_iter_get(s2s->in, (const char **) &key, xhv.val);

                log_debug(ZONE, "checking dialback state for incoming conn %s", key);
                if (_s2s_check_conn_routes(s2s, conn, "incoming"))
                    /* if the connection is still valid, check that dialbacks have been initiated */
                    if(!xhash_count(conn->states) && now > conn->init_time + s2s->check_queue) {
                        log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] no dialback started", conn->fd->fd, conn->ip, conn->port); 
                        sx_error(conn->s, stream_err_CONNECTION_TIMEOUT, "no dialback initiated");
                        sx_close(conn->s);
                    }
            } while(xhash_iter_next(s2s->in));

        /* incoming open connections (not yet streams) */
        if(xhash_iter_first(s2s->in_accept))
            do {
                xhv.conn_val = &conn;
                xhash_iter_get(s2s->in_accept, (const char **) &key, xhv.val);

                log_debug(ZONE, "checking stream connection state for incoming conn %i", conn->fd->fd);
                if(!conn->online && now > conn->init_time + s2s->check_queue) {
                    log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] stream initiation timed out", conn->fd->fd, conn->ip, conn->port);
                    sx_close(conn->s);
                }
            } while(xhash_iter_next(s2s->in_accept));

    }

    /* keepalives */
    if(xhash_iter_first(s2s->out))
        do {
            xhv.conn_val = &conn;
            xhash_iter_get(s2s->out, NULL, xhv.val);

            if(s2s->check_keepalive > 0 && conn->last_activity > 0 && now > conn->last_activity + s2s->check_keepalive && conn->s->state >= state_STREAM) {
                log_debug(ZONE, "sending keepalive for %d", conn->fd->fd);

                sx_raw_write(conn->s, " ", 1);
            }
        } while(xhash_iter_next(s2s->out));

    /* idle timeouts - disconnect connections through which no packets have been sent for <idle> seconds */
    if(s2s->check_idle > 0) {

       /* outgoing connections */
        if(xhash_iter_first(s2s->out))
            do {
                xhv.conn_val = &conn;
                xhash_iter_get(s2s->out, (const char **) &key, xhv.val);
                log_debug(ZONE, "checking idle state for %s", key);
                if (conn->last_packet > 0 && now > conn->last_packet + s2s->check_idle && conn->s->state >= state_STREAM) {
                    log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] idle timeout", conn->fd->fd, conn->ip, conn->port);
                    sx_close(conn->s);
                }
            } while(xhash_iter_next(s2s->out));

       /* incoming connections */
        if(xhash_iter_first(s2s->in))
            do {
                xhv.conn_val = &conn;
                xhash_iter_get(s2s->in, (const char **) &key, xhv.val);
                log_debug(ZONE, "checking idle state for %s", key);
                if (conn->last_packet > 0 && now > conn->last_packet + s2s->check_idle && conn->s->state >= state_STREAM) {
                    log_write(s2s->log, LOG_NOTICE, "[%d] [%s, port=%d] idle timeout", conn->fd->fd, conn->ip, conn->port);
                    sx_close(conn->s);
                }
            } while(xhash_iter_next(s2s->in));

    }

    return;
}

JABBER_MAIN("jabberd2s2s", "Jabber 2 S2S", "Jabber Open Source Server: Server to Server", "jabberd2router\0")
{
    s2s_t s2s;
    char *config_file;
    int optchar;
    conn_t conn;
    jqueue_t q;
    dnscache_t dns;
    union xhashv xhv;
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

    jabber_signal(SIGINT, _s2s_signal);
    jabber_signal(SIGTERM, _s2s_signal);
#ifdef SIGHUP
    jabber_signal(SIGHUP, _s2s_signal_hup);
#endif
#ifdef SIGPIPE
    jabber_signal(SIGPIPE, SIG_IGN);
#endif

    s2s = (s2s_t) malloc(sizeof(struct s2s_st));
    memset(s2s, 0, sizeof(struct s2s_st));

    /* load our config */
    s2s->config = config_new();

    config_file = CONFIG_DIR "/s2s.xml";

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
                printf("WARN: Debugging not enabled.  Ignoring -D.\n");
#endif
                break;
            case 'h': case '?': default:
                fputs(
                    "s2s - jabberd server-to-server connector (" VERSION ")\n"
                    "Usage: s2s <options>\n"
                    "Options are:\n"
                    "   -c <config>     config file to use [default: " CONFIG_DIR "/s2s.xml]\n"
#ifdef DEBUG
                    "   -D              Show debug output\n"
#endif
                    ,
                    stdout);
                config_free(s2s->config);
                free(s2s);
                return 1;
        }
    }

    if(config_load(s2s->config, config_file) != 0) {
        fputs("s2s: couldn't load config, aborting\n", stderr);
        config_free(s2s->config);
        free(s2s);
        return 2;
    }

    _s2s_config_expand(s2s);

    s2s->log = log_new(s2s->log_type, s2s->log_ident, s2s->log_facility);
    log_write(s2s->log, LOG_NOTICE, "starting up (interval=%i, queue=%i, keepalive=%i, idle=%i)", s2s->check_interval, s2s->check_queue, s2s->check_keepalive, s2s->check_idle);

    _s2s_pidfile(s2s);

    s2s->outq = xhash_new(401);
    s2s->out = xhash_new(401);
    s2s->in = xhash_new(401);
    s2s->in_accept = xhash_new(401);
    s2s->dnscache = xhash_new(401);

    s2s->pc = prep_cache_new();

    s2s->dead = jqueue_new();
    s2s->dead_conn = jqueue_new();

    s2s->sx_env = sx_env_new();

#ifdef HAVE_SSL
    /* get the ssl context up and running */
    if(s2s->local_pemfile != NULL) {
        s2s->sx_ssl = sx_env_plugin(s2s->sx_env, sx_ssl_init, s2s->local_pemfile, s2s->local_cachain, s2s->local_verify_mode);

        if(s2s->sx_ssl == NULL) {
            log_write(s2s->log, LOG_ERR, "failed to load local SSL pemfile, SSL will not be available to peers");
            s2s->local_pemfile = NULL;
        } else
            log_debug(ZONE, "loaded pemfile for SSL connections to peers");
    }

    /* try and get something online, so at least we can encrypt to the router */
    if(s2s->sx_ssl == NULL && s2s->router_pemfile != NULL) {
        s2s->sx_ssl = sx_env_plugin(s2s->sx_env, sx_ssl_init, s2s->router_pemfile, NULL, NULL);
        if(s2s->sx_ssl == NULL) {
            log_write(s2s->log, LOG_ERR, "failed to load router SSL pemfile, channel to router will not be SSL encrypted");
            s2s->router_pemfile = NULL;
        }
    }
#endif

    /* get sasl online */
    s2s->sx_sasl = sx_env_plugin(s2s->sx_env, sx_sasl_init, "xmpp", NULL, NULL);
    if(s2s->sx_sasl == NULL) {
        log_write(s2s->log, LOG_ERR, "failed to initialise SASL context, aborting");
        exit(1);
    }
            
    s2s->sx_db = sx_env_plugin(s2s->sx_env, s2s_db_init);

    s2s->mio = mio_new(MIO_MAXFD);

    s2s->retry_left = s2s->retry_init;
    _s2s_router_connect(s2s);

    while(!s2s_shutdown) {
        mio_run(s2s->mio, 5);

        if(s2s_logrotate) {
            log_write(s2s->log, LOG_NOTICE, "reopening log ...");
            log_free(s2s->log);
            s2s->log = log_new(s2s->log_type, s2s->log_ident, s2s->log_facility);
            log_write(s2s->log, LOG_NOTICE, "log started");

            s2s_logrotate = 0;
        }

        if(s2s_lost_router) {
            if(s2s->retry_left < 0) {
                log_write(s2s->log, LOG_NOTICE, "attempting reconnect");
                sleep(s2s->retry_sleep);
                s2s_lost_router = 0;
                _s2s_router_connect(s2s);
            }

            else if(s2s->retry_left == 0) {
                s2s_shutdown = 1;
            }

            else {
                log_write(s2s->log, LOG_NOTICE, "attempting reconnect (%d left)", s2s->retry_left);
                s2s->retry_left--;
                sleep(s2s->retry_sleep);
                s2s_lost_router = 0;
                _s2s_router_connect(s2s);
            }
        }
            
        /* cleanup dead sx_ts */
        while(jqueue_size(s2s->dead) > 0)
            sx_free((sx_t) jqueue_pull(s2s->dead));

        /* cleanup dead conn_ts */
        while(jqueue_size(s2s->dead_conn) > 0) {
            conn = (conn_t) jqueue_pull(s2s->dead_conn);
            xhash_free(conn->states);
            xhash_free(conn->states_time);
            xhash_free(conn->routes);

            if(conn->key != NULL) free(conn->key);
            free(conn);
        }

        /* time checks */
        if(s2s->check_interval > 0 && time(NULL) >= s2s->next_check) {
            log_debug(ZONE, "running time checks");

            _s2s_time_checks(s2s);

            s2s->next_check = time(NULL) + s2s->check_interval;
            log_debug(ZONE, "next time check at %d", s2s->next_check);
        }

#ifdef POOL_DEBUG
        if(time(NULL) > pool_time + 60) {
            pool_stat(1);
            pool_time = time(NULL);
        }
#endif
    }

    log_write(s2s->log, LOG_NOTICE, "shutting down");

    /* close active streams gracefully  */
    xhv.conn_val = &conn;
    if(xhash_iter_first(s2s->out))
        do {
            xhash_iter_get(s2s->out, NULL, xhv.val);
            sx_error(conn->s, stream_err_SYSTEM_SHUTDOWN, "s2s shutdown");
            sx_close(conn->s);
        } while(xhash_count(s2s->out));

    if(xhash_iter_first(s2s->in))
        do {
            xhash_iter_get(s2s->in, NULL, xhv.val);
            sx_error(conn->s, stream_err_SYSTEM_SHUTDOWN, "s2s shutdown");
            sx_close(conn->s);
        } while(xhash_count(s2s->in));

    if(xhash_iter_first(s2s->in_accept))
        do {
            xhash_iter_get(s2s->in_accept, NULL, xhv.val);
            sx_close(conn->s);
        } while(xhash_count(s2s->in_accept));


    /* remove dead streams */
    while(jqueue_size(s2s->dead) > 0)
        sx_free((sx_t) jqueue_pull(s2s->dead));

    /* cleanup dead conn_ts */
    while(jqueue_size(s2s->dead_conn) > 0) {
        conn = (conn_t) jqueue_pull(s2s->dead_conn);
        xhash_free(conn->states);
        xhash_free(conn->states_time);
        xhash_free(conn->routes);

        if(conn->key != NULL) free(conn->key);
        free(conn);
    }

    /* free outgoing queues  */
    xhv.jq_val = &q;
    if(xhash_iter_first(s2s->outq))
        do {
             xhash_iter_get(s2s->outq, NULL, xhv.val);
             jqueue_free(q);
        } while(xhash_iter_next(s2s->outq));

    /* walk & free resolve queues */
    xhv.dns_val = &dns;
    if(xhash_iter_first(s2s->dnscache))
        do {
             xhash_iter_get(s2s->dnscache, NULL, xhv.val);
             free(dns);
        } while(xhash_iter_next(s2s->dnscache));

    /* close mio */
    if(s2s->fd != NULL)
        mio_close(s2s->mio, s2s->fd);

    /* free hashes */
    xhash_free(s2s->outq);
    xhash_free(s2s->out);
    xhash_free(s2s->in);
    xhash_free(s2s->in_accept);
    xhash_free(s2s->dnscache);

    prep_cache_free(s2s->pc);

    jqueue_free(s2s->dead);
    jqueue_free(s2s->dead_conn);

    sx_free(s2s->router);

    sx_env_free(s2s->sx_env);

    mio_free(s2s->mio);

    log_free(s2s->log);

    config_free(s2s->config);

    free(s2s->local_secret);
    free(s2s);

#ifdef POOL_DEBUG
    pool_stat(1);
#endif

    return 0;
}