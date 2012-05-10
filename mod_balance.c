/*
 * Copyright (C) 2012 Marian Marinov <mm@yuhu.biz>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "mod_balance.h"

#ifdef APACHE2
module AP_MODULE_DECLARE_DATA balance_module;
#else
module MODULE_VAR_EXPORT balance_module;
#endif

#ifdef APACHE2
static int balance_init(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
	ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
	ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
	/* Add our banner to the ServerBanner */
	ap_add_version_component(pconf, BALANCE_VERSION);
#else
static void balance_init(server_rec *s, pool *p) {
	/* Add our banner to the ServerBanner */
	ap_add_version_component(BALANCE_VERSION);
#endif // APACHE2
#ifdef BALANCE_DEBUG
#ifdef APACHE2
	ap_log_error(APLOG_MARK, APLOG_INFO, OK, s, "%s loaded", BALANCE_VERSION);
#else
	ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, s, "%s loaded", BALANCE_VERSION);
#endif // APACHE2
#endif // BALANCE_DEBUG
#ifdef APACHE2
	return OK;
#endif
}

#ifdef APACHE2
static void *balance_create_server_config(apr_pool_t *p, server_rec *s) {
	balance_config *cfg = (balance_config *)apr_palloc(p, sizeof(balance_config));
#else
static void *balance_create_server_config(pool *p, server_rec *s) {
	balance_config *cfg = (balance_config *)ap_palloc(p, sizeof(balance_config));
#endif
	cfg->dynamic_throttle	= DYNAMIC_SLEEP;
	cfg->static_throttle	= STATIC_SLEEP;
	cfg->global_conns		= GLOBAL_CONNS;
	cfg->vhost_conns		= VHOST_CONNS;
	cfg->user_conns			= USER_CONNS;
	cfg->ip_conns			= IP_CONNS;
	cfg->min_load			= MIN_LOAD;
	cfg->max_load			= MAX_LOAD;
	return cfg;
}

#ifdef APACHE2
static void *balance_merge_server_config(apr_pool_t *p, void *basep, void *overridep) {
#else
static void *balance_merge_server_config(pool *p, void *basep, void *overridep) {
#endif
	balance_config *base		= (balance_config *)basep;
	balance_config *override	= (balance_config *)overridep;

	if ( override->min_load < 0 )
		override->min_load = base->min_load;

	if ( override->max_load < 0 )
		override->max_load = base->max_load;

	if ( override->ip_conns < 0 )
		override->ip_conns = base->ip_conns;

	if ( override->user_conns < 0 )
		override->user_conns = base->user_conns;

	if ( override->vhost_conns < 0 )
		override->vhost_conns = base->vhost_conns;

	if ( override->global_conns < 0 )
		override->global_conns = base->global_conns;

	if ( override->dynamic_throttle < 0 )
		override->dynamic_throttle = base->dynamic_throttle;

	if ( override->static_throttle < 0 )
		override->static_throttle = base->static_throttle;
#ifdef BALANCE_DEBUG
	printf("over->load: %.2f base->load: %.2f\nover->ip_conns: %d base->ip_conns: %d\nover->user_conns: %d base->user_conns: %d\n"
		"over->vhost_conns: %d base->vhost_conns: %d\nover->global_conns: %d base->global_conns: %d\n",
        override->load, base->load,
        override->ip_conns, base->ip_conns,
        override->user_conns, base->user_conns,
        override->vhost_conns, base->vhost_conns,
        override->global_conns, base->global_conns
    );
#endif
	return override;
}

static int balance_handler(request_rec *r) {
	balance_config *cfg = (balance_config *) ap_get_module_config(r->server->module_config, &balance_module);
    double loadavg[] = { 0.0, 0.0, 0.0 };
	int i;
	int throttle = 0;
	int ip_count = 0;
	int vhost_count = 0;
	int global_count = 0;
#ifdef APACHE2
	int j;
	worker_score *ws_record;
#else
	int user_count = 0;
	server_rec *vhost = NULL;
#endif
	/* Only handle initial requests */
	if(!ap_is_initial_req(r)) {
#ifdef BALANCE_DEBUG
#ifdef APACHE2
		ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] not an initial request...");
#else
		ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] not an initial request...");
#endif // APACHE2
#endif // BALANCE_DEBUG
		return DECLINED;
	}

#ifdef BALANCE_DEBUG
#ifdef APACHE2
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] content type: %s handler: %s",
		r->content_type, r->handler);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] Request UID: %d Request Host: %s Hostname: %s",
		r->server->server_uid, r->server->server_hostname, r->hostname);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] apache uid: %d uid: %d",
		ap_user_id, r->server->server_uid);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] ip_count: %d user_count: %d global_count %d",
		ip_count, user_count, global_count);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] global_conns: %d vhost_conns: %d user_conns: %d min_load: %.2f",
		cfg->global_conns, cfg->vhost_conns, cfg->user_conns, cfg->load);
#else
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] content type: %s handler: %s",
		r->content_type, r->handler);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] Request UID: %d Request Host: %s Hostname: %s",
		r->server->server_uid, r->server->server_hostname, r->hostname);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] apache uid: %d uid: %d",
		ap_user_id, r->server->server_uid);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] ip_count: %d user_count: %d global_count %d",
		ip_count, user_count, global_count);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] global_conns: %d vhost_conns: %d user_conns: %d min_load: %.2f",
		cfg->global_conns, cfg->vhost_conns, cfg->user_conns, cfg->load);
#endif // APACHE2
#endif // BALANCE_DEBUG

	// check the min load
	if (getloadavg(loadavg, 1) > 0) {
		if ( cfg->min_load > 0.0 && loadavg[0] < cfg->min_load ) {
			return DECLINED;
		}
		if ( cfg->max_load > 0.0 && loadavg[0] > cfg->max_load ) {
#ifdef APACHE2
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] %s req. to %s%s reached MaxThrottleLoad(%.2f) and the load is %.2f",
				r->content_type, r->hostname, r->uri, cfg->max_load, loadavg[0]);
#else
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] %s req. to %s%s reached MaxThrottleLoad(%.2f) and the load is %.2f",
				r->content_type, r->hostname, r->uri, cfg->max_load, loadavg[0]);
#endif
			throttle = 1;
		}
	}

	// the load is OK, we continue searching for a reason to throttle the request
	if (!throttle) {
#ifdef APACHE2
		for (i = 0; i < server_limit; ++i) {
			for (j = 0; j < thread_limit; ++j) {
				ws_record = ap_get_scoreboard_worker(i, j);
				if (ws_record->status != SERVER_DEAD &&
					ws_record->status != SERVER_STARTING &&
					ws_record->status != SERVER_READY) {
					if (strcmp(r->connection->remote_ip, ws_record->client) == 0) {
#else
			for (i = 0; i < HARD_SERVER_LIMIT; ++i) {
				if (ap_scoreboard_image->servers[i].status != SERVER_DEAD &&
					ap_scoreboard_image->servers[i].status != SERVER_STARTING &&
					ap_scoreboard_image->servers[i].status != SERVER_READY) {
					// check the IP limit
					if (strcmp(r->connection->remote_ip, ap_scoreboard_image->servers[i].client) == 0) {
#endif // APACHE2
						ip_count++;
						if ( cfg->ip_conns > 0 && ip_count >= cfg->ip_conns ) {
#ifdef APACHE2
							ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r,
								"[mod_balance] %s req. to %s%s reached MaxConnsPerIP (%d)",
								r->content_type, r->hostname, r->uri, ip_count);
#else
							ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
								"[mod_balance] %s req. to %s%s reached MaxConnsPerIP (%d)",
								r->content_type, r->hostname, r->uri, ip_count);
#endif // APACHE2
							throttle = 1;
							break;
						}
					}

				global_count++;
				// check the Global connections limit
				if ( cfg->global_conns > 0 && global_count >= cfg->global_conns ) {
#ifdef APACHE2
					ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r,
						"[mod_balance] %s req. to %s%s reached MaxGlobalConnections(%d)",
						r->content_type, r->hostname, r->uri, global_count);
#else
					ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
						"[mod_balance] %s req. to %s%s reached MaxGlobalConnections(%d)",
						r->content_type, r->hostname, r->uri, global_count);
#endif // APACHE2
					throttle = 1;
					break;
				}

#ifndef APACHE2
				// Fix for a race condition which happens during child start/restart, 
				// when the vhost information is not available in the scoreboard
				// I don't understand why I need the separate vhost pointer....
				vhost = ap_scoreboard_image->servers[i].vhostrec;
				if (ap_scoreboard_image->parent[i].generation != ap_my_generation) {
					vhost = NULL;
				}

				if (vhost == NULL)
					break;
#endif // !APACHE2


				// check the VHost limit
#ifdef APACHE2
				if ( strcmp(r->server->server_hostname, ws_record->vhost ) == 0) {
#else
				if ( strcmp(r->server->server_hostname, vhost->server_hostname ) == 0) {
#endif
					vhost_count++;
					if ( cfg->vhost_conns > 0 && vhost_count >= cfg->vhost_conns ) {
#ifdef APACHE2
						ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r,
							"[mod_balance] %s req. to %s%s reached MaxVhostConnections(%d)",
							r->content_type, r->hostname, r->uri, vhost_count);
#else
						ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
							"[mod_balance] %s req. to %s%s reached MaxVhostConnections(%d)",
							r->content_type, r->hostname, r->uri, vhost_count);
#endif
						throttle = 1;
						break;
					}
				}

/* TODO port this for apache 2.x
			// request UID
			r->server->module_config->ugid.uid
			// scoreboard UID

				if (r->server->module_config-> == vhost->server_uid) {
*/

#ifndef APACHE2
				// check the User limit
				if (r->server->server_uid == vhost->server_uid) {
					user_count++;
					if ( cfg->user_conns > 0 && user_count >= cfg->user_conns ) {
						ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
							"[mod_balance] %s req. to %s%s reached MaxUserConnections(%d) of UID %d",
							r->content_type, r->hostname, r->uri, user_count, r->server->server_uid);
						throttle = 1;
						break;
					}
				}
#endif
			} // invalid child status (for this module)
#ifdef APACHE2
		} // end of the thread loop
#endif
		} // end of the child loop
	} // we already know that we have to throttle the connection
	if ( throttle ) {
		if (r->handler && strncmp(r->handler, "application/", 12) == 0) {
#ifdef BALANCE_DEBUG
#ifdef APACHE2
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] Throttling the connection for %d seconds", cfg->dynamic_throttle);
#else
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] Throttling the connection for %d seconds", cfg->dynamic_throttle);
#endif // APACHE2
#endif // BALANCE_DEBUG
			usleep(cfg->dynamic_throttle * 1000000);
		} else {
#ifdef BALANCE_DEBUG
#ifdef APACHE2
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, r, "[mod_balance] Throttling the connection for %d seconds", cfg->static_throttle);
#else
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] Throttling the connection for %d seconds", cfg->static_throttle);
#endif // APACHE2
#endif // BALANCE_DEBUG
			usleep(cfg->static_throttle * 1000000);
		}
	}
	// continue with other modules
	return DECLINED;
};

static int is_digit(const char *val) {
	int i;
	if (val)
		for(i = 0; i < strlen(val); i++) {
			if (!isdigit(val[i]))
				return 0;
		}
	return atoi(val);
}

static const char *min_load_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int i;
	if (arg) {
		for(i = 0; i < strlen(arg); i++) {
			if (!isdigit(arg[i]) && arg[i] != '.') {
				return "Invalid value for MinThrottleLoad";
			}
		}
#ifdef BALANCE_DEBUG
#ifdef APACHE2
		ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, cmd->server, "[mod_balance] parsing MinThrottleLoad %s(%f) %f", arg, atof(arg), cfg->load);
#else
		ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, cmd->server, "[mod_balance] parsing MinThrottleLoad %s(%f) %f", arg, atof(arg), cfg->load);
#endif // APACHE2
#endif // BALANCE_DEBUG
		cfg->min_load = atof(arg);
	}
	return NULL;
}

static const char *max_load_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int i;
	if (arg) {
		for(i = 0; i < strlen(arg); i++) {
			if (!isdigit(arg[i]) && arg[i] != '.') {
				return "Invalid value for MaxThrottleLoad";
			}
		}
#ifdef BALANCE_DEBUG
#ifdef APACHE2
		ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, 0, cmd->server, "[mod_balance] parsing MaxThrottleLoad %s(%f) %f", arg, atof(arg), cfg->load);
#else
		ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, cmd->server, "[mod_balance] parsing MaxThrottleLoad %s(%f) %f", arg, atof(arg), cfg->load);
#endif // APACHE2
#endif // BALANCE_DEBUG
		cfg->max_load = atof(arg);
	}
	return NULL;
}

static const char *max_ip_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->ip_conns = val;
	return NULL;
}

static const char *max_user_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->user_conns = val;
	return NULL;
}

static const char *max_vhost_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->vhost_conns = val;
	return NULL;
}

static const char *max_global_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->global_conns = val;
	return NULL;
}

static const char *static_throttle_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->static_throttle = val;
	return NULL;
}

static const char *dynamic_throttle_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val >= 0)
		cfg->dynamic_throttle = val;
	return NULL;
}

// httpd options
static const command_rec balance_cmds[] = {
#ifdef APACHE2
	AP_INIT_TAKE1( "MaxGlobalConnections", max_global_conns_handler, NULL, RSRC_CONF,
		"maximum number of connections allowed from a single ip to the whole server. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "MaxUserConnections", max_user_conns_handler, NULL, RSRC_CONF,
		"maximum number of connections allowed for the defined user id, to the whole server. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "MaxVhostConnections", max_vhost_conns_handler, NULL, RSRC_CONF,
		"maximum number of connections allowed from a single ip to a single vhost. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "MaxConnsPerIP", max_ip_conns_handler, NULL, RSRC_CONF,
		"maximum number of connections allowed from a single ip. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "MinThrottleLoad", min_load_handler, NULL, RSRC_CONF,
		"minimum server load on which we trigger throttling. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "MaxThrottleLoad", max_load_handler, NULL, RSRC_CONF,
		"maximum server load after which we trigger throttling. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "StaticContentThrottle", static_throttle_handler, NULL, RSRC_CONF,
		"time in seconds for which we will wait when the content is not dynamic. Set it to 0 to disable." ),
	AP_INIT_TAKE1( "DynamicContentThrottle", dynamic_throttle_handler, NULL, RSRC_CONF,
		"time in seconds for which we will wait when the content is dynamic(PHP/Perl/Python/any type of CGI). Set it to 0 to disable." ),
#else
	{ "MaxGlobalConnections", max_global_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed from a single ip to the whole server. Set it to 0 to disable." },
	{ "MaxUserConnections", max_user_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed for the defined user id, to the whole server. Set it to 0 to disable." },
	{ "MaxVhostConnections", max_vhost_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed from a single ip to a single vhost. Set it to 0 to disable." },
	{ "MaxConnsPerIP", max_ip_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed from a single ip. Set it to 0 to disable." },
	{ "MinThrottleLoad", min_load_handler, NULL, RSRC_CONF, TAKE1,
		"minimum server load on which we trigger throttling. Set it to 0 to disable." },
	{ "MaxThrottleLoad", max_load_handler, NULL, RSRC_CONF, TAKE1,
		"maximum server load after which we trigger throttling. Set it to 0 to disable." },
	{ "StaticContentThrottle", static_throttle_handler, NULL, RSRC_CONF, TAKE1,
		"time in seconds for which we will wait when the content is not dynamic. Set it to 0 to disable." },
	{ "DynamicContentThrottle", dynamic_throttle_handler, NULL, RSRC_CONF, TAKE1,
		"time in seconds for which we will wait when the content is dynamic(PHP/Perl/Python/any type of CGI). Set it to 0 to disable." },
#endif // APACHE2
	{ NULL }
};

#ifdef APACHE2
static void register_hooks(apr_pool_t *p) {
    static const char * const aszPre[]  = { "mod_mime.c", NULL };
	static const char * const aszPost[] = { "mod_hive.c", "mod_cgi.c", "mod_php.c", "mod_suphp.c", "mod_python.c", "mod_perl.c", NULL };
    ap_hook_header_parser(balance_handler, aszPre, aszPost, APR_HOOK_MIDDLE);
	ap_hook_post_config(balance_init, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA balance_module = {
	STANDARD20_MODULE_STUFF,
	NULL,					/* per-directory config creator */
	NULL,					/* dir config merger */
	balance_create_server_config,	/* server config creator */
	balance_merge_server_config,					/* server config merger */
	balance_cmds,			/* command table */
	register_hooks,			/* set up other request processing hooks */
};
#else
// connection handlers (define mime types)
static const handler_rec balance_handlers[] = {
	{ "*", balance_handler },
	{ "*/*", balance_handler },
	{ "cgi-script", balance_handler },
	{ NULL }
};

module MODULE_VAR_EXPORT balance_module = {
	STANDARD_MODULE_STUFF,
	balance_init,					/* module initializer                  */
	NULL,							/* create per-dir    config structures */
	NULL,							/* merge  per-dir    config structures */
	balance_create_server_config,	/* create per-server config structures */
	balance_merge_server_config,	/* merge  per-server config structures */
	balance_cmds,					/* table of config file commands       */
	NULL,				/* [#8] MIME-typed-dispatched handlers */
	NULL,						/* [#1] URI to filename translation    */
	NULL,						/* [#4] validate user id from request  */
	NULL,						/* [#5] check if the user is ok _here_ */
	NULL,						/* [#3] check access by host address   */
	NULL,						/* [#6] determine MIME type            */
	balance_handler,						/* [#7] pre-run fixups                 */
	NULL,						/* [#9] log a transaction              */
	NULL,						/* [#2] header parser                  */
	NULL,						/* child_init                          */
	NULL,						/* child_exit                          */
	NULL						/* [#0] post read-request              */
};
#endif // APACHE2
