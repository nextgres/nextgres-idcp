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
 * This is basically a verbatim copy of core postmaster functions required for
 * connection startup handling.
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

static void processCancelRequest (Port *port, void *pkt);
static int ProcessStartupPacket (Port *port, bool ssl_done, bool gss_done);
static void SendNegotiateProtocolVersion (List *unrecognized_protocol_options);

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

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

int
ng_idcp_parse_startup_packet (
  Port             *port,
  MemoryContext     memctx,
  char             *buf,
  int               len,
  bool              ssl_done,
  bool              gss_done
) {
  ProtocolVersion proto;
  MemoryContext oldcontext;

  am_walsender = false;
  am_db_walsender = false;

  /*
   * The first field is either a protocol version number or a special
   * request code.
   */
  port->proto = proto = pg_ntoh32(*((ProtocolVersion *)buf));

  if (proto == CANCEL_REQUEST_CODE) {
    if (len != sizeof(CancelRequestPacket)) {
      ereport(COMMERROR, (errcode(ERRCODE_PROTOCOL_VIOLATION),
                          errmsg("invalid length of startup packet")));
      return STATUS_ERROR;
    }
    processCancelRequest(port, buf);
    /* Not really an error, but we don't want to proceed further */
    return STATUS_ERROR;
  }

  if (proto == NEGOTIATE_SSL_CODE && !ssl_done) {
    char SSLok;

#ifdef USE_SSL
    /* No SSL when disabled or on Unix sockets */
    if (!LoadedSSL || port->laddr.addr.ss_family == AF_UNIX)
      SSLok = 'N';
    else
      SSLok = 'S'; /* Support for SSL */
#else
    SSLok = 'N'; /* No support for SSL */
#endif

  retry1:
    if (send(port->sock, &SSLok, 1, 0) != 1) {
      if (errno == EINTR)
        goto retry1; /* if interrupted, just retry */
      ereport(COMMERROR,
              (errcode_for_socket_access(),
               errmsg("failed to send SSL negotiation response: %m")));
      return STATUS_ERROR; /* close the connection */
    }

#ifdef USE_SSL
    if (SSLok == 'S' && secure_open_server(port) == -1)
      return STATUS_ERROR;
#endif

    /*
     * At this point we should have no data already buffered.  If we do,
     * it was received before we performed the SSL handshake, so it wasn't
     * encrypted and indeed may have been injected by a man-in-the-middle.
     * We report this case to the client.
     */
    if (ng_idcp_pq_buffer_has_data())
      ereport(
          FATAL,
          (errcode(ERRCODE_PROTOCOL_VIOLATION),
           errmsg("received unencrypted data after SSL request"),
           errdetail("This could be either a client-software bug or evidence "
                     "of an attempted man-in-the-middle attack.")));

    /*
     * regular startup packet, cancel, etc packet should follow, but not
     * another SSL negotiation request, and a GSS request should only
     * follow if SSL was rejected (client may negotiate in either order)
     */
    return ProcessStartupPacket(port, true, SSLok == 'S');
  } else if (proto == NEGOTIATE_GSS_CODE && !gss_done) {
    char GSSok = 'N';

#ifdef ENABLE_GSS
    /* No GSSAPI encryption when on Unix socket */
    if (port->laddr.addr.ss_family != AF_UNIX)
      GSSok = 'G';
#endif

    while (send(port->sock, &GSSok, 1, 0) != 1) {
      if (errno == EINTR)
        continue;
      ereport(COMMERROR,
              (errcode_for_socket_access(),
               errmsg("failed to send GSSAPI negotiation response: %m")));
      return STATUS_ERROR; /* close the connection */
    }

#ifdef ENABLE_GSS
    if (GSSok == 'G' && secure_open_gssapi(port) == -1)
      return STATUS_ERROR;
#endif

    /*
     * At this point we should have no data already buffered.  If we do,
     * it was received before we performed the GSS handshake, so it wasn't
     * encrypted and indeed may have been injected by a man-in-the-middle.
     * We report this case to the client.
     */
    if (ng_idcp_pq_buffer_has_data())
      ereport(
          FATAL,
          (errcode(ERRCODE_PROTOCOL_VIOLATION),
           errmsg("received unencrypted data after GSSAPI encryption request"),
           errdetail("This could be either a client-software bug or evidence "
                     "of an attempted man-in-the-middle attack.")));

    /*
     * regular startup packet, cancel, etc packet should follow, but not
     * another GSS negotiation request, and an SSL request should only
     * follow if GSS was rejected (client may negotiate in either order)
     */
    return ProcessStartupPacket(port, GSSok == 'G', true);
  }

  /* Could add additional special packet types here */

  /*
   * Set FrontendProtocol now so that ereport() knows what format to send if
   * we fail during startup.
   */
  FrontendProtocol = proto;

  /* Check that the major protocol version is in range. */
  if (PG_PROTOCOL_MAJOR(proto) < PG_PROTOCOL_MAJOR(PG_PROTOCOL_EARLIEST) ||
      PG_PROTOCOL_MAJOR(proto) > PG_PROTOCOL_MAJOR(PG_PROTOCOL_LATEST))
    ereport(FATAL, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("unsupported frontend protocol %u.%u: server "
                           "supports %u.0 to %u.%u",
                           PG_PROTOCOL_MAJOR(proto), PG_PROTOCOL_MINOR(proto),
                           PG_PROTOCOL_MAJOR(PG_PROTOCOL_EARLIEST),
                           PG_PROTOCOL_MAJOR(PG_PROTOCOL_LATEST),
                           PG_PROTOCOL_MINOR(PG_PROTOCOL_LATEST))));

  /*
   * Now fetch parameters out of startup packet and save them into the Port
   * structure.  All data structures attached to the Port struct must be
   * allocated in TopMemoryContext so that they will remain available in a
   * running backend (even after PostmasterContext is destroyed).  We need
   * not worry about leaking this storage on failure, since we aren't in the
   * postmaster process anymore.
   */
  oldcontext = MemoryContextSwitchTo(TopMemoryContext);

  /* Handle protocol version 3 startup packet */
  {
    int32 offset = sizeof(ProtocolVersion);
    List *unrecognized_protocol_options = NIL;

    /*
     * Scan packet body for name/option pairs.  We can assume any string
     * beginning within the packet body is null-terminated, thanks to
     * zeroing extra byte above.
     */
    port->guc_options = NIL;

    while (offset < len) {
      char *nameptr = buf + offset;
      int32 valoffset;
      char *valptr;

      if (*nameptr == '\0')
        break; /* found packet terminator */
      valoffset = offset + strlen(nameptr) + 1;
      if (valoffset >= len)
        break; /* missing value, will complain below */
      valptr = buf + valoffset;

      if (strcmp(nameptr, "database") == 0)
        port->database_name = pstrdup(valptr);
      else if (strcmp(nameptr, "user") == 0)
        port->user_name = pstrdup(valptr);
      else if (strcmp(nameptr, "options") == 0)
        port->cmdline_options = pstrdup(valptr);
      else if (strcmp(nameptr, "replication") == 0) {
        /*
         * Due to backward compatibility concerns the replication
         * parameter is a hybrid beast which allows the value to be
         * either boolean or the string 'database'. The latter
         * connects to a specific database which is e.g. required for
         * logical decoding while.
         */
        if (strcmp(valptr, "database") == 0) {
          am_walsender = true;
          am_db_walsender = true;
        } else if (!parse_bool(valptr, &am_walsender))
          ereport(FATAL, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                          errmsg("invalid value for parameter \"%s\": \"%s\"",
                                 "replication", valptr),
                          errhint("Valid values are: \"false\", 0, \"true\", "
                                  "1, \"database\".")));
      } else if (strncmp(nameptr, "_pq_.", 5) == 0) {
        /*
         * Any option beginning with _pq_. is reserved for use as a
         * protocol-level option, but at present no such options are
         * defined.
         */
        unrecognized_protocol_options =
            lappend(unrecognized_protocol_options, pstrdup(nameptr));
      } else {
        /* Assume it's a generic GUC option */
        port->guc_options = lappend(port->guc_options, pstrdup(nameptr));
        port->guc_options = lappend(port->guc_options, pstrdup(valptr));

        /*
         * Copy application_name to port if we come across it.  This
         * is done so we can log the application_name in the
         * connection authorization message.  Note that the GUC would
         * be used but we haven't gone through GUC setup yet.
         */
        if (strcmp(nameptr, "application_name") == 0) {
          port->application_name = pg_clean_ascii(valptr, 0);
        }
      }
      offset = valoffset + strlen(valptr) + 1;
    }

    /*
     * If we didn't find a packet terminator exactly at the end of the
     * given packet length, complain.
     */
    if (offset != len - 1)
      ereport(FATAL, (errcode(ERRCODE_PROTOCOL_VIOLATION),
                      errmsg("invalid startup packet layout: expected "
                             "terminator as last byte")));

    /*
     * If the client requested a newer protocol version or if the client
     * requested any protocol options we didn't recognize, let them know
     * the newest minor protocol version we do support and the names of
     * any unrecognized options.
     */
    if (PG_PROTOCOL_MINOR(proto) > PG_PROTOCOL_MINOR(PG_PROTOCOL_LATEST) ||
        unrecognized_protocol_options != NIL)
      SendNegotiateProtocolVersion(unrecognized_protocol_options);
  }

  /* Check a user name was given. */
  if (port->user_name == NULL || port->user_name[0] == '\0')
    ereport(FATAL,
            (errcode(ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION),
             errmsg("no PostgreSQL user name specified in startup packet")));

  /* The database defaults to the user name. */
  if (port->database_name == NULL || port->database_name[0] == '\0')
    port->database_name = pstrdup(port->user_name);

  if (Db_user_namespace) {
    /*
     * If user@, it is a global user, remove '@'. We only want to do this
     * if there is an '@' at the end and no earlier in the user string or
     * they may fake as a local user of another database attaching to this
     * database.
     */
    if (strchr(port->user_name, '@') ==
        port->user_name + strlen(port->user_name) - 1)
      *strchr(port->user_name, '@') = '\0';
    else {
      /* Append '@' and dbname */
      port->user_name =
          psprintf("%s@%s", port->user_name, port->database_name);
    }
  }

  /*
   * Truncate given database and user names to length of a Postgres name.
   * This avoids lookup failures when overlength names are given.
   */
  if (strlen(port->database_name) >= NAMEDATALEN)
    port->database_name[NAMEDATALEN - 1] = '\0';
  if (strlen(port->user_name) >= NAMEDATALEN)
    port->user_name[NAMEDATALEN - 1] = '\0';

  if (am_walsender)
    MyBackendType = B_WAL_SENDER;
  else
    MyBackendType = B_BACKEND;

  /*
   * Normal walsender backends, e.g. for streaming replication, are not
   * connected to a particular database. But walsenders used for logical
   * replication need to connect to a specific database. We allow streaming
   * replication commands to be issued even if connected to a database as it
   * can make sense to first make a basebackup and then stream changes
   * starting from that.
   */
  if (am_walsender && !am_db_walsender)
    port->database_name[0] = '\0';

  /*
   * Done putting stuff in TopMemoryContext.
   */
  MemoryContextSwitchTo(oldcontext);

  /*
   * If we're going to reject the connection due to database state, say so
   * now instead of wasting cycles on an authentication exchange. (This also
   * allows a pg_ping utility to be written.)
   */
  switch (port->canAcceptConnections) {
  case CAC_STARTUP:
    ereport(FATAL, (errcode(ERRCODE_CANNOT_CONNECT_NOW),
                    errmsg("the database system is starting up")));
    break;
  case CAC_NOTCONSISTENT:
    if (EnableHotStandby)
      ereport(
          FATAL,
          (errcode(ERRCODE_CANNOT_CONNECT_NOW),
           errmsg("the database system is not yet accepting connections"),
           errdetail("Consistent recovery state has not been yet reached.")));
    else
      ereport(FATAL,
              (errcode(ERRCODE_CANNOT_CONNECT_NOW),
               errmsg("the database system is not accepting connections"),
               errdetail("Hot standby mode is disabled.")));
    break;
  case CAC_SHUTDOWN:
    ereport(FATAL, (errcode(ERRCODE_CANNOT_CONNECT_NOW),
                    errmsg("the database system is shutting down")));
    break;
  case CAC_RECOVERY:
    ereport(FATAL, (errcode(ERRCODE_CANNOT_CONNECT_NOW),
                    errmsg("the database system is in recovery mode")));
    break;
  case CAC_TOOMANY:
    ereport(FATAL, (errcode(ERRCODE_TOO_MANY_CONNECTIONS),
                    errmsg("sorry, too many clients already")));
    break;
  case CAC_OK:
    break;
  }

  return STATUS_OK;
} /* ng_idcp_parse_startup_packet() */

/* ========================================================================= */
/* -- PRIVATE FUNCTION DEFINITIONS ----------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION DEFINITIONS ------------------------------------------- */
/* ========================================================================= */

/*
 * The client has sent a cancel request packet, not a normal
 * start-a-new-connection packet.  Perform the necessary processing.
 * Nothing is sent back to the client.
 */
static void
processCancelRequest (
  Port *port,
  void *pkt
) {
#if 0
  CancelRequestPacket *canc = (CancelRequestPacket *) pkt;
  int      backendPID;
  int32    cancelAuthCode;
  Backend    *bp;

#ifndef EXEC_BACKEND
  dlist_iter  iter;
#else
  int      i;
#endif

  backendPID = (int) pg_ntoh32(canc->backendPID);
  cancelAuthCode = (int32) pg_ntoh32(canc->cancelAuthCode);

  /*
   * See if we have a matching backend.  In the EXEC_BACKEND case, we can no
   * longer access the postmaster's own backend list, and must rely on the
   * duplicate array in shared memory.
   */
#ifndef EXEC_BACKEND
  dlist_foreach(iter, &BackendList)
  {
    bp = dlist_container(Backend, elem, iter.cur);
#else
  for (i = MaxLivePostmasterChildren() - 1; i >= 0; i--)
  {
    bp = (Backend *) &ShmemBackendArray[i];
#endif
    if (bp->pid == backendPID)
    {
      if (bp->cancel_key == cancelAuthCode)
      {
        /* Found a match; signal that backend to cancel current op */
        ereport(DEBUG2,
            (errmsg_internal("processing cancel request: sending SIGINT to process %d",
                     backendPID)));
        signal_child(bp->pid, SIGINT);
      }
      else
        /* Right PID, wrong key: no way, Jose */
        ereport(LOG,
            (errmsg("wrong key in cancel request for process %d",
                backendPID)));
      return;
    }
#ifndef EXEC_BACKEND /* make GNU Emacs 26.1 see brace balance */
  }
#else
  }
#endif

  /* No matching backend */
  ereport(LOG,
      (errmsg("PID %d in cancel request did not match any process",
          backendPID)));
#endif
} /* func() */

/* ------------------------------------------------------------------------- */

static int
ProcessStartupPacket (
  Port *port,
  bool  ssl_done,
  bool  gss_done
) {
  int32 len;
  char *buf;

  ng_idcp_pq_startmsgread();

  /*
   * Grab the first byte of the length word separately, so that we can tell
   * whether we have no data at all or an incomplete packet.  (This might
   * sound inefficient, but it's not really, because of buffering in
   * pqcomm.c.)
   */
  if (ng_idcp_pq_getbytes((char *)&len, 1) == EOF) {
    /*
     * If we get no data at all, don't clutter the log with a complaint;
     * such cases often occur for legitimate reasons.  An example is that
     * we might be here after responding to NEGOTIATE_SSL_CODE, and if the
     * client didn't like our response, it'll probably just drop the
     * connection.  Service-monitoring software also often just opens and
     * closes a connection without sending anything.  (So do port
     * scanners, which may be less benign, but it's not really our job to
     * notice those.)
     */
    return STATUS_ERROR;
  }

  if (ng_idcp_pq_getbytes(((char *)&len) + 1, 3) == EOF) {
    /* Got a partial length word, so bleat about that */
    if (!ssl_done && !gss_done)
      ereport(COMMERROR, (errcode(ERRCODE_PROTOCOL_VIOLATION),
                          errmsg("incomplete startup packet")));
    return STATUS_ERROR;
  }

  len = pg_ntoh32(len);
  len -= 4;

  if (len < (int32)sizeof(ProtocolVersion) ||
      len > MAX_STARTUP_PACKET_LENGTH) {
    ereport(COMMERROR, (errcode(ERRCODE_PROTOCOL_VIOLATION),
                        errmsg("invalid length of startup packet")));
    return STATUS_ERROR;
  }

  /*
   * Allocate space to hold the startup packet, plus one extra byte that's
   * initialized to be zero.  This ensures we will have null termination of
   * all strings inside the packet.
   */
  buf = palloc(len + 1);
  buf[len] = '\0';

  if (ng_idcp_pq_getbytes(buf, len) == EOF) {
    ereport(COMMERROR, (errcode(ERRCODE_PROTOCOL_VIOLATION),
                        errmsg("incomplete startup packet")));
    return STATUS_ERROR;
  }
  ng_idcp_pq_endmsgread();

  return ng_idcp_parse_startup_packet(port, TopMemoryContext, buf, len,
    ssl_done, gss_done);
} /* ProcessStartupPacket() */

/* ------------------------------------------------------------------------- */

void
SendNegotiateProtocolVersion (
  List *unrecognized_protocol_options
) {
  StringInfoData buf;
  ListCell *lc;

  pq_beginmessage(&buf, 'v'); /* NegotiateProtocolVersion */
  pq_sendint32(&buf, PG_PROTOCOL_LATEST);
  pq_sendint32(&buf, list_length(unrecognized_protocol_options));
  foreach (lc, unrecognized_protocol_options) {
    pq_sendstring(&buf, lfirst(lc));
  }
  pq_endmessage(&buf);

  /* no need to flush, some other message will follow */

} /* SendNegotiateProtocolVersion() */

/* vim: set ts=2 et sw=2 ft=c: */

