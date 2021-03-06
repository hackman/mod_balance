#ifndef _MOD_BALANCE_H
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_protocol.h"
#include "http_core.h"
#include "http_main.h"
#include "http_log.h"
#include "scoreboard.h"
#include "ap_release.h"

#include <stdlib.h>		// required by atoi(), atof(), getloadavg()
#include <unistd.h>		// usleep
#include <ctype.h>		// isdigit()

#ifdef APACHE_RELEASE
#include "http_conf_globals.h"
#else
#define APACHE2
#include "ap_mpm.h"
static int server_limit, thread_limit;
#endif

#ifdef APLOG_USE_MODULE
APLOG_USE_MODULE(balance);
#endif

//#define BALANCE_DEBUG

#ifdef BALANCE_DEBUG
#include <stdio.h>
#endif

#if (AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER == 4)
#define APACHE24
#endif

#define BALANCE_VERSION "mod_balance/1.3"
#define DYNAMIC_SLEEP 5
#define STATIC_SLEEP 2
#define IP_CONNS 0
#define USER_CONNS 0
#define VHOST_CONNS 0
#define GLOBAL_CONNS 0
#define MIN_LOAD 0.00
#define MAX_LOAD 0.00

// Module config structures
typedef struct {
    int global_conns;
    int vhost_conns;
	int user_conns;
	int ip_conns;
    double min_load;
	double max_load;
	int static_throttle;
	int dynamic_throttle;
} balance_config;

#endif // _MOD_BALANCE_H
