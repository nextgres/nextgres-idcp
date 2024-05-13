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
 * This file represents a background worker that coordinates individual
 * connection pool listeners responsible for handling incoming requests.
 */

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* -------------------------- Interface Inclusions ------------------------- */

#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "access/xact.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"

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

static void idcp_controller_sighup_handler (SIGNAL_ARGS);
static void idcp_controller_sigterm_handler (SIGNAL_ARGS);

PGDLLEXPORT void ng_idcp_controller_main(Datum main_arg)
    pg_attribute_noreturn();

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE VARIABLES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL VARIABLES ------------------------------------------------------ */
/* ========================================================================= */

/** Flag indicating the coordinator should be reloaded. */
static volatile sig_atomic_t got_sighup = true;

/** Flag indicating the coordinator should be terminated. */
static volatile sig_atomic_t got_sigterm = false;

/* ========================================================================= */
/* -- STATIC ASSERTIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC FUNCTION DEFINITIONS ------------------------------------------ */
/* ========================================================================= */

void
ng_idcp_controller_main (
  Datum db_oid
) {
  bool proxy_workers_started = false;

  /* Register functions for SIGTERM/SIGHUP management */
  pqsignal(SIGHUP, idcp_controller_sighup_handler);
  pqsignal(SIGTERM, idcp_controller_sigterm_handler);

  /* We're now ready to receive signals */
  BackgroundWorkerUnblockSignals();

  /* Connect to our database */
  BackgroundWorkerInitializeConnectionByOid(db_oid, InvalidOid, 0);

  while (!got_sigterm) {
    while (got_sighup) {
      got_sighup = false;
      (void)WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
                      1000L, PG_WAIT_EXTENSION);
      ResetLatch(MyLatch);
    }

    /* Run the background process main loop interrupt handler */
    HandleMainLoopInterrupts();

    /* Signal our workers */
    if (proxy_workers_started) {
    }

    /* Wait on worker responses */
    if (proxy_workers_started) {
    }

    if (!proxy_workers_started) {
      BackgroundWorker worker = {
        .bgw_name = NEXTGRES_EXTNAME "_worker",
        .bgw_type = NEXTGRES_EXTNAME,
        .bgw_function_name = "ng_idcp_proxy_main",
        .bgw_notify_pid = MyProcPid,
        .bgw_main_arg = db_oid,
        .bgw_restart_time = BGW_NEVER_RESTART,
        .bgw_flags =
            BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
        .bgw_start_time = BgWorkerStart_RecoveryFinished};
      strncpy(worker.bgw_library_name, MyBgworkerEntry->bgw_library_name,
        BGW_MAXLEN - 1);
      for (int ii = 0; ii < g_ng_idcp_cfg_thread_count; ++ii) {
        BackgroundWorkerHandle *handle;
        pid_t worker_pid;

        RegisterDynamicBackgroundWorker(&worker, &handle);
        if (WaitForBackgroundWorkerStartup(handle, &worker_pid) ==
            BGWH_POSTMASTER_DIED) {
          goto main_loop_exit;
        }

        /* We should put these puppies into a list for later signal handling */
      }

      /*
       * We should check to make sure all started, but they always do in our
       * evaluation, so this is a later addition.
       */

      proxy_workers_started = true;
    }

    (void)WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
                    1000L, PG_WAIT_EXTENSION);
    ResetLatch(MyLatch);
  }

main_loop_exit:
  if (proxy_workers_started) {
    ereport(LOG, errmsg("Shutting down proxy workers"));

    /*
     * Per earlier comment, we should iterate the dynamic bgworker list and
     * signal each one accordingly...
     */
  }

  ereport(LOG, errmsg("Exiting " NEXTGRES_EXTNAME));

  proc_exit(0);
} /* ng_idcp_controller_main() */

/* ========================================================================= */
/* -- PRIVATE FUNCTION DEFINITIONS ----------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- LOCAL FUNCTION DEFINITIONS ------------------------------------------- */
/* ========================================================================= */

static void
idcp_controller_sighup_handler (
  SIGNAL_ARGS
) {
  elog(LOG, "received SIGHUP");

  got_sighup = true;

  if (MyProc) {
    SetLatch(&MyProc->procLatch);
  }

} /* idcp_controller_sighup_handler() */

/* ------------------------------------------------------------------------- */

static void
idcp_controller_sigterm_handler (
  SIGNAL_ARGS
) {
  int save_errno = errno;

  got_sigterm = true;

  if (MyProc) {
    SetLatch(&MyProc->procLatch);
  }

  errno = save_errno;

} /* idcp_controller_sigterm_handler() */

/* vim: set ts=2 et sw=2 ft=c: */

