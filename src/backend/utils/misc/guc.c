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
 * ...
 *
 */

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* -------------------------- Interface Inclusions ------------------------- */

#include "postgres.h"
#include "utils/guc.h"

/* --------------------------- System Inclusions --------------------------- */

#include <limits.h>

/* --------------------------- Project Inclusions -------------------------- */

#include "nextgres/idcp.h"
#include "nextgres/idcp/util/guc.h"

/* ========================================================================= */
/* -- LOCAL DEFINITIONS ---------------------------------------------------- */
/* ========================================================================= */

#define DEFAULT_IDCP_APPLICATION_NAME_ADD_HOST  0
#define DEFAULT_IDCP_AUTH_DBNAME                NULL
#define DEFAULT_IDCP_AUTH_FILE                  NULL
#define DEFAULT_IDCP_AUTH_HBA_FILE              "pg_hba.conf"
#define DEFAULT_IDCP_AUTH_IDENT_FILE            NULL
#define DEFAULT_IDCP_AUTH_QUERY                 "SELECT usename, passwd FROM pg_shadow WHERE usename=$1"
#define DEFAULT_IDCP_AUTH_TYPE                  "md5"
#define DEFAULT_IDCP_AUTODB_IDLE_TIMEOUT        3600
#define DEFAULT_IDCP_CANCEL_WAIT_TIMEOUT        10
#define DEFAULT_IDCP_CLIENT_IDLE_TIMEOUT        0
#define DEFAULT_IDCP_CLIENT_LOGIN_TIMEOUT       60
#define DEFAULT_IDCP_CLIENT_TLS_CA_FILE         NULL
#define DEFAULT_IDCP_CLIENT_TLS_CERT_FILE       NULL
#define DEFAULT_IDCP_CLIENT_TLS_CIPHERS         "default"
#define DEFAULT_IDCP_CLIENT_TLS_DHEPARAMS       "auto"
#define DEFAULT_IDCP_CLIENT_TLS_ECDHCURVE       "auto"
#define DEFAULT_IDCP_CLIENT_TLS_KEY_FILE        NULL
#define DEFAULT_IDCP_CLIENT_TLS_PROTOCOLS       "secure"
#define DEFAULT_IDCP_CLIENT_TLS_SSLMODE         "disable"
#define DEFAULT_IDCP_DEFAULT_POOL_SIZE          20
#define DEFAULT_IDCP_DISABLE_PQEXEC             0
#define DEFAULT_IDCP_DNS_MAX_TTL                15
#define DEFAULT_IDCP_DNS_NXDOMAIN_TTL           15
#define DEFAULT_IDCP_DNS_ZONE_CHECK_PERIOD      0
#define DEFAULT_IDCP_IDLE_TRANSACTION_TIMEOUT   0
#define DEFAULT_IDCP_IDLE_WORKER_TIMEOUT_IN_MS  0
#define DEFAULT_IDCP_IGNORE_STARTUP_PARAMETERS  NULL
#define DEFAULT_IDCP_JOB_NAME                   "pgbouncer"
#define DEFAULT_IDCP_LISTEN_ADDR                NULL
#define DEFAULT_IDCP_LISTEN_BACKLOG             128
#define DEFAULT_IDCP_LISTEN_PORT                6432
#define DEFAULT_IDCP_LOGFILE                    NULL
#define DEFAULT_IDCP_LOG_CONNECTIONS            1
#define DEFAULT_IDCP_LOG_DISCONNECTIONS         1
#define DEFAULT_IDCP_LOG_POOLER_ERRORS          1
#define DEFAULT_IDCP_LOG_STATS                  1
#define DEFAULT_IDCP_MAX_CLIENT_CONN            100
#define DEFAULT_IDCP_MAX_DB_CONNECTIONS         0
#define DEFAULT_IDCP_MAX_PACKET_SIZE            2147483647
#define DEFAULT_IDCP_MAX_PREPARED_STATEMENTS    0
#define DEFAULT_IDCP_MAX_SESSIONS_PER_THREAD    1000
#define DEFAULT_IDCP_MAX_USER_CONNECTIONS       0
#define DEFAULT_IDCP_MIN_POOL_SIZE              0
#define DEFAULT_IDCP_MULTITENANT_PROXY          false
#define DEFAULT_IDCP_PEER_ID                    0
#define DEFAULT_IDCP_PIDFILE                    NULL
#define DEFAULT_IDCP_PKT_BUF                    4096
#define DEFAULT_IDCP_POOL_MODE                  NG_IDCP_POOL_MODE_SESSION
#define DEFAULT_IDCP_PORT                       6543
#define DEFAULT_IDCP_PROXYING_GUCS              false
#define DEFAULT_IDCP_QUERY_TIMEOUT              0
#define DEFAULT_IDCP_QUERY_WAIT_TIMEOUT         120
#define DEFAULT_IDCP_RESERVE_POOL_SIZE          0
#define DEFAULT_IDCP_RESERVE_POOL_TIMEOUT       5
#define DEFAULT_IDCP_RESOLV_CONF                NULL
#define DEFAULT_IDCP_RESTART_POOLER_ON_RELOAD   false
#define DEFAULT_IDCP_SBUF_LOOPCNT               5
#define DEFAULT_IDCP_SERVER_CHECK_DELAY         30
#define DEFAULT_IDCP_SERVER_CHECK_QUERY         "SELECT 1"
#define DEFAULT_IDCP_SERVER_CONNECT_TIMEOUT     15
#define DEFAULT_IDCP_SERVER_FAST_CLOSE          0
#define DEFAULT_IDCP_SERVER_IDLE_TIMEOUT        600
#define DEFAULT_IDCP_SERVER_LIFETIME            3600
#define DEFAULT_IDCP_SERVER_LOGIN_RETRY         15
#define DEFAULT_IDCP_SERVER_RESET_QUERY         "DISCARD ALL"
#define DEFAULT_IDCP_SERVER_RESET_QUERY_ALWAYS  0
#define DEFAULT_IDCP_SERVER_ROUND_ROBIN         0
#define DEFAULT_IDCP_SERVER_TLS_CA_FILE         NULL
#define DEFAULT_IDCP_SERVER_TLS_CERT_FILE       NULL
#define DEFAULT_IDCP_SERVER_TLS_CIPHERS         "default"
#define DEFAULT_IDCP_SERVER_TLS_KEY_FILE        NULL
#define DEFAULT_IDCP_SERVER_TLS_PROTOCOLS       "secure"
#define DEFAULT_IDCP_SERVER_TLS_SSLMODE         "prefer"
#define DEFAULT_IDCP_SERVICE_NAME               NULL
#define DEFAULT_IDCP_SESSION_POOL_SIZE          10
#define DEFAULT_IDCP_SESSION_SCHEDULER          NG_IDCP_SESSION_SCHED_ROUND_ROBIN
#define DEFAULT_IDCP_SO_REUSEPORT               0
#define DEFAULT_IDCP_STATS_PERIOD               60
#define DEFAULT_IDCP_SUSPEND_TIMEOUT            10
#define DEFAULT_IDCP_TCP_DEFER_ACCEPT           0
#define DEFAULT_IDCP_TCP_KEEPALIVE              NULL
#define DEFAULT_IDCP_TCP_KEEPCNT                0
#define DEFAULT_IDCP_TCP_KEEPIDLE               0
#define DEFAULT_IDCP_TCP_KEEPINTVL              0
#define DEFAULT_IDCP_TCP_SOCKET_BUFFER          0
#define DEFAULT_IDCP_TCP_USER_TIMEOUT           0
#define DEFAULT_IDCP_THREAD_COUNT               0
#define DEFAULT_IDCP_TRACK_EXTRA_PARAMETERS     "IntervalStyle"
#define DEFAULT_IDCP_UNIX_SOCKET_DIR            "/tmp"
#define DEFAULT_IDCP_UNIX_SOCKET_GROUP          NULL
#define DEFAULT_IDCP_UNIX_SOCKET_MODE           0777
#define DEFAULT_IDCP_VERBOSE                    0

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

/* ========================================================================= */
/* -- PRIVATE VARIABLES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL VARIABLES ------------------------------------------------------ */
/* ========================================================================= */

/* ---------------------------- Enumerated GUCs ---------------------------- */

static const struct config_enum_entry ng_idcp_session_schedulers[] = {
  { "round-robin",    NG_IDCP_SESSION_SCHED_ROUND_ROBIN,    false },
  { "random",         NG_IDCP_SESSION_SCHED_RANDOM,         false },
  { "load-balancing", NG_IDCP_SESSION_SCHED_LOAD_BALANCING, false },
  { NULL,             0,                                    false }
};

static const struct config_enum_entry ng_idcp_pool_modes[] = {
  { "session",        NG_IDCP_POOL_MODE_SESSION,            false },
  { "transaction",    NG_IDCP_POOL_MODE_TRANSACTION,        false },
  { "statement",      NG_IDCP_POOL_MODE_STATEMENT,          false },
  { NULL,             0,                                    false }
};

/* ------------------------------ Boolean GUCs ----------------------------- */

static const struct ng_idcp_bool_guc_s {
  const char             *name;
  const char             *short_desc;
  const char             *long_desc;
  bool                   *valueAddr;
  bool                    bootValue;
  GucContext              context;
  int                     flags;
  GucBoolCheckHook        check_hook;
  GucBoolAssignHook       assign_hook;
  GucShowHook             show_hook;
} ng_idcp_bool_gucs[] = {
  {
    .name = "nextgres_idcp.restart_pooler_on_reload",
    .short_desc = gettext_noop("Restart session pool workers on pg_reload_conf()"),
    .long_desc = gettext_noop("Restart session pool workers on pg_reload_conf()"),
    .valueAddr = &g_ng_idcp_restart_pooler_on_reload,
    .bootValue = DEFAULT_IDCP_RESTART_POOLER_ON_RELOAD,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
  },
  {
    .name = "nextgres_idcp.proxying_gucs",
    .short_desc = gettext_noop("Support setting parameters in connection pooler sessions."),
    .long_desc = gettext_noop("Support setting parameters in connection pooler sessions."),
    .valueAddr = &g_ng_idcp_proxying_gucs,
    .bootValue = DEFAULT_IDCP_PROXYING_GUCS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
  },
  {
    .name = "nextgres_idcp.multitenant_proxy",
    .short_desc = gettext_noop("One pool worker can serve clients with different roles"),
    .long_desc = gettext_noop("One pool worker can serve clients with different roles"),
    .valueAddr = &g_ng_idcp_multitenant_proxy,
    .bootValue = DEFAULT_IDCP_MULTITENANT_PROXY,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
  },
}; /* ng_idcp_bool_gucs */

/* ------------------------------ Integer GUCs ----------------------------- */

static const struct ng_idcp_int_guc_s {
  const char             *name;
  const char             *short_desc;
  const char             *long_desc;
  int                    *valueAddr;
  int                     bootValue;
  int                     minValue;
  int                     maxValue;
  GucContext              context;
  int                     flags;
  GucIntCheckHook         check_hook;
  GucIntAssignHook        assign_hook;
  GucShowHook             show_hook;
} ng_idcp_int_gucs[] = {
  {
    .name = "nextgres_idcp.application_name_add_host",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_application_name_add_host,
    .bootValue = DEFAULT_IDCP_APPLICATION_NAME_ADD_HOST,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.autodb_idle_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_autodb_idle_timeout,
    .bootValue = DEFAULT_IDCP_AUTODB_IDLE_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.cancel_wait_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_cancel_wait_timeout,
    .bootValue = DEFAULT_IDCP_CANCEL_WAIT_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.client_idle_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_client_idle_timeout,
    .bootValue = DEFAULT_IDCP_CLIENT_IDLE_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.client_login_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_client_login_timeout,
    .bootValue = DEFAULT_IDCP_CLIENT_LOGIN_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.default_pool_size",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_default_pool_size,
    .bootValue = DEFAULT_IDCP_DEFAULT_POOL_SIZE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.disable_pqexec",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_disable_pqexec,
    .bootValue = DEFAULT_IDCP_DISABLE_PQEXEC,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.dns_max_ttl",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_dns_max_ttl,
    .bootValue = DEFAULT_IDCP_DNS_MAX_TTL,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.dns_nxdomain_ttl",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_dns_nxdomain_ttl,
    .bootValue = DEFAULT_IDCP_DNS_NXDOMAIN_TTL,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.dns_zone_check_period",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_dns_zone_check_period,
    .bootValue = DEFAULT_IDCP_DNS_ZONE_CHECK_PERIOD,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.idle_transaction_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_idle_transaction_timeout,
    .bootValue = DEFAULT_IDCP_IDLE_TRANSACTION_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.listen_backlog",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_listen_backlog,
    .bootValue = DEFAULT_IDCP_LISTEN_BACKLOG,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.listen_port",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_listen_port,
    .bootValue = DEFAULT_IDCP_LISTEN_PORT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.log_connections",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_log_connections,
    .bootValue = DEFAULT_IDCP_LOG_CONNECTIONS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.log_disconnections",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_log_disconnections,
    .bootValue = DEFAULT_IDCP_LOG_DISCONNECTIONS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.log_pooler_errors",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_log_pooler_errors,
    .bootValue = DEFAULT_IDCP_LOG_POOLER_ERRORS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.log_stats",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_log_stats,
    .bootValue = DEFAULT_IDCP_LOG_STATS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_client_conn",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_max_client_conn,
    .bootValue = DEFAULT_IDCP_MAX_CLIENT_CONN,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_db_connections",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_max_db_connections,
    .bootValue = DEFAULT_IDCP_MAX_DB_CONNECTIONS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_packet_size",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_max_packet_size,
    .bootValue = DEFAULT_IDCP_MAX_PACKET_SIZE,
    .minValue = 0,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_prepared_statements",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_max_prepared_statements,
    .bootValue = DEFAULT_IDCP_MAX_PREPARED_STATEMENTS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_user_connections",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_max_user_connections,
    .bootValue = DEFAULT_IDCP_MAX_USER_CONNECTIONS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.min_pool_size",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_min_pool_size,
    .bootValue = DEFAULT_IDCP_MIN_POOL_SIZE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.peer_id",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_peer_id,
    .bootValue = DEFAULT_IDCP_PEER_ID,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.pkt_buf",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_pkt_buf,
    .bootValue = DEFAULT_IDCP_PKT_BUF,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.query_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_query_timeout,
    .bootValue = DEFAULT_IDCP_QUERY_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.query_wait_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_query_wait_timeout,
    .bootValue = DEFAULT_IDCP_QUERY_WAIT_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.reserve_pool_size",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_reserve_pool_size,
    .bootValue = DEFAULT_IDCP_RESERVE_POOL_SIZE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.reserve_pool_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_reserve_pool_timeout,
    .bootValue = DEFAULT_IDCP_RESERVE_POOL_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.sbuf_loopcnt",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_sbuf_loopcnt,
    .bootValue = DEFAULT_IDCP_SBUF_LOOPCNT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_check_delay",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_check_delay,
    .bootValue = DEFAULT_IDCP_SERVER_CHECK_DELAY,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_connect_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_connect_timeout,
    .bootValue = DEFAULT_IDCP_SERVER_CONNECT_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_fast_close",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_fast_close,
    .bootValue = DEFAULT_IDCP_SERVER_FAST_CLOSE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_idle_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_idle_timeout,
    .bootValue = DEFAULT_IDCP_SERVER_IDLE_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_lifetime",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_lifetime,
    .bootValue = DEFAULT_IDCP_SERVER_LIFETIME,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_login_retry",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_login_retry,
    .bootValue = DEFAULT_IDCP_SERVER_LOGIN_RETRY,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_reset_query_always",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_reset_query_always,
    .bootValue = DEFAULT_IDCP_SERVER_RESET_QUERY_ALWAYS,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.server_round_robin",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_server_round_robin,
    .bootValue = DEFAULT_IDCP_SERVER_ROUND_ROBIN,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.so_reuseport",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_so_reuseport,
    .bootValue = DEFAULT_IDCP_SO_REUSEPORT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.stats_period",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_stats_period,
    .bootValue = DEFAULT_IDCP_STATS_PERIOD,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.suspend_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_suspend_timeout,
    .bootValue = DEFAULT_IDCP_SUSPEND_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_defer_accept",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_defer_accept,
    .bootValue = DEFAULT_IDCP_TCP_DEFER_ACCEPT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_keepcnt",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_keepcnt,
    .bootValue = DEFAULT_IDCP_TCP_KEEPCNT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_keepidle",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_keepidle,
    .bootValue = DEFAULT_IDCP_TCP_KEEPIDLE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_keepintvl",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_keepintvl,
    .bootValue = DEFAULT_IDCP_TCP_KEEPINTVL,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_socket_buffer",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_socket_buffer,
    .bootValue = DEFAULT_IDCP_TCP_SOCKET_BUFFER,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.tcp_user_timeout",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_tcp_user_timeout,
    .bootValue = DEFAULT_IDCP_TCP_USER_TIMEOUT,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.unix_socket_mode",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_unix_socket_mode,
    .bootValue = DEFAULT_IDCP_UNIX_SOCKET_MODE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.verbose",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &g_ng_idcp_cfg_verbose,
    .bootValue = DEFAULT_IDCP_VERBOSE,
    .minValue = 0,
    .maxValue = 65535,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },

  {
    .name = "nextgres_idcp.session_pool_size",
    .short_desc = gettext_noop("Sets number of backends serving client sessions."),
    .long_desc = gettext_noop("If non-zero then session pooling will be used: "
      "client connections will be redirected to one of the backends and "
      "maximal number of backends is determined by this parameter."
      "Launched backend are never terminated even in case of no active sessions."),
    .valueAddr = &g_ng_idcp_cfg_session_pool_size,
    .bootValue = DEFAULT_IDCP_SESSION_POOL_SIZE,
    .minValue = 0,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.idle_pool_worker_timeout",
    .short_desc = gettext_noop("Sets the maximum allowed duration of any idling connection pool worker."),
    .long_desc = gettext_noop("A value of 0 turns off the timeout."),
    .valueAddr = &g_ng_idcp_cfg_idle_worker_timeout_in_ms,
    .bootValue = DEFAULT_IDCP_IDLE_WORKER_TIMEOUT_IN_MS,
    .minValue = 0,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.thread_count",
    .short_desc = gettext_noop("Sets number of connection proxies."),
    .long_desc = gettext_noop("Postmaster spawns separate worker process for each proxy. Postmaster scatters connections between proxies using one of scheduling policies (round-robin, random, load-balancing)."
              "Each proxy launches its own subset of backends. So maximal number of non-tainted backends is "
              "session_pool_size*connection_proxies*databases*roles."),
    .valueAddr = &g_ng_idcp_cfg_thread_count,
    .bootValue = DEFAULT_IDCP_THREAD_COUNT,
    .minValue = 0,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.max_sessions_per_thread",
    .short_desc = gettext_noop("Sets the maximum number of client session."),
    .long_desc = gettext_noop("Maximal number of client sessions which can be handled by one connection proxy."
      "It can be greater than max_connections and actually be arbitrary large."),
    .valueAddr = &g_ng_idcp_cfg_max_sessions_per_thread,
    .bootValue = DEFAULT_IDCP_MAX_SESSIONS_PER_THREAD,
    .minValue = 1,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  },
  {
    .name = "nextgres_idcp.port",
    .short_desc = gettext_noop("Sets the TCP port for the connection pooler."),
    .long_desc = gettext_noop("Sets the TCP port for the connection pooler."),
    .valueAddr = &g_ng_idcp_cfg_port,
    .bootValue = DEFAULT_IDCP_PORT,
    .minValue = 1,
    .maxValue = INT_MAX,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL
  }
}; /* ng_idcp_int_gucs */

/* ------------------------------ String GUCs ------------------------------ */

static const struct ng_idcp_string_guc_s {
  const char             *name;
  const char             *short_desc;
  const char             *long_desc;
  char                  **valueAddr;
  const char             *bootValue;
  GucContext              context;
  int                     flags;
  GucStringCheckHook      check_hook;
  GucStringAssignHook     assign_hook;
  GucShowHook             show_hook;
  const char             *original_name;
} ng_idcp_string_gucs[] = {
  {
    .name = "nextgres_idcp.auth_dbname",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_dbname,
    .bootValue = DEFAULT_IDCP_AUTH_DBNAME,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_dbname"
  },
  {
    .name = "nextgres_idcp.auth_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_file,
    .bootValue = DEFAULT_IDCP_AUTH_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_file"
  },
  {
    .name = "nextgres_idcp.auth_hba_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_hba_file,
    .bootValue = DEFAULT_IDCP_AUTH_HBA_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_hba_file"
  },
  {
    .name = "nextgres_idcp.auth_ident_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_ident_file,
    .bootValue = DEFAULT_IDCP_AUTH_IDENT_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_ident_file"
  },
  {
    .name = "nextgres_idcp.auth_query",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_query,
    .bootValue = DEFAULT_IDCP_AUTH_QUERY,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_query"
  },
  {
    .name = "nextgres_idcp.auth_type",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_auth_type,
    .bootValue = DEFAULT_IDCP_AUTH_TYPE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "auth_type"
  },
  {
    .name = "nextgres_idcp.client_tls_ca_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_ca_file,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_CA_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_ca_file"
  },
  {
    .name = "nextgres_idcp.client_tls_cert_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_cert_file,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_CERT_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_cert_file"
  },
  {
    .name = "nextgres_idcp.client_tls_ciphers",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_ciphers,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_CIPHERS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_ciphers"
  },
  {
    .name = "nextgres_idcp.client_tls_dheparams",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_dheparams,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_DHEPARAMS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_dheparams"
  },
  {
    .name = "nextgres_idcp.client_tls_ecdhcurve",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_ecdhcurve,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_ECDHCURVE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_ecdhcurve"
  },
  {
    .name = "nextgres_idcp.client_tls_key_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_key_file,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_KEY_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_key_file"
  },
  {
    .name = "nextgres_idcp.client_tls_protocols",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_protocols,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_PROTOCOLS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_protocols"
  },
  {
    .name = "nextgres_idcp.client_tls_sslmode",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_client_tls_sslmode,
    .bootValue = DEFAULT_IDCP_CLIENT_TLS_SSLMODE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "client_tls_sslmode"
  },
  {
    .name = "nextgres_idcp.ignore_startup_parameters",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_ignore_startup_parameters,
    .bootValue = DEFAULT_IDCP_IGNORE_STARTUP_PARAMETERS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "ignore_startup_parameters"
  },
  {
    .name = "nextgres_idcp.job_name",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_job_name,
    .bootValue = DEFAULT_IDCP_JOB_NAME,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "job_name"
  },
  {
    .name = "nextgres_idcp.listen_addr",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_listen_addr,
    .bootValue = DEFAULT_IDCP_LISTEN_ADDR,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "listen_addr"
  },
  {
    .name = "nextgres_idcp.logfile",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_logfile,
    .bootValue = DEFAULT_IDCP_LOGFILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "logfile"
  },
  {
    .name = "nextgres_idcp.pidfile",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_pidfile,
    .bootValue = DEFAULT_IDCP_PIDFILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "pidfile"
  },
  {
    .name = "nextgres_idcp.resolv_conf",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_resolv_conf,
    .bootValue = DEFAULT_IDCP_RESOLV_CONF,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "resolv_conf"
  },
  {
    .name = "nextgres_idcp.server_check_query",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_check_query,
    .bootValue = DEFAULT_IDCP_SERVER_CHECK_QUERY,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_check_query"
  },
  {
    .name = "nextgres_idcp.server_reset_query",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_reset_query,
    .bootValue = DEFAULT_IDCP_SERVER_RESET_QUERY,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_reset_query"
  },
  {
    .name = "nextgres_idcp.server_tls_ca_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_ca_file,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_CA_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_ca_file"
  },
  {
    .name = "nextgres_idcp.server_tls_cert_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_cert_file,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_CERT_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_cert_file"
  },
  {
    .name = "nextgres_idcp.server_tls_ciphers",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_ciphers,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_CIPHERS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_ciphers"
  },
  {
    .name = "nextgres_idcp.server_tls_key_file",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_key_file,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_KEY_FILE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_key_file"
  },
  {
    .name = "nextgres_idcp.server_tls_protocols",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_protocols,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_PROTOCOLS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_protocols"
  },
  {
    .name = "nextgres_idcp.server_tls_sslmode",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_server_tls_sslmode,
    .bootValue = DEFAULT_IDCP_SERVER_TLS_SSLMODE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "server_tls_sslmode"
  },
  {
    .name = "nextgres_idcp.service_name",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_service_name,
    .bootValue = DEFAULT_IDCP_SERVICE_NAME,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "service_name"
  },
  {
    .name = "nextgres_idcp.tcp_keepalive",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_tcp_keepalive,
    .bootValue = DEFAULT_IDCP_TCP_KEEPALIVE,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "tcp_keepalive"
  },
  {
    .name = "nextgres_idcp.track_extra_parameters",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_track_extra_parameters,
    .bootValue = DEFAULT_IDCP_TRACK_EXTRA_PARAMETERS,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "track_extra_parameters"
  },
  {
    .name = "nextgres_idcp.unix_socket_dir",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_unix_socket_dir,
    .bootValue = DEFAULT_IDCP_UNIX_SOCKET_DIR,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "unix_socket_dir"
  },
  {
    .name = "nextgres_idcp.unix_socket_group",
    .short_desc = gettext_noop(""),
    .long_desc = gettext_noop(""),
    .valueAddr = &gp_ng_idcp_cfg_unix_socket_group,
    .bootValue = DEFAULT_IDCP_UNIX_SOCKET_GROUP,
    .context = PGC_POSTMASTER,
    .flags = 0,
    .check_hook = NULL,
    .assign_hook = NULL,
    .show_hook = NULL,
    .original_name = "unix_socket_group"
  },
}; /* ng_idcp_string_guc_s */

/* ========================================================================= */
/* -- STATIC ASSERTIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC FUNCTION DEFINITIONS ------------------------------------------ */
/* ========================================================================= */

void
ng_idcp_guc_define (
  void
) {

  DefineCustomEnumVariable("nextgres_idcp.pool_mode",
    gettext_noop("Specifies when a server connection can be reused by other clients."),
    gettext_noop("Specifies when a server connection can be reused by other clients."),
    &g_ng_idcp_cfg_pool_mode,
    DEFAULT_IDCP_POOL_MODE,
    ng_idcp_pool_modes,
    PGC_POSTMASTER,
    0,
    NULL,
    NULL,
    NULL);

  DefineCustomEnumVariable("nextgres_idcp.session_schedule",
    gettext_noop("Session schedule policy for connection pool."),
    gettext_noop("Session schedule policy for connection pool."),
    &g_ng_idcp_cfg_session_scheduler,
    DEFAULT_IDCP_SESSION_SCHEDULER,
    ng_idcp_session_schedulers,
    PGC_POSTMASTER,
    0,
    NULL,
    NULL,
    NULL);

  /* Iterate & define the boolean GUCs */
  for (int ii = 0; ii < lengthof(ng_idcp_bool_gucs); ++ii) {
    const struct ng_idcp_bool_guc_s *guc = &ng_idcp_bool_gucs[ii];

    elog(LOG, "Setting bool GUC %s", guc->name);
    DefineCustomBoolVariable(guc->name, guc->short_desc, guc->long_desc,
      guc->valueAddr, guc->bootValue, guc->context, guc->flags,
      guc->check_hook, guc->assign_hook, guc->show_hook);
  }

  /* Iterate & define the integer GUCs */
  for (int ii = 0; ii < lengthof(ng_idcp_int_gucs); ++ii) {
    const struct ng_idcp_int_guc_s *guc = &ng_idcp_int_gucs[ii];

    elog(LOG, "Setting int GUC %s", guc->name);
    DefineCustomIntVariable(guc->name, guc->short_desc, guc->long_desc,
      guc->valueAddr, guc->bootValue, guc->minValue, guc->maxValue,
      guc->context, guc->flags, guc->check_hook, guc->assign_hook,
      guc->show_hook);
  }

  /* Iterate & define the string GUCs */
  for (int ii = 0; ii < lengthof(ng_idcp_string_gucs); ++ii) {
    const struct ng_idcp_string_guc_s *guc =
      &ng_idcp_string_gucs[ii];

    elog(LOG, "Setting string GUC %s", guc->name);
    DefineCustomStringVariable(guc->name, guc->short_desc, guc->long_desc,
      guc->valueAddr, guc->bootValue, guc->context, guc->flags,
      guc->check_hook, guc->assign_hook, guc->show_hook);
  }

  /* You kids get off my lawn! */
  MarkGUCPrefixReserved("nextgres_idcp");

} /* ng_idcp_guc_define() */

/* ========================================================================= */
/* -- PRIVATE FUNCTION DEFINITIONS ----------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION DEFINITIONS ------------------------------------------- */
/* ========================================================================= */

/* vim: set ts=2 et sw=2 ft=c: */

