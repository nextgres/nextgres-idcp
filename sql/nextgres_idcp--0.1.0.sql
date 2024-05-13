
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
--\echo Use CREATE EXTENSION worker_spi to load this file. \quit

CREATE SCHEMA _nextgres_idcp;
GRANT USAGE ON SCHEMA _nextgres_idcp TO PUBLIC;

SET SEARCH_PATH TO _nextgres_idcp;
CREATE TYPE _nextgres_idcp.nextgres_idcp_pool_mode AS ENUM ('session', 'transaction', 'statement');

/*
CREATE TABLE IF NOT EXISTS _nextgres_idcp.nextgres_idcp (
  application_name_add_host BIGINT,
  auth_dbname TEXT,
  auth_file TEXT,
  auth_hba_file TEXT,
  auth_ident_file TEXT,
  auth_query TEXT,
  auth_type TEXT,
  autodb_idle_timeout BIGINT,
  cancel_wait_timeout BIGINT,
  client_idle_timeout BIGINT,
  client_login_timeout BIGINT,
  client_tls_ca_file TEXT,
  client_tls_cert_file TEXT,
  client_tls_ciphers TEXT,
  client_tls_dheparams TEXT,
  client_tls_ecdhcurve TEXT,
  client_tls_key_file TEXT,
  client_tls_protocols TEXT,
  client_tls_sslmode TEXT,
  default_pool_size BIGINT,
  disable_pqexec BIGINT,
  dns_max_ttl BIGINT,
  dns_nxdomain_ttl BIGINT,
  dns_zone_check_period BIGINT,
  idle_transaction_timeout BIGINT,
  ignore_startup_parameters TEXT,
  job_name TEXT,
  listen_addr TEXT,
  listen_backlog BIGINT,
  listen_port BIGINT,
  log_connections BIGINT,
  log_disconnections BIGINT,
  log_pooler_errors BIGINT,
  log_stats BIGINT,
  logfile TEXT,
  max_client_conn BIGINT,
  max_db_connections BIGINT,
  max_packet_size BIGINT,
  max_prepared_statements BIGINT,
  max_user_connections BIGINT,
  min_pool_size BIGINT,
  peer_id BIGINT,
  pidfile TEXT,
  pkt_buf BIGINT,
  pool_mode nextgres_idcp_pool_mode,
  query_timeout BIGINT,
  query_wait_timeout BIGINT,
  reserve_pool_size BIGINT,
  reserve_pool_timeout BIGINT,
  resolv_conf TEXT,
  sbuf_loopcnt BIGINT,
  server_check_delay BIGINT,
  server_check_query TEXT,
  server_connect_timeout BIGINT,
  server_fast_close BIGINT,
  server_idle_timeout BIGINT,
  server_lifetime BIGINT,
  server_login_retry BIGINT,
  server_reset_query TEXT,
  server_reset_query_always BIGINT,
  server_round_robin BIGINT,
  server_tls_ca_file TEXT,
  server_tls_cert_file TEXT,
  server_tls_ciphers TEXT,
  server_tls_key_file TEXT,
  server_tls_protocols TEXT,
  server_tls_sslmode TEXT,
  service_name TEXT,
  so_reuseport BIGINT,
  stats_period BIGINT,
  suspend_timeout BIGINT,
  tcp_defer_accept BIGINT,
  tcp_keepalive TEXT,
  tcp_keepcnt BIGINT,
  tcp_keepidle BIGINT,
  tcp_keepintvl BIGINT,
  tcp_socket_buffer BIGINT,
  tcp_user_timeout BIGINT,
  track_extra_parameters TEXT,
  unix_socket_dir TEXT,
  unix_socket_group TEXT,
  unix_socket_mode BIGINT,
  verbosity BIGINT
);
*/

CREATE TABLE IF NOT EXISTS _nextgres_idcp.databases (
  database_name                   NAME NOT NULL,
  auth_database_name              NAME,
  auth_query                      TEXT,
  auth_user                       NAME,
  backend_database_name           NAME NOT NULL,
  client_encoding                 TEXT,
  connect_query                   TEXT,
  host                            TEXT,
  max_db_connections              INTEGER,
  min_pool_size                   INTEGER,
  pool_mode                       _nextgres_idcp.nextgres_idcp_pool_mode,
  pool_size                       INTEGER,
  port                            BIGINT,
  reserve_pool                    BIGINT,
  specific_datestyle              TEXT,
  specific_timezone               TEXT,
  specific_user                   NAME,
  PRIMARY KEY (database_name),
  FOREIGN KEY (auth_database_name)
    REFERENCES _nextgres_idcp.databases (database_name));

CREATE TABLE IF NOT EXISTS _nextgres_idcp.admin_users (
  user_name                       NAME NOT NULL,
  PRIMARY KEY (user_name));

CREATE TABLE IF NOT EXISTS _nextgres_idcp.stats_users (
  user_name                       NAME NOT NULL,
  PRIMARY KEY (user_name));

CREATE TABLE IF NOT EXISTS _nextgres_idcp.users (
  user_name                       NAME NOT NULL,
  max_user_connections            BIGINT,
  pool_mode                       nextgres_idcp_pool_mode,
  PRIMARY KEY (user_name));

