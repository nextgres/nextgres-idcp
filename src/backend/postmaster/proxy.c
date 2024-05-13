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
 * This file represents a dynamic background worker that listens for incoming
 * connections and pools those connections to N backend servers.
 *
 * ENGINEERING APPROACH
 *
 *  1. ALPHA - Extract connection pool proxy patch from core and evaluate it
 *     as an extension. We do this prior to PgBouncer incorporation as it has
 *     a number of better low-level concepts directly applicable to our use
 *     case. However, it's lacking multiple pool strategies, support for
 *     prepared statements, and requires a peering-like structure to accomplish
 *     things like query cancellation (unless a separate backend is used solely
 *     for this.) Still, it sets us up better for success as the preliminary
 *     architecture and its deficiencies will be alleviated by incorporating
 *     our subsequent work already done. Alpha should follow KISS principle:
 *     Konstantin's work works for the basic cases, we just need to make sure
 *     it runs as an extension and we'll deal with the cases it doesn't work
 *     with later.
 *
 *  2. BETA - Incorporate multi-threaded PgBouncer components to optimize this
 *     extension. Largely, add support for prepared statements and eliminate
 *     the need to create multiple dynamic background workers for parallelism.
 *     Additionally, this allows us to avoid partitioning N backends among M
 *     connection proxies, making coordination much simpler and direct. Also,
 *     the large function bodies and broad if statements slow this down,
 *     where we see ~5% improvement in splitting frontend/backend handling
 *     into separate functions.
 *
 *  3. GAMMA - Incorporate our multi-protocol APIs to add support for MySQL
 *     et al.
 */

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* -------------------------- Interface Inclusions ------------------------- */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/xlog.h"
#include "commands/defrem.h"
#include "common/ip.h"
#include "common/string.h"
#include "funcapi.h"
#include "internal/libpq-int.h"
#include "libpq-fe.h"
#include "libpq/libpq-be.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "parser/parse_expr.h"
#include "pgstat.h"
#include "postmaster/fork_process.h"
#include "postmaster/interrupt.h"
#include "postmaster/postmaster.h"
#include "replication/walsender.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/guc_hooks.h"
#include "utils/memutils.h"
#include "utils/pidfile.h"
#include "utils/portal.h"
#include "utils/ps_status.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
#include "utils/varlena.h"

/* --------------------------- System Inclusions --------------------------- */

#include <errno.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>

/* --------------------------- Project Inclusions -------------------------- */

#include "nextgres/idcp.h"
#include "nextgres/idcp/libpq/libpq.h"
#include "nextgres/idcp/postmaster/postmaster.h"
#include "nextgres/idcp/postmaster/proxy.h"

/* ========================================================================= */
/* -- LOCAL DEFINITIONS ---------------------------------------------------- */
/* ========================================================================= */

#define DB_HASH_SIZE            101
#define INIT_BUF_SIZE           (64 * 1024)
#define MAXLISTEN               64
#define MAX_READY_EVENTS        128
#define PROXY_WAIT_TIMEOUT      1000 /* 1 second */
#define WL_SOCKET_EDGE          (1 << 7)

/* Channel state */
#define ACTIVE_CHANNEL_MAGIC    0xDEFA1234U
#define REMOVED_CHANNEL_MAGIC   0xDEADDEEDU

/* ========================================================================= */
/* -- LOCAL MACROS --------------------------------------------------------- */
/* ========================================================================= */

#define NULLSTR(s) ((s) ? (s) : "?")

/*
 * #define ELOG(severity, fmt,...) elog(severity, "PROXY: " fmt, ##
 * __VA_ARGS__)
 */
#define ELOG(severity, fmt, ...)

/* ========================================================================= */
/* -- LOCAL TYPEDEFS ------------------------------------------------------- */
/* ========================================================================= */

struct Channel;
struct PoolerStateContext;
struct Proxy;
struct SessionPool;
struct SessionPoolKey;

/* ========================================================================= */
/* -- LOCAL STRUCTURES ----------------------------------------------------- */
/* ========================================================================= */

typedef struct SessionPoolKey {
  char                  database[NAMEDATALEN];
  char                  username[NAMEDATALEN];
} SessionPoolKey;

/*
 * Channels represent both clients and backends
 */
typedef struct Channel {
  int                   magic;
  char                 *buf;
  int                   rx_pos;
  int                   tx_pos;
  int                   tx_size;
  int                   buf_size;

  /** Position of wait event returned by AddWaitEventToSet */
  int                   event_pos;

  /** Not null for client, null for server */
  Port                 *client_port;

  pgsocket              backend_socket;
  PGPROC               *backend_proc;
  int                   backend_pid;

  /** ready for query */
  bool                  backend_is_ready;

  /** client interrupts query execution */
  bool                  is_interrupted;

  /** connection is lost */
  bool                  is_disconnected;

  /** no activity on this channel */
  bool                  is_idle;

  /** inside transaction body */
  bool                  in_transaction;

  /* emulate epoll EPOLLET (edge-triggered) flag */
  bool                  edge_triggered;

  /*
   * We need to save startup packet response to be able to send it to new
   * connection
   */
  int                   handshake_response_size;
  char                 *handshake_response;

  /** time of last backend activity */
  TimestampTz           backend_last_activity;

  /** concatenated "SET var=" commands for this session */
  char                 *gucs;

  /** previous value of "gucs" to perform rollback in case of error */
  char                 *prev_gucs;

  /** the linked backend channel (when this is a client) */
  struct Channel       *peer;
  struct Channel       *next;
  struct Proxy         *proxy;
  struct SessionPool   *pool;
} Channel;

/*
 * Control structure for connection proxies (several proxy workers can be
 * launched and each has its own proxy instance). Proxy contains hash of
 * session pools for reach role/dbname combination.
 */
typedef struct Proxy {
  /** Temporary memory context used for parsing startup packet */
  MemoryContext         parse_ctx;

  /** Set of socket descriptors of backends and clients socket descriptors */
  WaitEventSet         *wait_events;

  /** Session pool map with dbname/role used as a key */
  HTAB                 *pools;

  /**
   * Number of accepted, but not yet established connections (startup packet is
   * not received and db/role are not known)
   */
  int                   n_accepted_connections;

  /** Maximal number of backends per database */
  int                   max_backends;

  /** Shutdown flag */
  bool                  shutdown;

  /** List of disconnected backends */
  Channel              *hangout;

  /** State of proxy */
  ConnectionProxyState *state;

  /** Time of last check for idle worker timeout expration */
  TimestampTz           last_idle_timeout_check;
} Proxy;

/*
 * Connection pool to particular role/dbname
 */
typedef struct SessionPool {
  SessionPoolKey        key;

  /** List of idle clients */
  Channel              *idle_backends;

  /** List of clients waiting for free backend */
  Channel              *pending_clients;

  /** Owner of this pool */
  Proxy                *proxy;

  /** Total number of launched backends */
  int                   n_launched_backends;

  /** Number of dedicated (tainted) backends */
  int                   n_dedicated_backends;

  /** Number of backends in idle state */
  int                   n_idle_backends;

  /** Total number of connected clients */
  int                   n_connected_clients;

  /** Number of clients in idle state */
  int                   n_idle_clients;

  /** Number of clients waiting for free backend */
  int                   n_pending_clients;

  /** List of startup options specified in startup packet */
  List                 *startup_gucs;

  /** Command line options passed to backend */
  char                 *cmdline_options;
} SessionPool;

typedef struct PoolerStateContext {
  int proxy_id;
  TupleDesc ret_desc;
} PoolerStateContext;

/* ========================================================================= */
/* -- PRIVATE FUNCTION PROTOTYPES ------------------------------------------ */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION PROTOTYPES -------------------------------------------- */
/* ========================================================================= */

static Channel *backend_start(SessionPool *pool, char **error);
static Channel *channel_create(Proxy *proxy);
static List *string_list_copy(List *orig);
static bool backend_reschedule(Channel *chan, bool is_new);
static bool channel_read(Channel *chan);
static bool channel_register(Proxy *proxy, Channel *chan);
static bool channel_write(Channel *chan, bool synchronous);
static bool client_attach(Channel *chan);
static bool client_connect(Channel *chan, int startup_packet_size);
static bool is_transaction_start(char *stmt);
static bool is_transactional_statement(char *stmt);
static bool string_equal(char const *a, char const *b);
static bool string_list_equal(List *a, List *b);
static char *string_append(char *dst, char const *src);
static size_t string_length(char const *str);
static size_t string_list_length(List *list);
static ssize_t socket_write(Channel *chan, char const *buf, size_t size);
static void channel_hangout(Channel *chan, char const *op);
static void channel_remove(Channel *chan);
static void proxy_add_client(Proxy *proxy, Port *port);
static void proxy_handle_sigterm(SIGNAL_ARGS);
static void proxy_loop(Proxy *proxy);
static void report_error_to_client(Channel *chan, char const *error);
static void *libpq_connectdb(char const *keywords[], char const *values[],
                             char **error);
static Proxy *proxy_create (ConnectionProxyState *state, int max_backends);
static void proxy_add_listen_socket(Proxy *proxy, pgsocket socket);

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE VARIABLES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL VARIABLES ------------------------------------------------------ */
/* ========================================================================= */

bool WaitEventUseEpoll = false;
static Proxy *proxy;
int MyProxyId;
pgsocket MyProxySocket;
ConnectionProxyState *ProxyState = NULL;

/* ========================================================================= */
/* -- STATIC ASSERTIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC FUNCTION DEFINITIONS ------------------------------------------ */
/* ========================================================================= */

PGDLLEXPORT void
ng_idcp_proxy_main (
  Datum db_oid
) {
  static int ListenSocket[MAXLISTEN];
  int i;
  int nsockets = 0;
  Proxy *proxy = NULL;

  pqsignal(SIGTERM, proxy_handle_sigterm);

  ProxyState = calloc(32, sizeof(*ProxyState));

  for (i = 0; i < MAXLISTEN; i++) {
    ListenSocket[i] = PGINVALID_SOCKET;
  }

  /*
   * Establish input sockets.
   */
  if (ListenAddresses) {
    char *rawstring;
    List *elemlist;
    ListCell *l;
    int success = 0;
    int status;

    /* Need a modifiable copy of ListenAddresses */
    rawstring = pstrdup(ListenAddresses);

    /* Parse string into list of hostnames */
    if (!SplitGUCList(rawstring, ',', &elemlist)) {
      /* syntax error in list */
      ereport(FATAL, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("invalid list syntax in parameter \"%s\"",
                             "listen_addresses")));
    }

    foreach (l, elemlist) {
      char *curhost = (char *)lfirst(l);

      if (strcmp(curhost, "*") == 0) {
        status = ng_idcp_stream_server_port(
          AF_UNSPEC, NULL, (unsigned short)g_ng_idcp_cfg_listen_port,
          NULL, ListenSocket, MAXLISTEN);
      } else {
        status = ng_idcp_stream_server_port(
          AF_UNSPEC, curhost,
          (unsigned short)g_ng_idcp_cfg_listen_port, NULL,
          ListenSocket, MAXLISTEN);
      }

      if (status == STATUS_OK) {
        success++;
      } else {
        ereport(FATAL, (errmsg("could not create listen socket for \"%s\"",
                               ListenAddresses)));
      }
    }

    if (!success && elemlist != NIL)
      ereport(FATAL, (errmsg("could not create any TCP/IP sockets")));

    list_free(elemlist);
    pfree(rawstring);
  }

  /* How many server sockets do we need to wait for? */
  while (nsockets < MAXLISTEN && ListenSocket[nsockets] != PGINVALID_SOCKET)
    ++nsockets;

  proxy = proxy_create(&ProxyState[0], SessionPoolSize);

  for (i = 0; i < nsockets; i++) {
    proxy_add_listen_socket(proxy, ListenSocket[i]);
  }
  proxy_loop(proxy);

  proc_exit(0);

} /* ng_idcp_proxy_main() */

/* ========================================================================= */
/* -- PRIVATE FUNCTION DEFINITIONS ----------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION DEFINITIONS ------------------------------------------- */
/* ========================================================================= */

/**
 * Backend is ready for next command outside transaction block (idle state).
 * Now if backend is not tainted it is possible to schedule some other client
 * to this backend.
 */
static bool
backend_reschedule (
  Channel  *chan,
  bool      is_new
) {
  Channel *pending = chan->pool->pending_clients;

  chan->backend_is_ready = false;

  /* Lazy resolving of PGPROC entry */
  if (chan->backend_proc == NULL) {
    Assert(chan->backend_pid != 0);
    chan->backend_proc = BackendPidGetProc(chan->backend_pid);

    /*
     * If backend completes execution of some query, then it has definitely
     * registered itself in procarray.
     */
    Assert(chan->backend_proc);
  }

  if (chan->peer) {
    chan->peer->peer = NULL;
    chan->pool->n_idle_clients += 1;
    chan->pool->proxy->state->n_idle_clients += 1;
    chan->peer->is_idle = true;
  }
  if (pending) {
    /* Has pending clients: serve one of them */
    ELOG(LOG, "Backed %d is reassigned to client %p", chan->backend_pid,
      pending);
    chan->pool->pending_clients = pending->next;
    Assert(chan != pending);
    chan->peer = pending;
    pending->peer = chan;
    chan->pool->n_pending_clients -= 1;
    if (pending->tx_size == 0) /* new client has sent startup packet and we now
                                  need to send handshake response */
    {
      Assert(chan->handshake_response !=
             NULL); /* backend already sent handshake response */
      Assert(chan->handshake_response_size < chan->buf_size);
      memcpy(chan->buf, chan->handshake_response,
             chan->handshake_response_size);
      chan->rx_pos = chan->tx_size = chan->handshake_response_size;
      ELOG(LOG, "Simulate response for startup packet to client %p", pending);
      chan->backend_is_ready = true;
      return channel_write(pending, false);
    } else {
      ELOG(LOG,
           "Try to send pending request from client %p to backend %p (pid %d)",
           pending, chan, chan->backend_pid);
      Assert(pending->tx_pos == 0 && pending->rx_pos >= pending->tx_size);
      return channel_write(chan, false); /* Send pending request to backend */
    }
  } else /* return backend to the list of idle backends */
  {
    ELOG(LOG, "Backed %d is idle", chan->backend_pid);
    Assert(!chan->client_port);
    chan->next = chan->pool->idle_backends;
    chan->pool->idle_backends = chan;
    chan->pool->n_idle_backends += 1;
    chan->pool->proxy->state->n_idle_backends += 1;
    chan->is_idle = true;
    chan->peer = NULL;
  }
  return true;
} /* backend_reschedule() */

/* ------------------------------------------------------------------------- */

/*
 * Start new backend for particular pool associated with dbname/role
 * combination. Backend is forked using BackendStartup function.
 */
static Channel *
backend_start (
  SessionPool    *pool,
  char          **error
) {
  Channel *chan;
  char postmaster_port[8];
  char *options = (char *) palloc(string_length(pool->cmdline_options)
    + string_list_length(pool->startup_gucs)
    + list_length(pool->startup_gucs) / 2 * 5 + 1);
  char const *keywords[] = {
    "port",
    "dbname",
    "user",
    "sslmode",
    "application_name",
    "options",
    NULL
  };
  char const *values[] = {
    postmaster_port,
    pool->key.database,
    pool->key.username,
    "disable",
    NEXTGRES_EXTNAME "_worker_backend",
    options,
    NULL
  };

  PGconn *conn;
  char *msg;
  int int32_buf;
  int msg_len;
  ListCell *gucopts;
  char *dst = options;

  pg_ltoa(PostPortNumber, postmaster_port);

  gucopts = list_head(pool->startup_gucs);
  if (pool->cmdline_options)
    dst += sprintf(dst, "%s", pool->cmdline_options);
  while (gucopts) {
    char *name;
    char *value;

    name = lfirst(gucopts);
    gucopts = lnext(pool->startup_gucs, gucopts);

    value = lfirst(gucopts);
    gucopts = lnext(pool->startup_gucs, gucopts);

    if (strcmp(name, "application_name") != 0) {
      dst += sprintf(dst, " -c %s=", name);
      dst = string_append(dst, value);
    }
  }
  *dst = '\0';
  conn = libpq_connectdb(keywords, values, error);
  pfree(options);
  if (!conn)
    return NULL;

  chan = channel_create(pool->proxy);
  chan->pool = pool;
  chan->backend_socket = conn->sock;
  /* Using edge epoll mode requires non-blocking sockets */
  pg_set_noblock(conn->sock);

  /* Save handshake response */
  chan->handshake_response_size = conn->inEnd;
  chan->handshake_response = palloc(chan->handshake_response_size);
  memcpy(chan->handshake_response, conn->inBuffer,
         chan->handshake_response_size);

  /* Extract backend pid */
  msg = chan->handshake_response;
  while (*msg != 'K') /* Scan handshake response until we reach PID message */
  {
    memcpy(&int32_buf, ++msg, sizeof(int32_buf));
    msg_len = ntohl(int32_buf);
    msg += msg_len;
    Assert(msg < chan->handshake_response + chan->handshake_response_size);
  }
  memcpy(&int32_buf, msg + 5, sizeof(int32_buf));
  chan->backend_pid = ntohl(int32_buf);

  if (channel_register(pool->proxy, chan)) {
    pool->proxy->state->n_backends += 1;
    pool->n_launched_backends += 1;
  } else {
    *error = strdup("Too much sessios: try to increase 'max_sessions' "
                    "configuration parameter");
    /* Too much sessions, error report was already logged */
    closesocket(chan->backend_socket);
    chan->magic = REMOVED_CHANNEL_MAGIC;
    pfree(chan->buf);
    pfree(chan);
    chan = NULL;
  }
  return chan;
} /* backend_start() */

/* ------------------------------------------------------------------------- */

/*
 * Create new channel.
 */
static Channel *
channel_create (
  Proxy *proxy
) {
  Channel *chan = (Channel *)palloc0(sizeof(Channel));
  chan->magic = ACTIVE_CHANNEL_MAGIC;
  chan->proxy = proxy;
  chan->buf = palloc(INIT_BUF_SIZE);
  chan->buf_size = INIT_BUF_SIZE;
  chan->tx_pos = chan->rx_pos = chan->tx_size = 0;
  return chan;
} /* channel_create() */

/* ------------------------------------------------------------------------- */

/*
 * Handle communication failure for this channel.
 * It is not possible to remove channel immediately because it can be triggered
 * by other epoll events. So link all channels in L1 list for pending delete.
 */
static void
channel_hangout (
  Channel        *chan,
  char const     *op
) {
  Channel **ipp;
  Channel *peer = chan->peer;
  if (chan->is_disconnected || chan->pool == NULL)
    return;

  if (chan->client_port) {
    ELOG(LOG, "Hangout client %p due to %s error: %m", chan, op);
    for (ipp = &chan->pool->pending_clients; *ipp != NULL;
         ipp = &(*ipp)->next) {
      if (*ipp == chan) {
        *ipp = chan->next;
        chan->pool->n_pending_clients -= 1;
        break;
      }
    }
    if (chan->is_idle) {
      chan->pool->n_idle_clients -= 1;
      chan->pool->proxy->state->n_idle_clients -= 1;
      chan->is_idle = false;
    }
  } else {
    ELOG(LOG, "Hangout backend %p (pid %d) due to %s error: %m", chan,
         chan->backend_pid, op);
    for (ipp = &chan->pool->idle_backends; *ipp != NULL; ipp = &(*ipp)->next) {
      if (*ipp == chan) {
        Assert(chan->is_idle);
        *ipp = chan->next;
        chan->pool->n_idle_backends -= 1;
        chan->pool->proxy->state->n_idle_backends -= 1;
        chan->is_idle = false;
        break;
      }
    }
  }
  if (peer) {
    peer->peer = NULL;
    chan->peer = NULL;
  }
  chan->backend_is_ready = false;

  if (chan->client_port && peer) /* If it is client connected to backend. */
  {
    if (!chan->is_interrupted) /* Client didn't sent 'X' command, so do it for
                                  him. */
    {
      ELOG(LOG, "Send terminate command to backend %p (pid %d)", peer,
           peer->backend_pid);
      peer->is_interrupted =
          true; /* interrupted flags makes channel_write to send 'X' message */
      channel_write(peer, false);
      return;
    } else if (!peer->is_interrupted) {
      /* Client already sent 'X' command, so we can safely reschedule backend
       * to some other client session */
      backend_reschedule(peer, false);
    }
  }
  chan->next = chan->proxy->hangout;
  chan->proxy->hangout = chan;
  chan->is_disconnected = true;
} /* channel_hangout() */

/* ------------------------------------------------------------------------- */

/*
 * Try to read more data from the channel and send it to the peer.
 */
static bool
channel_read (
  Channel *chan
) {
  int msg_start;
  while (chan->tx_size == 0) /* there is no pending write op */
  {
    ssize_t rc;
    bool handshake = false;
#ifdef USE_SSL
    int waitfor = 0;
    if (chan->client_port && chan->client_port->ssl_in_use)
      rc = be_tls_read(chan->client_port, chan->buf + chan->rx_pos,
                       chan->buf_size - chan->rx_pos, &waitfor);
    else
#endif
      rc = chan->client_port
               ? secure_raw_read(chan->client_port, chan->buf + chan->rx_pos,
                                 chan->buf_size - chan->rx_pos)
               : recv(chan->backend_socket, chan->buf + chan->rx_pos,
                      chan->buf_size - chan->rx_pos, 0);
    ELOG(LOG, "%p: read %d: %m", chan, (int)rc);

    if (rc <= 0) {
      if (rc == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        channel_hangout(chan, "read");
      return false; /* wait for more data */
    } else if (chan->edge_triggered) {
      /* resume accepting all events */
      ModifyWaitEvent(
          chan->proxy->wait_events, chan->event_pos,
          WL_SOCKET_READABLE | WL_SOCKET_WRITEABLE | WL_SOCKET_EDGE, NULL);
      chan->edge_triggered = false;
    }

    if (!chan->client_port)
      ELOG(LOG,
           "Receive reply %c %d bytes from backend %d (%p:ready=%d) to client "
           "%d",
           chan->buf[0] ? chan->buf[0] : '?', (int)rc + chan->rx_pos,
           chan->backend_pid, chan, chan->backend_is_ready,
           chan->peer ? chan->peer->client_port->sock : -1);
    else
      ELOG(LOG,
           "Receive command %c %d bytes from client %d to backend %d "
           "(%p:ready=%d)",
           chan->buf[0] ? chan->buf[0] : '?', (int)rc + chan->rx_pos,
           chan->client_port->sock, chan->peer ? chan->peer->backend_pid : -1,
           chan->peer, chan->peer ? chan->peer->backend_is_ready : -1);

    chan->rx_pos += rc;
    msg_start = 0;

    /* Loop through all received messages */
    while (chan->rx_pos - msg_start >= 5) /* has message code + length */
    {
      int msg_len;
      uint32 new_msg_len;

      if (chan->pool == NULL) {
        /* process startup packet */
        Assert(msg_start == 0);
        memcpy(&msg_len, chan->buf + msg_start, sizeof(msg_len));
        msg_len = ntohl(msg_len);
        handshake = true;
      } else {
        ELOG(LOG, "%p receive message %c", chan, chan->buf[msg_start]);
        memcpy(&msg_len, chan->buf + msg_start + 1, sizeof(msg_len));
        msg_len = ntohl(msg_len) + 1;
      }

      if (msg_start + msg_len > chan->buf_size) {
        /* Reallocate buffer to fit complete message body */
        chan->buf_size = msg_start + msg_len;
        chan->buf = repalloc(chan->buf, chan->buf_size);
      }

      if (chan->rx_pos - msg_start >= msg_len) {
        /* Message is completely fetched */
        if (chan->pool == NULL) {
          /* receive startup packet */
          Assert(chan->client_port);
          if (!client_connect(chan, msg_len)) {
            /* Some trouble with processing startup packet */
            chan->is_disconnected = true;
            channel_remove(chan);
            return false;
          }
        } else if (!chan->client_port) {
          /* Message from backend */
          if (chan->buf[msg_start] == 'Z' && chan->buf[msg_start + 5] == 'I') {
            /* Ready for query && transaction block status is idle */

            /* Should be last message */
            Assert(chan->rx_pos - msg_start == msg_len);

            chan->backend_is_ready = true; /* Backend is ready for query */
            chan->proxy->state->n_transactions += 1;
            if (chan->peer) {
              chan->peer->in_transaction = false;
            }
          } else if (chan->buf[msg_start] == 'E') {
            /* Error */
            if (chan->peer && chan->peer->prev_gucs) {
              /* Undo GUC assignment */
              pfree(chan->peer->gucs);
              chan->peer->gucs = chan->peer->prev_gucs;
              chan->peer->prev_gucs = NULL;
            }
          }
        } else if (chan->client_port) {
          /* Message from client */
          switch (chan->buf[msg_start]) {
            /* one-packet queries */
            case 'Q':    /* Query */
              if ((ProxyingGUCs || MultitenantProxy) && !chan->in_transaction) {
                char *stmt = &chan->buf[msg_start + 5];
                if (chan->prev_gucs) {
                  pfree(chan->prev_gucs);
                  chan->prev_gucs = NULL;
                }
                if (ProxyingGUCs &&
                    ((pg_strncasecmp(stmt, "set", 3) == 0 &&
                      pg_strncasecmp(stmt + 3, " local", 6) != 0) ||
                     pg_strncasecmp(stmt, "reset", 5) == 0)) {
                  char *new_msg;
                  chan->prev_gucs = chan->gucs ? chan->gucs : pstrdup("");
                  if (pg_strncasecmp(stmt, "reset", 5) == 0) {
                    char *semi = strchr(stmt + 5, ';');
                    if (semi)
                      *semi = '\0';
                    chan->gucs = psprintf("%sset local%s=default;",
                                          chan->prev_gucs, stmt + 5);
                  } else {
                    char *param = stmt + 3;
                    if (pg_strncasecmp(param, " session", 8) == 0)
                      param += 8;
                    chan->gucs =
                        psprintf("%sset local%s%c", chan->prev_gucs, param,
                                 chan->buf[chan->rx_pos - 2] == ';' ? ' ' : ';');
                  }
                  new_msg = chan->gucs + strlen(chan->prev_gucs);
                  Assert(msg_start + strlen(new_msg) * 2 + 6 < chan->buf_size);
                  /*
                   * We need to send SET command to check if it is correct.
                   * To avoid "SET LOCAL can only be used in transaction blocks"
                   * error we need to construct block. Let's just double the
                   * command.
                   */
                  msg_len = sprintf(stmt, "%s%s", new_msg, new_msg) + 6;
                  new_msg_len = pg_hton32(msg_len - 1);
                  memcpy(&chan->buf[msg_start + 1], &new_msg_len,
                         sizeof(new_msg_len));
                  chan->rx_pos = msg_start + msg_len;
                } else if (chan->gucs && is_transactional_statement(stmt)) {
                  size_t gucs_len = strlen(chan->gucs);
                  if (chan->rx_pos + gucs_len + 1 > chan->buf_size) {
                    /* Reallocate buffer to fit concatenated GUCs */
                    chan->buf_size = chan->rx_pos + gucs_len + 1;
                    chan->buf = repalloc(chan->buf, chan->buf_size);
                  }
                  if (is_transaction_start(stmt)) {
                    /* Append GUCs after BEGIN command to include them in
                     * transaction body */
                    Assert(chan->buf[chan->rx_pos - 1] == '\0');
                    if (chan->buf[chan->rx_pos - 2] != ';') {
                      chan->buf[chan->rx_pos - 1] = ';';
                      chan->rx_pos += 1;
                      msg_len += 1;
                    }
                    memcpy(&chan->buf[chan->rx_pos - 1], chan->gucs, gucs_len + 1);
                    chan->in_transaction = true;
                  } else {
                    /* Prepend standalone command with GUCs */
                    memmove(stmt + gucs_len, stmt, msg_len);
                    memcpy(stmt, chan->gucs, gucs_len);
                  }
                  chan->rx_pos += gucs_len;
                  msg_len += gucs_len;
                  new_msg_len = pg_hton32(msg_len - 1);
                  memcpy(&chan->buf[msg_start + 1], &new_msg_len,
                         sizeof(new_msg_len));
                } else if (is_transaction_start(stmt))
                  chan->in_transaction = true;
              }
              break;

            case 'F':    /* FunctionCall */
              break;

            /* request immediate response from server */
            case 'S':    /* Sync */
              break;

            case 'H':    /* Flush */
              break;

            /* copy end markers */
            case 'c':    /* CopyDone(F/B) */
            case 'f':    /* CopyFail(F/B) */
              break;

            /*
             * extended protocol allows server (and thus pooler)
             * to buffer packets until sync or flush is sent by client
             */
            case 'P':    /* Parse */
              break;

            case 'E':    /* Execute */
              break;

            case 'C':    /* Close */
              break;

            case 'B':    /* Bind */
              break;

            case 'D':    /* Describe */
              break;

            case 'd':    /* CopyData(F/B) */
              break;

            case 'X':  /* Terminate */
              {
                Channel *backend = chan->peer;
                elog(DEBUG1, "Receive 'X' to backend %d",
                     backend != NULL ? backend->backend_pid : 0);
                chan->is_interrupted = true;
                if (backend != NULL && !backend->backend_is_ready) {
                  /* If client send abort inside transaction, then mark backend as
                   * tainted */
                  chan->proxy->state->n_dedicated_backends += 1;
                  chan->pool->n_dedicated_backends += 1;
                }
                if (backend == NULL) {
                  /* Skip terminate message to idle and non-tainted backends */
                  channel_hangout(chan, "terminate");
                  return false;
                }
              }
              break;

            /* client wants to go away */
            default:
              elog(WARNING, "unknown pkt from client: %c", chan->buf[msg_start]);
          }

        }
        msg_start += msg_len;
      } else {
        break; /* Incomplete message. */
      }
    }
    elog(DEBUG1, "Message size %d", msg_start);
    if (msg_start != 0) {
      /* Has some complete messages to send to peer */
      if (chan->peer == NULL)
      {
        /* client is not yet connected to backend */
        if (!chan->client_port) {
          /*
           * We are not expecting messages from idle backend. Assume that it
           * some error or shutdown.
           */
          channel_hangout(chan, "idle");
          return false;
        }
        client_attach(chan);
        if (handshake) {
          /* Send handshake response to the client */
          /*
           * If we attach new client to the existed backend, then we need to
           * send handshake response to the client
           */
          Channel *backend = chan->peer;
          chan->rx_pos = 0;    /* Skip startup packet */
          if (backend != NULL) /* Backend was assigned */
          {
            /* Ensure backend hasn't already sent handshake responses */
            Assert(backend->handshake_response != NULL);
            Assert(backend->handshake_response_size < backend->buf_size);
            memcpy(backend->buf, backend->handshake_response,
              backend->handshake_response_size);
            backend->rx_pos = backend->tx_size =
              backend->handshake_response_size;
            backend->backend_is_ready = true;
            elog(DEBUG1, "Send handshake response to the client");
            return channel_write(chan, false);
          } else {
            /*
             * Handshake response will be send to client later when backend is
             * assigned
             */
            elog(DEBUG1, "Handshake response will be sent to the client later "
                         "when backed is assigned");
            return false;
          }
        } else if (chan->peer == NULL) {
          /* Backend was not assigned */
          chan->tx_size = msg_start;
          /* query will be send later once backend is assigned */
          elog(DEBUG1, "Query will be sent to this client later when backed "
                       "is assigned");
          return false;
        }
      }
      Assert(chan->tx_pos == 0);
      Assert(chan->rx_pos >= msg_start);
      chan->tx_size = msg_start;
      if (!channel_write(chan->peer, true)) {
        return false;
      }
    }
    /* If backend is out of transaction, then reschedule it */
    if (chan->backend_is_ready) {
      return backend_reschedule(chan, false);
    }

    /* Do not try to read more data if edge-triggered mode is not supported */
    if (!WaitEventUseEpoll) {
      break;
    }
  }
  return true;
} /* channel_read() */

/* ------------------------------------------------------------------------- */

/*
 * Register new channel in wait event set.
 */
static bool
channel_register (
  Proxy      *proxy,
  Channel    *chan
) {
  pgsocket sock =
      chan->client_port ? chan->client_port->sock : chan->backend_socket;
  /* Using edge epoll mode requires non-blocking sockets */
  pg_set_noblock(sock);
  chan->event_pos = AddWaitEventToSet(proxy->wait_events,
                                      WL_SOCKET_READABLE |
                                          WL_SOCKET_WRITEABLE | WL_SOCKET_EDGE,
                                      sock, NULL, chan);
  if (chan->event_pos < 0) {
    elog(WARNING,
         "PROXY: Failed to add new client - too much sessions: %d clients, %d "
         "backends. "
         "Try to increase 'max_sessions' configuration parameter.",
         proxy->state->n_clients, proxy->state->n_backends);
    return false;
  }
  return true;
} /* channel_register() */

/* ------------------------------------------------------------------------- */

/*
 * Perform delayed deletion of channel
 */
static void
channel_remove (
  Channel *chan
) {
  Assert(chan->is_disconnected); /* should be marked as disconnected by
                                    channel_hangout */

  // JHH FIXME
  //  DeleteWaitEventFromSet(chan->proxy->wait_events, chan->event_pos);
  if (chan->client_port) {
    if (chan->pool)
      chan->pool->n_connected_clients -= 1;
    else
      chan->proxy->n_accepted_connections -= 1;
    chan->proxy->state->n_clients -= 1;
    chan->proxy->state->n_ssl_clients -= chan->client_port->ssl_in_use;
    closesocket(chan->client_port->sock);
    pfree(chan->client_port);
    if (chan->gucs)
      pfree(chan->gucs);
    if (chan->prev_gucs)
      pfree(chan->prev_gucs);
  } else {
    chan->proxy->state->n_backends -= 1;
    chan->pool->n_launched_backends -= 1;
    closesocket(chan->backend_socket);
    pfree(chan->handshake_response);

    if (chan->pool->pending_clients) {
      char *error;
      /* Try to start new backend instead of terminated */
      Channel *new_backend = backend_start(chan->pool, &error);
      if (new_backend != NULL) {
        ELOG(LOG, "Spawn new backend %p instead of terminated %p", new_backend,
             chan);
        backend_reschedule(new_backend, true);
      } else
        free(error);
    }
  }
  chan->magic = REMOVED_CHANNEL_MAGIC;
  pfree(chan->buf);
  pfree(chan);
} /* channel_remove() */

/* ------------------------------------------------------------------------- */

/*
 * Try to send some data to the channel.
 * Data is located in the peer buffer. Because of using edge-triggered mode we
 * have have to use non-blocking IO and try to write all available data. Once
 * write is completed we should try to read more data from source socket.
 * "synchronous" flag is used to avoid infinite recursion or reads-writers.
 * Returns true if there is nothing to do or operation is successfully
 * completed, false in case of error or socket buffer is full.
 */
static bool
channel_write (
  Channel  *chan,
  bool      synchronous
) {
  Channel *peer = chan->peer;
  if (!chan->client_port && chan->is_interrupted) {
    /* Send terminate command to the backend. */
    char const terminate[] = {'X', 0, 0, 0, 4};
    if (socket_write(chan, terminate, sizeof(terminate)) <= 0)
      return false;
    channel_hangout(chan, "terminate");
    return true;
  }
  if (peer == NULL)
    return false;

  while (peer->tx_pos < peer->tx_size) /* has something to write */
  {
    ssize_t rc = socket_write(chan, peer->buf + peer->tx_pos,
                              peer->tx_size - peer->tx_pos);

    ELOG(LOG, "%p: write %d tx_pos=%d, tx_size=%d: %m", chan, (int)rc,
         peer->tx_pos, peer->tx_size);
    if (rc <= 0)
      return false;

    if (!chan->client_port)
      ELOG(LOG, "Send command %c from client %d to backend %d (%p:ready=%d)",
           peer->buf[peer->tx_pos], peer->client_port->sock, chan->backend_pid,
           chan, chan->backend_is_ready);
    else
      ELOG(LOG, "Send reply %c to client %d from backend %d (%p:ready=%d)",
           peer->buf[peer->tx_pos], chan->client_port->sock, peer->backend_pid,
           peer, peer->backend_is_ready);

    if (chan->client_port)
      chan->proxy->state->tx_bytes += rc;
    else
      chan->proxy->state->rx_bytes += rc;
    if (rc > 0 && chan->edge_triggered) {
      /* resume accepting all events */
      ModifyWaitEvent(
          chan->proxy->wait_events, chan->event_pos,
          WL_SOCKET_WRITEABLE | WL_SOCKET_READABLE | WL_SOCKET_EDGE, NULL);
      chan->edge_triggered = false;
    }
    peer->tx_pos += rc;
  }
  if (peer->tx_size != 0) {
    /* Copy rest of received data to the beginning of the buffer */
    chan->backend_is_ready = false;
    Assert(peer->rx_pos >= peer->tx_size);
    memmove(peer->buf, peer->buf + peer->tx_size,
            peer->rx_pos - peer->tx_size);
    peer->rx_pos -= peer->tx_size;
    peer->tx_pos = peer->tx_size = 0;
    if (peer->backend_is_ready) {
      Assert(peer->rx_pos == 0);
      backend_reschedule(peer, false);
      return true;
    }
  }
  return synchronous ||
         channel_read(peer); /* write is not invoked from read */
} /* channel_write() */

/* ------------------------------------------------------------------------- */

/*
 * Attach client to backend. Return true if new backend is attached, false
 * otherwise.
 */
static bool
client_attach (
  Channel *chan
) {
  Channel *idle_backend = chan->pool->idle_backends;
  chan->is_idle = false;
  chan->pool->n_idle_clients -= 1;
  chan->pool->proxy->state->n_idle_clients -= 1;
  if (idle_backend) {
    /* has some idle backend */
    Assert(!idle_backend->client_port);
    Assert(chan != idle_backend);
    chan->peer = idle_backend;
    idle_backend->peer = chan;
    chan->pool->idle_backends = idle_backend->next;
    chan->pool->n_idle_backends -= 1;
    chan->pool->proxy->state->n_idle_backends -= 1;
    idle_backend->is_idle = false;
    if (IdlePoolWorkerTimeout)
      chan->backend_last_activity = GetCurrentTimestamp();
    ELOG(LOG, "Attach client %p to backend %p (pid %d)", chan, idle_backend,
         idle_backend->backend_pid);
  } else /* all backends are busy */
  {
    if (chan->pool->n_launched_backends < chan->proxy->max_backends) {
      char *error;
      /* Try to start new backend */
      idle_backend = backend_start(chan->pool, &error);
      if (idle_backend != NULL) {
        ELOG(LOG, "Start new backend %p (pid %d) for client %p", idle_backend,
             idle_backend->backend_pid, chan);
        Assert(chan != idle_backend);
        chan->peer = idle_backend;
        idle_backend->peer = chan;
        if (IdlePoolWorkerTimeout)
          idle_backend->backend_last_activity = GetCurrentTimestamp();
        return true;
      } else {
        if (error) {
          report_error_to_client(chan, error);
          free(error);
        }
        channel_hangout(chan, "connect");
        return false;
      }
    }
    /* Postpone handshake until some backend is available */
    ELOG(LOG, "Client %p is waiting for available backends", chan);
    chan->next = chan->pool->pending_clients;
    chan->pool->pending_clients = chan;
    chan->pool->n_pending_clients += 1;
  }
  return false;
} /* client_attach() */

/* ------------------------------------------------------------------------- */

/**
 * Parse client's startup packet and assign client to proper connection pool
 * based on dbname/role
 */
static bool
client_connect (
  Channel    *chan,
  int         startup_packet_size
) {
  bool found;
  SessionPoolKey key;
  char *startup_packet = chan->buf;
  MemoryContext proxy_ctx;

  Assert(chan->client_port);

  /* parse startup packet in parse_ctx memory context and reset it when it is
   * not needed any more */
  MemoryContextReset(chan->proxy->parse_ctx);
  proxy_ctx = MemoryContextSwitchTo(chan->proxy->parse_ctx);

  /* Associate libpq with client's port */
  MyProcPort = chan->client_port;
  ng_idcp_pq_init();

  if (ng_idcp_parse_startup_packet(chan->client_port, chan->proxy->parse_ctx,
                         startup_packet + 4, startup_packet_size - 4, false,
                         false) != STATUS_OK) /* skip packet size */
  {
    MyProcPort = NULL;
    MemoryContextSwitchTo(proxy_ctx);
    elog(WARNING, "Failed to parse startup packet for client %p", chan);
    return false;
  }
  MyProcPort = NULL;
  MemoryContextSwitchTo(proxy_ctx);
  if (am_walsender) {
    elog(WARNING, "WAL sender should not be connected through proxy");
    return false;
  }

  chan->proxy->state->n_ssl_clients += chan->client_port->ssl_in_use;
  pg_set_noblock(
      chan->client_port
          ->sock); /* SSL handshake may switch socket to blocking mode */
  memset(&key, 0, sizeof(key));
  strlcpy(key.database, chan->client_port->database_name, NAMEDATALEN);
  if (MultitenantProxy)
    chan->gucs = psprintf("set local role %s;", chan->client_port->user_name);
  else
    strlcpy(key.username, chan->client_port->user_name, NAMEDATALEN);

  ELOG(LOG, "Client %p connects to %s/%s", chan, key.database, key.username);

  chan->pool =
      (SessionPool *)hash_search(chan->proxy->pools, &key, HASH_ENTER, &found);
  if (!found) {
    /* First connection to this role/dbname */
    chan->proxy->state->n_pools += 1;
    chan->pool->startup_gucs = NULL;
    chan->pool->cmdline_options = NULL;
    memset((char *)chan->pool + sizeof(SessionPoolKey), 0,
           sizeof(SessionPool) - sizeof(SessionPoolKey));
  }
  if (ProxyingGUCs) {
    ListCell *gucopts = list_head(chan->client_port->guc_options);
    while (gucopts) {
      char *name;
      char *value;

      name = lfirst(gucopts);
      gucopts = lnext(chan->client_port->guc_options, gucopts);

      value = lfirst(gucopts);
      gucopts = lnext(chan->client_port->guc_options, gucopts);

      chan->gucs = psprintf("%sset local %s='%s';",
                            chan->gucs ? chan->gucs : "", name, value);
    }
  } else {
    /* Assume that all clients are using the same set of GUCs.
     * Use them for launching pooler worker backends and report error
     * if GUCs in startup packets are different.
     */
    if (chan->pool->n_launched_backends == chan->pool->n_dedicated_backends) {
      list_free(chan->pool->startup_gucs);
      if (chan->pool->cmdline_options)
        pfree(chan->pool->cmdline_options);

      chan->pool->startup_gucs =
          string_list_copy(chan->client_port->guc_options);
      if (chan->client_port->cmdline_options)
        chan->pool->cmdline_options =
            pstrdup(chan->client_port->cmdline_options);
    } else {
      if (!string_list_equal(chan->pool->startup_gucs,
                             chan->client_port->guc_options) ||
          !string_equal(chan->pool->cmdline_options,
                        chan->client_port->cmdline_options)) {
        elog(LOG, "Ignoring startup GUCs of client %s",
             NULLSTR(chan->client_port->application_name));
      }
    }
  }
  chan->pool->proxy = chan->proxy;
  chan->pool->n_connected_clients += 1;
  chan->proxy->n_accepted_connections -= 1;
  chan->pool->n_idle_clients += 1;
  chan->pool->proxy->state->n_idle_clients += 1;
  chan->is_idle = true;
  return true;
} /* client_connect() */

/* ------------------------------------------------------------------------- */

static bool
is_transactional_statement (
  char *stmt
) {
  static char const *const non_tx_stmts[] = {
    "create tablespace",
    "create database",
    "cluster",
    "drop",
    "discard",
    "reindex",
    "rollback",
    "vacuum",
    NULL
  };
  int i;
  for (i = 0; non_tx_stmts[i]; i++) {
    if (pg_strncasecmp(stmt, non_tx_stmts[i], strlen(non_tx_stmts[i])) == 0) {
      return false;
    }
  }
  return true;
} /* is_transactional_statement() */

/* ------------------------------------------------------------------------- */

static bool
is_transaction_start (
  char *stmt
) {
  return pg_strncasecmp(stmt, "begin", 5) == 0 ||
         pg_strncasecmp(stmt, "start", 5) == 0;
} /* is_transaction_start() */

/* ------------------------------------------------------------------------- */

static void *
libpq_connectdb (
  char const   *keywords[],
  char const   *values[],
  char        **error
) {
  PGconn *conn = PQconnectdbParams(keywords, values, false);
  if (conn && PQstatus(conn) != CONNECTION_OK) {
    ereport(WARNING,
            (errcode(ERRCODE_SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION),
             errmsg("could not setup local connect to server"),
             errdetail_internal("%s", pchomp(PQerrorMessage(conn)))));
    *error = strdup(PQerrorMessage(conn));
    PQfinish(conn);
    return NULL;
  }
  *error = NULL;
  return conn;
} /* libpq_connectdb() */

/* ------------------------------------------------------------------------- */

/*
 * Add new client accepted by postmaster. This client will be assigned to
 * concrete session pool when it's startup packet is received.
 */
static void
proxy_add_client (
  Proxy    *proxy,
  Port     *port
) {
  Channel *chan = channel_create(proxy);
  chan->client_port = port;
  chan->backend_socket = PGINVALID_SOCKET;
  if (channel_register(proxy, chan)) {
    ELOG(LOG, "Add new client %p", chan);
    proxy->n_accepted_connections += 1;
    proxy->state->n_clients += 1;
  } else {
    report_error_to_client(chan, "Too much sessions. Try to increase "
                                 "'max_sessions' configuration parameter");
    /* Too much sessions, error report was already logged */
    closesocket(port->sock);
#if defined(ENABLE_GSS) || defined(ENABLE_SSPI)
    pfree(port->gss);
#endif
    chan->magic = REMOVED_CHANNEL_MAGIC;
    pfree(port);
    pfree(chan->buf);
    pfree(chan);
  }
} /* proxy_add_client() */

/* ------------------------------------------------------------------------- */

static void
proxy_add_listen_socket (
  Proxy        *proxy,
  pgsocket      socket
) {
  AddWaitEventToSet(proxy->wait_events, WL_SOCKET_ACCEPT, socket, NULL, NULL);
} /* proxy_add_listen_socket() */

/* ------------------------------------------------------------------------- */

static Proxy *
proxy_create (
  ConnectionProxyState *state,
  int                   max_backends
) {
  HASHCTL ctl;
  Proxy *proxy;
  MemoryContext proxy_memctx =
      AllocSetContextCreate(TopMemoryContext, "Proxy", ALLOCSET_DEFAULT_SIZES);
  MemoryContextSwitchTo(proxy_memctx);
  proxy = palloc0(sizeof(Proxy));
  proxy->parse_ctx = AllocSetContextCreate(
      proxy_memctx, "Startup packet parsing context", ALLOCSET_DEFAULT_SIZES);
  MemSet(&ctl, 0, sizeof(ctl));
  ctl.keysize = sizeof(SessionPoolKey);
  ctl.entrysize = sizeof(SessionPool);
  ctl.hcxt = proxy_memctx;
  proxy->pools = hash_create("Pool by database and user", DB_HASH_SIZE, &ctl,
                             HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

  /*
   * We need events both for clients and backends so multiply MaxConnection by
   * two
   */
  proxy->wait_events = CreateWaitEventSet(TopMemoryContext, MaxSessions * 2);
  proxy->max_backends = max_backends;
  proxy->state = state;
  return proxy;
} /* proxy_create() */

/* ------------------------------------------------------------------------- */

/*
 * Handle normal shutdown of Postgres instance
 */
static void
proxy_handle_sigterm (
  SIGNAL_ARGS
) {
  if (proxy) {
    proxy->shutdown = true;
  }
} /* proxy_handle_sigterm() */

/* ------------------------------------------------------------------------- */

/*
 * Main proxy loop
 */
static void
proxy_loop (
  Proxy *proxy
) {
  int i, n_ready;
  WaitEvent ready[MAX_READY_EVENTS];
  Channel *chan, *next;

  /* Main loop */
  while (!proxy->shutdown) {
    /* Use timeout to allow normal proxy shutdown */
    int wait_timeout =
        IdlePoolWorkerTimeout ? IdlePoolWorkerTimeout : PROXY_WAIT_TIMEOUT;
    n_ready = WaitEventSetWait(proxy->wait_events, wait_timeout, ready,
                               MAX_READY_EVENTS, PG_WAIT_CLIENT);
    for (i = 0; i < n_ready; i++) {
      chan = (Channel *)ready[i].user_data;
      if (chan == NULL) /* new connection from postmaster */
      {
        if (ready[i].events & WL_SOCKET_ACCEPT) {
          Port *port = (Port *)palloc0(sizeof(Port));
          if (ng_idcp_stream_connection(ready[i].fd, port) != STATUS_OK) {
            elog(ERROR, "problem with connection");
            if (port->sock != PGINVALID_SOCKET) {
              StreamClose(port->sock);
            }
            pfree(port);
          }
          proxy_add_client(proxy, port);
        }
      }
      /*
       * epoll may return event for already closed session if
       * socket is still openned. From epoll documentation: Q6
       * Will closing a file descriptor cause it to be removed
       * from all epoll sets automatically?
       *
       * A6  Yes, but be aware of the following point.  A file
       * descriptor is a reference to an open file description
       * (see open(2)).  Whenever a descriptor is duplicated via
       * dup(2), dup2(2), fcntl(2) F_DUPFD, or fork(2), a new
       * file descriptor referring to the same open file
       * description is created.  An open file  description
       * continues  to exist until  all  file  descriptors
       * referring to it have been closed.  A file descriptor is
       * removed from an epoll set only after all the file
       * descriptors referring to the underlying open file
       * description  have been closed  (or  before  if  the
       * descriptor is explicitly removed using epoll_ctl(2)
       * EPOLL_CTL_DEL).  This means that even after a file
       * descriptor that is part of an epoll set has been
       * closed, events may be reported  for that  file
       * descriptor  if  other  file descriptors referring to
       * the same underlying file description remain open.
       *
       * Using this check for valid magic field we try to ignore
       * such events.
       */
      else if (chan->magic == ACTIVE_CHANNEL_MAGIC) {
        if (ready[i].events & WL_SOCKET_WRITEABLE) {
          ELOG(LOG, "Channel %p is writable", chan);
          channel_write(chan, false);
          if (chan->magic == ACTIVE_CHANNEL_MAGIC &&
              (chan->peer == NULL ||
               chan->peer->tx_size == 0)) /* nothing to write */
          {
            /* At systems not supporting epoll edge triggering (Win32, FreeBSD,
             * MacOS), we need to disable writable event to avoid busy loop */
            ModifyWaitEvent(chan->proxy->wait_events, chan->event_pos,
                            WL_SOCKET_READABLE | WL_SOCKET_EDGE, NULL);
            chan->edge_triggered = true;
          }
        }
        if (ready[i].events & WL_SOCKET_READABLE) {
          ELOG(LOG, "Channel %p is readable", chan);
          channel_read(chan);
          if (chan->magic == ACTIVE_CHANNEL_MAGIC &&
              chan->tx_size != 0) /* pending write: read is not prohibited */
          {
            /* At systems not supporting epoll edge triggering (Win32, FreeBSD,
             * MacOS), we need to disable readable event to avoid busy loop */
            ModifyWaitEvent(chan->proxy->wait_events, chan->event_pos,
                            WL_SOCKET_WRITEABLE | WL_SOCKET_EDGE, NULL);
            chan->edge_triggered = true;
          }
        }
      }
    }
    if (IdlePoolWorkerTimeout) {
      TimestampTz now = GetCurrentTimestamp();
      TimestampTz timeout_usec = IdlePoolWorkerTimeout * 1000;
      if (proxy->last_idle_timeout_check + timeout_usec < now) {
        HASH_SEQ_STATUS seq;
        struct SessionPool *pool;
        proxy->last_idle_timeout_check = now;
        hash_seq_init(&seq, proxy->pools);
        while ((pool = hash_seq_search(&seq)) != NULL) {
          for (chan = pool->idle_backends; chan != NULL; chan = chan->next) {
            if (chan->backend_last_activity + timeout_usec < now) {
              chan->is_interrupted =
                  true; /* interrupted flags makes channel_write to send 'X'
                           message */
              channel_write(chan, false);
            }
          }
        }
      }
    }

    /*
     * Delayed deallocation of disconnected channels.
     * We can not delete channels immediately because of presence of peer
     * events.
     */
    for (chan = proxy->hangout; chan != NULL; chan = next) {
      next = chan->next;
      channel_remove(chan);
    }
    proxy->hangout = NULL;
  }
} /* proxy_loop() */

/* ------------------------------------------------------------------------- */

/*
 * Send error message to the client. This function is called when new backend
 * can not be started or client is assigned to the backend because of
 * configuration limitations.
 */
static void
report_error_to_client (
  Channel        *chan,
  char const     *error
) {
  StringInfoData msgbuf;
  initStringInfo(&msgbuf);
  pq_sendbyte(&msgbuf, 'E');
  pq_sendint32(&msgbuf, 7 + strlen(error));
  pq_sendbyte(&msgbuf, PG_DIAG_MESSAGE_PRIMARY);
  pq_sendstring(&msgbuf, error);
  pq_sendbyte(&msgbuf, '\0');
  socket_write(chan, msgbuf.data, msgbuf.len);
  pfree(msgbuf.data);
} /* report_error_to_client() */

/* ------------------------------------------------------------------------- */

/*
 * Try to write data to the socket.
 */
static ssize_t
socket_write (
  Channel        *chan,
  char const     *buf,
  size_t          size
) {
  ssize_t rc;
#ifdef USE_SSL
  int waitfor = 0;
  if (chan->client_port && chan->client_port->ssl_in_use)
    rc = be_tls_write(chan->client_port, (char *)buf, size, &waitfor);
  else
#endif
    rc = chan->client_port ? secure_raw_write(chan->client_port, buf, size)
                           : send(chan->backend_socket, buf, size, 0);
  if (rc == 0 || (rc < 0 && (errno != EAGAIN && errno != EWOULDBLOCK))) {
    channel_hangout(chan, "write");
  }
  return rc;
} /* socket_write() */

/* ------------------------------------------------------------------------- */

static char *
string_append (
  char         *dst,
  char const   *src
) {
  while (*src) {
    if (*src == ' ')
      *dst++ = '\\';
    *dst++ = *src++;
  }
  return dst;
} /* string_append() */

/* ------------------------------------------------------------------------- */

static bool
string_equal (
  char const *a,
  char const *b
) {
  return a == b ? true : a == NULL || b == NULL ? false : strcmp(a, b) == 0;
} /* string_equal() */

/* ------------------------------------------------------------------------- */

static size_t
string_length (
  char const *str
) {
  size_t spaces = 0;
  char const *p = str;
  if (p == NULL)
    return 0;
  while (*p != '\0')
    spaces += (*p++ == ' ');
  return (p - str) + spaces;
} /* string_length() */

/* ------------------------------------------------------------------------- */

static List *
string_list_copy (
  List *orig
) {
  List *copy = list_copy(orig);
  ListCell *cell;
  foreach (cell, copy) {
    lfirst(cell) = pstrdup((char *)lfirst(cell));
  }
  return copy;
} /* string_list_copy() */

/* ------------------------------------------------------------------------- */

static bool
string_list_equal (
  List *a,
  List *b
) {
  const ListCell *ca, *cb;
  if (list_length(a) != list_length(b))
    return false;
  forboth(ca, a, cb, b) if (strcmp(lfirst(ca), lfirst(cb)) != 0) return false;
  return true;
} /* string_list_equal() */

/* ------------------------------------------------------------------------- */

static size_t
string_list_length (
  List *list
) {
  ListCell *cell;
  size_t length = 0;
  foreach (cell, list) {
    length += strlen((char *)lfirst(cell));
  }
  return length;
} /* string_list_length() */

/* vim: set ts=2 et sw=2 ft=c: */

