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
 * This file represents the entrypoint of our extension, spawning a background
 * worker that coordinates individual connection pool listeners responsible for
 * handling incoming requests.
 */

/* ========================================================================= */
/* -- INCLUSIONS ----------------------------------------------------------- */
/* ========================================================================= */

/* -------------------------- Interface Inclusions ------------------------- */
#include "postgres.h"

/* These are always necessary for a bgworker */
#include "miscadmin.h"
#include "pgstat.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* --------------------------- System Inclusions --------------------------- */

/* --------------------------- Project Inclusions -------------------------- */

#include "nextgres/idcp.h"                      /* Our base extension header */
#include "nextgres/idcp/util/guc.h"          /* Our extension's GUC handling */

/* ========================================================================= */
/* -- PRIVATE DEFINITIONS -------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE MACROS ------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE TYPEDEFS ----------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE STRUCTURES --------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- INTERNAL FUNCTION PROTOTYPES ----------------------------------------- */
/* ========================================================================= */

void _PG_init(void);

/* ========================================================================= */
/* -- STATIC FUNCTION PROTOTYPES ------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PRIVATE DATA --------------------------------------------------------- */
/* ========================================================================= */

/* We define our module's magic in the entrypoint, rather than globals. */
PG_MODULE_MAGIC;

/* ========================================================================= */
/* -- EXPORTED DATA -------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- STATIC ASSERTIONS ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- EXPORTED FUNCTION DEFINITIONS ---------------------------------------- */
/* ========================================================================= */

void
_PG_init (
  void
) {
  BackgroundWorker ng_idcp_controller_bgworker = {
    .bgw_name = NEXTGRES_EXTNAME "_controller",
    .bgw_type = NEXTGRES_EXTNAME,
    .bgw_library_name = NEXTGRES_LIBNAME,
    .bgw_function_name = "ng_idcp_controller_main",
    .bgw_restart_time = BGW_NEVER_RESTART,
    .bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
    .bgw_main_arg = (Datum) 0,
    .bgw_notify_pid = 0,
    .bgw_start_time = BgWorkerStart_RecoveryFinished
  };

  /* Make sure we're in the right state. */
  if (!process_shared_preload_libraries_in_progress) {
    return;
  }

  /* Define our extension's custom GUC variables. */
  ng_idcp_guc_define();

  /* Since we're disabled by default, inform the user. */
  if (0 >= g_ng_idcp_cfg_thread_count) {
    ereport(WARNING,
        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
         errmsg("In-database connection pool loaded, but not enabled."),
         errhint("You need to set nextgres_idcp.thread_count > 0.")));

    return;
  }

  /* Register the Background Worker */
  RegisterBackgroundWorker(&ng_idcp_controller_bgworker);

} /* _PG_init() */

/* ========================================================================= */
/* -- INTERNAL FUNCTION DEFINITIONS ---------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- STATIC FUNCTION DEFINITIONS ------------------------------------------ */
/* ========================================================================= */

/* :vi set ts=2 et sw=2: */

