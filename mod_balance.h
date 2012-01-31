#ifndef _MOD_BALANCE_H
#include "httpd.h"
#include "http_config.h"
#include "http_request.h"
#include "http_protocol.h"
#include "http_core.h"
#include "http_main.h"
#include "http_log.h"
#include "http_conf_globals.h"
#include "scoreboard.h"

#include <stdio.h>		// printf()
#include <stdlib.h>		// required by atoi(), atof(), getloadavg()
#include <unistd.h>		// usleep

#define BALANCE_DEBUG
#define BALANCE_VERSION "mod_balance/0.01"
#define MAX_USER_SIZE 12
#define DYNAMIC_SLEEP 5
#define STATIC_SLEEP 2
#define USER_CONNS -1
#define VHOST_CONNS -1
#define GLOBAL_CONNS -1
#define MIN_LOAD 5.00

// Module config structures
typedef struct {
    int global_conns;
    int vhost_conns;
	int user_conns;
    double load;
	int static_throttle;
	int dynamic_throttle;
} balance_config;

#endif // _MOD_BALANCE_H
