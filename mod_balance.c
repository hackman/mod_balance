#include "mod_balance.h"

module MODULE_VAR_EXPORT balance_module;

static unsigned int my_hash(int val) {
	unsigned int hash = 166573;
	hash =+ val;
	return hash;
}

static void balance_init(server_rec *s, pool *p) {
#ifdef BALANCE_DEBUG
  ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, s, "[mod_balance] Initializing...");
#endif
  /* Add our banner to the ServerBanner */
  ap_add_version_component(BALANCE_VERSION);
}

static void *balance_create_server_config(pool *p, server_rec *s) {
	balance_config *cfg = (balance_config *)ap_palloc(p, sizeof(balance_config));
	cfg->dynamic_throttle	= DYNAMIC_SLEEP;
	cfg->static_throttle	= STATIC_SLEEP;
	cfg->global_conns		= GLOBAL_CONNS;
	cfg->vhost_conns		= VHOST_CONNS;
	cfg->user_conns			= USER_CONNS;
	cfg->load				= MIN_LOAD;
	return cfg;
}

static void *balance_merge_server_config(pool *p, void *overridep, void *basep) {
	balance_config *base		= (balance_config *)basep;
	balance_config *override	= (balance_config *)overridep;

	if (!override->load || override->load < 0)
		override->load = base->load;

	if (!override->user_conns || override->user_conns < 0)
		override->user_conns = base->user_conns;

	if (!override->vhost_conns || override->vhost_conns < 0)
		override->vhost_conns = base->vhost_conns;

	if (!override->global_conns || override->global_conns < 0)
		override->global_conns = base->global_conns;

	if (!override->dynamic_throttle || override->dynamic_throttle < 0)	
		override->dynamic_throttle = base->dynamic_throttle;

	if (!override->static_throttle || override->static_throttle < 0)
		override->static_throttle = base->static_throttle;

	return override;
}

static int balance_handler(request_rec *r) {
	balance_config *cfg = (balance_config *) ap_get_module_config(r->server->module_config, &balance_module);
    double loadavg[] = { 0.0, 0.0, 0.0 };
	int i;
	int throttle = 0;
	int ip_count = 0;
	int user_count = 0;
	int global_count = 0;

	/* Only handle initial requests */
	if(!ap_is_initial_req(r)) {
#ifdef BALANCE_DEBUG
		ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] not an initial request...");
#endif
		return DECLINED;
	}

	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
			"[mod_balance] Request UID: %d Request Host: %s", r->server->server_uid, r->server->server_hostname);

	if (cfg->load > 0.0 && getloadavg(loadavg, 1) > 0 && loadavg[0] > cfg->load ) { 
		ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
			"[mod_balance] Serving file %s throttled because of load %.2f", r->filename, loadavg[0]);
		throttle = 1;
	}

	// the load is OK, we continue searching for a reason to throttle the request
	if (!throttle) {
		for (i = 0; i < HARD_SERVER_LIMIT; ++i) {
			if (ap_scoreboard_image->servers[i].status != SERVER_DEAD && 
				ap_scoreboard_image->servers[i].status != SERVER_STARTING && 
				ap_scoreboard_image->servers[i].status != SERVER_READY) {
				// check IP
				if ((strcmp(r->connection->remote_ip, ap_scoreboard_image->servers[i].client) == 0)
	#ifdef RECORD_FORWARD
					|| (strcmp(ap_table_get(r->headers_in, "X-Forwarded-For"), ap_scoreboard_image->servers[i].fwdclient) == 0)
	#endif
					) {
						ip_count++;
						if ( cfg->vhost_conns > 0 && ip_count >= cfg->vhost_conns ) {
							throttle = 1;
							break;
						}
				}
				ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r,
					"[mod_balance] Scoreboard UID: %d Host: %s", 
						ap_scoreboard_image->servers[i].vhostrec->server_uid,
						ap_scoreboard_image->servers[i].vhostrec->server_hostname);
					
				// check User
				if (r->server->server_uid == ap_scoreboard_image->servers[i].vhostrec->server_uid) {
					user_count++;
					if ( cfg->user_conns > 0 && user_count >= cfg->user_conns ) {
						throttle = 1;
						break;
					}
				}
				global_count++;
				if ( cfg->global_conns > 0 && global_count >= cfg->global_conns ) {
					throttle = 1;
					break;
				}
			}
		}
	}
#ifdef BALANCE_DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] ip_count: %d user_count: %d global_count %d", ip_count, user_count, global_count);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] apache uid: %d uid: %d", 
		ap_user_id, r->server->server_uid);
	ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] global_conns: %d vhost_conns: %d user_conns: %d min_load: %.2f", 
		cfg->global_conns, cfg->vhost_conns, cfg->user_conns, cfg->load);
#endif
	
	if ( throttle ) {
#ifdef BALANCE_DEBUG
		ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] content type: %s", r->content_type);
#endif
		if (r->content_type && strncmp(r->content_type, "application/", 12) == 0) {
#ifdef BALANCE_DEBUG
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] Throttling the connection for %d seconds", cfg->dynamic_throttle);
#endif
			usleep(cfg->dynamic_throttle * 1000000);
		} else {
#ifdef BALANCE_DEBUG
			ap_log_rerror(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, r, "[mod_balance] Throttling the connection for %d seconds", cfg->static_throttle);
#endif
			usleep(cfg->static_throttle * 1000000);
		}
		return DECLINED;
	}

	return OK;
};

int is_digit(const char *val) {
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
				return NULL;
			}
		}
		ap_log_error(APLOG_MARK, APLOG_INFO | APLOG_NOERRNO, cmd->server, "[mod_balance] parsing MinThrottleLoad %s(%f) %f", arg, atof(arg), cfg->load);
		cfg->load = atof(arg);
	}
	return NULL;
}

static const char *max_user_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val != 0)
		cfg->user_conns = val;
	return NULL;
}

static const char *max_vhost_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val != 0)
		cfg->vhost_conns = val;
	return NULL;
}

static const char *max_global_conns_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val != 0)
		cfg->global_conns = val;
	return NULL;
}

static const char *static_throttle_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val != 0)
		cfg->static_throttle = val;
	return NULL;
}

static const char *dynamic_throttle_handler(cmd_parms *cmd, void *mconfig, const char *arg) {
	balance_config *cfg = (balance_config *) ap_get_module_config(cmd->server->module_config, &balance_module);
	int val = 0;
	val = is_digit(arg);
	if (val != 0)
		cfg->dynamic_throttle = val;
	return NULL;
}

// httpd options
static const command_rec balance_cmds[] = {
	{ "MaxGlobalConnections", max_global_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed from a single ip to the whole server. Set it to 0 to disable." },
	{ "MaxUserConnections", max_user_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed for the defined user id, to the whole server. Set it to 0 to disable." },
	{ "MaxVhostConnections", max_vhost_conns_handler, NULL, RSRC_CONF, TAKE1,
		"maximum number of connections allowed from a single ip to a single vhost. Set it to 0 to disable." },
	{ "MinThrottleLoad", min_load_handler, NULL, RSRC_CONF, TAKE1,
		"minimum server load on which we trigger throttling. Set it to 0 to disable." },
	{ "StaticContentThrottle", static_throttle_handler, NULL, RSRC_CONF, TAKE1,
		"time in seconds for which we will wait when the content is not dynamic. Set it to 0 to disable." },
	{ "DynamicContentThrottle", dynamic_throttle_handler, NULL, RSRC_CONF, TAKE1,
		"time in seconds for which we will wait when the content is dynamic(PHP/Perl/Python/any type of CGI). Set it to 0 to disable." },
	{ NULL }
};

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

