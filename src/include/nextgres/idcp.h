#ifndef NG_IDCP_H                                /* Multiple Inclusion Guard */
#define NG_IDCP_H
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

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC DEFINITIONS --------------------------------------------------- */
/* ========================================================================= */

#define NEXTGRES_EXTNAME    "nextgres_idcp"
#define NEXTGRES_LIBNAME    NEXTGRES_EXTNAME

/* ========================================================================= */
/* -- PUBLIC MACROS -------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC TYPEDEFS ------------------------------------------------------ */
/* ========================================================================= */

typedef enum ng_idcp_pool_mode_t {
  NG_IDCP_POOL_MODE_SESSION = 0,                         /** Session Pooling */
  NG_IDCP_POOL_MODE_TRANSACTION,                     /** Transaction Pooling */
  NG_IDCP_POOL_MODE_STATEMENT                          /** Statement Pooling */
} ng_idcp_pool_mode_t;

typedef enum ng_idcp_session_scheduler_t {
  NG_IDCP_SESSION_SCHED_ROUND_ROBIN = 0, /** Round Robin Assignment Strategy */
  NG_IDCP_SESSION_SCHED_RANDOM,               /** Random Assignment Strategy */
  NG_IDCP_SESSION_SCHED_LOAD_BALANCING    /** Least-Used Assignment Strategy */
} ng_idcp_session_scheduler_t;

enum SessionSchedulePolicy
{
  SESSION_SCHED_ROUND_ROBIN,
  SESSION_SCHED_RANDOM,
  SESSION_SCHED_LOAD_BALANCING
};

/*
 * Information in share dmemory about connection proxy state (used for session scheduling and monitoring)
 */
typedef struct ConnectionProxyState
{
  int pid;                  /* proxy worker pid */
  int n_clients;            /* total number of clients */
  int n_ssl_clients;        /* number of clients using SSL connection */
  int n_pools;              /* nubmer of dbname/role combinations */
  int n_backends;           /* totatal number of launched backends */
  int n_dedicated_backends; /* number of tainted backends */
  int n_idle_backends;      /* number of idle backends */
  int n_idle_clients;       /* number of idle clients */
  uint64 tx_bytes;          /* amount of data sent to client */
  uint64 rx_bytes;          /* amount of data send to server */
  uint64 n_transactions;    /* total number of proroceeded transactions */
} ConnectionProxyState;

/* ========================================================================= */
/* -- PUBLIC STRUCTURES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

extern PGDLLIMPORT int MaxSessions;
extern PGDLLIMPORT int SessionPoolSize;
extern PGDLLIMPORT int IdlePoolWorkerTimeout;
extern PGDLLIMPORT int ConnectionProxiesNumber;
extern PGDLLIMPORT int SessionSchedule;
extern PGDLLIMPORT bool RestartPoolerOnReload;
extern PGDLLIMPORT bool ProxyingGUCs;
extern PGDLLIMPORT bool MultitenantProxy;

extern ConnectionProxyState* ProxyState;
extern PGDLLIMPORT int MyProxyId;
extern PGDLLIMPORT pgsocket MyProxySocket;

/* ---------------------------- Enumerated GUCs ---------------------------- */

/* ------------------------------ Boolean GUCs ----------------------------- */

extern bool g_ng_idcp_multitenant_proxy;
extern bool g_ng_idcp_proxying_gucs;
extern bool g_ng_idcp_restart_pooler_on_reload;

/* ------------------------------ Integer GUCs ----------------------------- */

extern int g_ng_idcp_cfg_port;
extern int g_ng_idcp_cfg_session_pool_size;
extern int g_ng_idcp_cfg_session_scheduler;
extern int g_ng_idcp_cfg_idle_worker_timeout_in_ms;
extern int g_ng_idcp_cfg_thread_count;
extern int g_ng_idcp_cfg_max_sessions_per_thread;
extern int g_ng_idcp_cfg_pool_mode;
extern int g_ng_idcp_cfg_application_name_add_host;
extern int g_ng_idcp_cfg_autodb_idle_timeout;
extern int g_ng_idcp_cfg_cancel_wait_timeout;
extern int g_ng_idcp_cfg_client_idle_timeout;
extern int g_ng_idcp_cfg_client_login_timeout;
extern int g_ng_idcp_cfg_default_pool_size;
extern int g_ng_idcp_cfg_disable_pqexec;
extern int g_ng_idcp_cfg_dns_max_ttl;
extern int g_ng_idcp_cfg_dns_nxdomain_ttl;
extern int g_ng_idcp_cfg_dns_zone_check_period;
extern int g_ng_idcp_cfg_idle_transaction_timeout;
extern int g_ng_idcp_cfg_listen_backlog;
extern int g_ng_idcp_cfg_listen_port;
extern int g_ng_idcp_cfg_log_connections;
extern int g_ng_idcp_cfg_log_disconnections;
extern int g_ng_idcp_cfg_log_pooler_errors;
extern int g_ng_idcp_cfg_log_stats;
extern int g_ng_idcp_cfg_max_client_conn;
extern int g_ng_idcp_cfg_max_db_connections;
extern int g_ng_idcp_cfg_max_packet_size;
extern int g_ng_idcp_cfg_max_prepared_statements;
extern int g_ng_idcp_cfg_max_user_connections;
extern int g_ng_idcp_cfg_min_pool_size;
extern int g_ng_idcp_cfg_peer_id;
extern int g_ng_idcp_cfg_pkt_buf;
extern int g_ng_idcp_cfg_query_timeout;
extern int g_ng_idcp_cfg_query_wait_timeout;
extern int g_ng_idcp_cfg_reserve_pool_size;
extern int g_ng_idcp_cfg_reserve_pool_timeout;
extern int g_ng_idcp_cfg_sbuf_loopcnt;
extern int g_ng_idcp_cfg_server_check_delay;
extern int g_ng_idcp_cfg_server_connect_timeout;
extern int g_ng_idcp_cfg_server_fast_close;
extern int g_ng_idcp_cfg_server_idle_timeout;
extern int g_ng_idcp_cfg_server_lifetime;
extern int g_ng_idcp_cfg_server_login_retry;
extern int g_ng_idcp_cfg_server_reset_query_always;
extern int g_ng_idcp_cfg_server_round_robin;
extern int g_ng_idcp_cfg_so_reuseport;
extern int g_ng_idcp_cfg_stats_period;
extern int g_ng_idcp_cfg_suspend_timeout;
extern int g_ng_idcp_cfg_tcp_defer_accept;
extern int g_ng_idcp_cfg_tcp_keepcnt;
extern int g_ng_idcp_cfg_tcp_keepidle;
extern int g_ng_idcp_cfg_tcp_keepintvl;
extern int g_ng_idcp_cfg_tcp_socket_buffer;
extern int g_ng_idcp_cfg_tcp_user_timeout;
extern int g_ng_idcp_cfg_unix_socket_mode;
extern int g_ng_idcp_cfg_verbose;

/* ------------------------------ String GUCs ------------------------------ */

extern char *gp_ng_idcp_cfg_auth_dbname;
extern char *gp_ng_idcp_cfg_auth_file;
extern char *gp_ng_idcp_cfg_auth_hba_file;
extern char *gp_ng_idcp_cfg_auth_ident_file;
extern char *gp_ng_idcp_cfg_auth_query;
extern char *gp_ng_idcp_cfg_auth_type;
extern char *gp_ng_idcp_cfg_client_tls_ca_file;
extern char *gp_ng_idcp_cfg_client_tls_cert_file;
extern char *gp_ng_idcp_cfg_client_tls_ciphers;
extern char *gp_ng_idcp_cfg_client_tls_dheparams;
extern char *gp_ng_idcp_cfg_client_tls_ecdhcurve;
extern char *gp_ng_idcp_cfg_client_tls_key_file;
extern char *gp_ng_idcp_cfg_client_tls_protocols;
extern char *gp_ng_idcp_cfg_client_tls_sslmode;
extern char *gp_ng_idcp_cfg_ignore_startup_parameters;
extern char *gp_ng_idcp_cfg_job_name;
extern char *gp_ng_idcp_cfg_listen_addr;
extern char *gp_ng_idcp_cfg_logfile;
extern char *gp_ng_idcp_cfg_pidfile;
extern char *gp_ng_idcp_cfg_resolv_conf;
extern char *gp_ng_idcp_cfg_server_check_query;
extern char *gp_ng_idcp_cfg_server_reset_query;
extern char *gp_ng_idcp_cfg_server_tls_ca_file;
extern char *gp_ng_idcp_cfg_server_tls_cert_file;
extern char *gp_ng_idcp_cfg_server_tls_ciphers;
extern char *gp_ng_idcp_cfg_server_tls_key_file;
extern char *gp_ng_idcp_cfg_server_tls_protocols;
extern char *gp_ng_idcp_cfg_server_tls_sslmode;
extern char *gp_ng_idcp_cfg_service_name;
extern char *gp_ng_idcp_cfg_tcp_keepalive;
extern char *gp_ng_idcp_cfg_track_extra_parameters;
extern char *gp_ng_idcp_cfg_unix_socket_dir;
extern char *gp_ng_idcp_cfg_unix_socket_group;

/* ========================================================================= */
/* -- PUBLIC FUNCTION PROTOTYPES ------------------------------------------- */
/* ========================================================================= */

extern int ConnectionProxyStart(void);
extern int ConnectionProxyShmemSize(void);
extern void ConnectionProxyShmemInit(void);

/* ========================================================================= */
/* -- PUBLIC INLINE FUNCTIONS ---------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC HELPER FUNCTIONS ---------------------------------------------- */
/* ========================================================================= */

/* vim: set ts=2 et sw=2 ft=c: */

#endif /* NG_IDCP_H */
