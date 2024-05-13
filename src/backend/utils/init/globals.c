/* ========================================================================= **
**                    _  _______  ________________  ________                 **
**                   / |/ / __/ |/_/_  __/ ___/ _ \/ __/ __/                 **
**                  /    / _/_>  <  / / / (_ / , _/ _/_\ \                   **
**                 /_/|_/___/_/|_| /_/  \___/_/|_/___/___/                   **
**                                                                           **
** ========================================================================= **
**                  IN-DATABASE CONNECTION POOLING EXTENSION                 **
** ========================================================================= **
** NEXTGRES Database Compatibility System                                    **
** Portions Copyright (C) NEXTGRES, INC. <info@nextgres.com>                 **
** Portions Copyright (C) PostgresPro                                        **
** Portions Copyright (C) Konstantin Knizhnik                                **
** All Rights Reserved.                                                      **
**                                                                           **
** Permission to use, copy, modify, and/or distribute this software for any  **
** purpose is subject to the terms specified in the License Agreement.       **
** ========================================================================= */

/*
 * OVERVIEW
 *
 * Just as in core Postgres, globals used all over the place should be
 * declared here; not in other modules.
 */

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* -------------------------- Interface Inclusions ------------------------- */

#include "postgres.h"

/* --------------------------- System Inclusions --------------------------- */

/* --------------------------- Project Inclusions -------------------------- */

#include "nextgres/idcp.h"

/* ========================================================================= */
/* -- LOCAL DEFINITIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL MACROS --------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL TYPEDEFS ------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL STRUCTURES ----------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE FUNCTION PROTOTYPES ------------------------------------------ */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION PROTOTYPES -------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

int SessionPoolSize = 10;
int IdlePoolWorkerTimeout = 0;
int ConnectionProxiesNumber = 0;
int SessionSchedule = SESSION_SCHED_ROUND_ROBIN;
int MaxSessions = 20000;
bool RestartPoolerOnReload = false;
bool ProxyingGUCs = false;
bool MultitenantProxy = false;

/* ---------------------------- Enumerated GUCs ---------------------------- */

/* ------------------------------ Boolean GUCs ----------------------------- */

bool g_ng_idcp_multitenant_proxy = false;
bool g_ng_idcp_proxying_gucs = false;
bool g_ng_idcp_restart_pooler_on_reload = false;

/* ------------------------------ Integer GUCs ----------------------------- */

int g_ng_idcp_cfg_port = 0;
int g_ng_idcp_cfg_session_pool_size = 0;
int g_ng_idcp_cfg_session_scheduler = 0;
int g_ng_idcp_cfg_idle_worker_timeout_in_ms = 0;
int g_ng_idcp_cfg_thread_count = 0;
int g_ng_idcp_cfg_max_sessions_per_thread = 0;
int g_ng_idcp_cfg_pool_mode = NG_IDCP_POOL_MODE_SESSION;
int g_ng_idcp_cfg_application_name_add_host = 0;
int g_ng_idcp_cfg_autodb_idle_timeout = 0;
int g_ng_idcp_cfg_cancel_wait_timeout = 0;
int g_ng_idcp_cfg_client_idle_timeout = 0;
int g_ng_idcp_cfg_client_login_timeout = 0;
int g_ng_idcp_cfg_default_pool_size = 0;
int g_ng_idcp_cfg_disable_pqexec = 0;
int g_ng_idcp_cfg_dns_max_ttl = 0;
int g_ng_idcp_cfg_dns_nxdomain_ttl = 0;
int g_ng_idcp_cfg_dns_zone_check_period = 0;
int g_ng_idcp_cfg_idle_transaction_timeout = 0;
int g_ng_idcp_cfg_listen_backlog = 0;
int g_ng_idcp_cfg_listen_port = 0;
int g_ng_idcp_cfg_log_connections = 0;
int g_ng_idcp_cfg_log_disconnections = 0;
int g_ng_idcp_cfg_log_pooler_errors = 0;
int g_ng_idcp_cfg_log_stats = 0;
int g_ng_idcp_cfg_max_client_conn = 0;
int g_ng_idcp_cfg_max_db_connections = 0;
int g_ng_idcp_cfg_max_packet_size = 0;
int g_ng_idcp_cfg_max_prepared_statements = 0;
int g_ng_idcp_cfg_max_user_connections = 0;
int g_ng_idcp_cfg_min_pool_size = 0;
int g_ng_idcp_cfg_peer_id = 0;
int g_ng_idcp_cfg_pkt_buf = 0;
int g_ng_idcp_cfg_query_timeout = 0;
int g_ng_idcp_cfg_query_wait_timeout = 0;
int g_ng_idcp_cfg_reserve_pool_size = 0;
int g_ng_idcp_cfg_reserve_pool_timeout = 0;
int g_ng_idcp_cfg_sbuf_loopcnt = 0;
int g_ng_idcp_cfg_server_check_delay = 0;
int g_ng_idcp_cfg_server_connect_timeout = 0;
int g_ng_idcp_cfg_server_fast_close = 0;
int g_ng_idcp_cfg_server_idle_timeout = 0;
int g_ng_idcp_cfg_server_lifetime = 0;
int g_ng_idcp_cfg_server_login_retry = 0;
int g_ng_idcp_cfg_server_reset_query_always = 0;
int g_ng_idcp_cfg_server_round_robin = 0;
int g_ng_idcp_cfg_so_reuseport = 0;
int g_ng_idcp_cfg_stats_period = 0;
int g_ng_idcp_cfg_suspend_timeout = 0;
int g_ng_idcp_cfg_tcp_defer_accept = 0;
int g_ng_idcp_cfg_tcp_keepcnt = 0;
int g_ng_idcp_cfg_tcp_keepidle = 0;
int g_ng_idcp_cfg_tcp_keepintvl = 0;
int g_ng_idcp_cfg_tcp_socket_buffer = 0;
int g_ng_idcp_cfg_tcp_user_timeout = 0;
int g_ng_idcp_cfg_unix_socket_mode = 0;
int g_ng_idcp_cfg_verbose = 0;

/* ------------------------------ String GUCs ------------------------------ */

char *gp_ng_idcp_cfg_auth_dbname = NULL;
char *gp_ng_idcp_cfg_auth_file = NULL;
char *gp_ng_idcp_cfg_auth_hba_file = NULL;
char *gp_ng_idcp_cfg_auth_ident_file = NULL;
char *gp_ng_idcp_cfg_auth_query = NULL;
char *gp_ng_idcp_cfg_auth_type = NULL;
char *gp_ng_idcp_cfg_client_tls_ca_file = NULL;
char *gp_ng_idcp_cfg_client_tls_cert_file = NULL;
char *gp_ng_idcp_cfg_client_tls_ciphers = NULL;
char *gp_ng_idcp_cfg_client_tls_dheparams = NULL;
char *gp_ng_idcp_cfg_client_tls_ecdhcurve = NULL;
char *gp_ng_idcp_cfg_client_tls_key_file = NULL;
char *gp_ng_idcp_cfg_client_tls_protocols = NULL;
char *gp_ng_idcp_cfg_client_tls_sslmode = NULL;
char *gp_ng_idcp_cfg_ignore_startup_parameters = NULL;
char *gp_ng_idcp_cfg_job_name = NULL;
char *gp_ng_idcp_cfg_listen_addr = NULL;
char *gp_ng_idcp_cfg_logfile = NULL;
char *gp_ng_idcp_cfg_pidfile = NULL;
char *gp_ng_idcp_cfg_resolv_conf = NULL;
char *gp_ng_idcp_cfg_server_check_query = NULL;
char *gp_ng_idcp_cfg_server_reset_query = NULL;
char *gp_ng_idcp_cfg_server_tls_ca_file = NULL;
char *gp_ng_idcp_cfg_server_tls_cert_file = NULL;
char *gp_ng_idcp_cfg_server_tls_ciphers = NULL;
char *gp_ng_idcp_cfg_server_tls_key_file = NULL;
char *gp_ng_idcp_cfg_server_tls_protocols = NULL;
char *gp_ng_idcp_cfg_server_tls_sslmode = NULL;
char *gp_ng_idcp_cfg_service_name = NULL;
char *gp_ng_idcp_cfg_tcp_keepalive = NULL;
char *gp_ng_idcp_cfg_track_extra_parameters = NULL;
char *gp_ng_idcp_cfg_unix_socket_dir = NULL;
char *gp_ng_idcp_cfg_unix_socket_group = NULL;

/* ========================================================================= */
/* -- PRIVATE VARIABLES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL VARIABLES ------------------------------------------------------ */
/* ========================================================================= */

/* ========================================================================= */
/* -- STATIC ASSERTIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC FUNCTION DEFINITIONS ------------------------------------------ */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE FUNCTION DEFINITIONS ----------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION DEFINITIONS ------------------------------------------- */
/* ========================================================================= */

/* vim: set ts=2 et sw=2 ft=c: */

