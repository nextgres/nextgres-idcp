#ifndef NG_IDCP_LIBPQ_LIBPQ_H                    /* Multiple Inclusion Guard */
#define NG_IDCP_LIBPQ_LIBPQ_H
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

/* ========================================================================= */
/* -- PUBLIC MACROS -------------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC TYPEDEFS ------------------------------------------------------ */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC STRUCTURES ---------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC VARIABLES ----------------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC FUNCTION PROTOTYPES ------------------------------------------- */
/* ========================================================================= */

extern int ng_idcp_stream_server_port (int family, const char *hostName,
  unsigned short portNumber, const char *unixSocketDir,
  pgsocket ListenSocket[], int MaxListen);
int ng_idcp_stream_connection (pgsocket server_fd, Port *port);
extern int StreamConnection(pgsocket server_fd, Port *port);
extern void StreamClose(pgsocket sock);
extern void TouchSocketFiles(void);
extern void RemoveSocketFiles(void);
extern void ng_idcp_pq_init(void);
extern int ng_idcp_pq_getbytes(char *s, size_t len);
extern void ng_idcp_pq_startmsgread(void);
extern void ng_idcp_pq_endmsgread(void);
extern bool ng_idcp_pq_is_reading_msg(void);
extern int ng_idcp_pq_getmessage(StringInfo s, int maxlen);
extern int ng_idcp_pq_getbyte(void);
extern int ng_idcp_pq_peekbyte(void);
extern int ng_idcp_pq_getbyte_if_available(unsigned char *c);
extern bool ng_idcp_pq_buffer_has_data(void);
extern int ng_idcp_pq_putmessage_v2(char msgtype, const char *s, size_t len);
extern bool ng_idcp_pq_check_connection(void);

/* ========================================================================= */
/* -- PUBLIC INLINE FUNCTIONS ---------------------------------------------- */
/* ========================================================================= */

/* ========================================================================= */
/* -- PUBLIC HELPER FUNCTIONS ---------------------------------------------- */
/* ========================================================================= */


/* vim: set ts=2 et sw=2 ft=c: */

#endif /* NG_IDCP_LIBPQ_LIBPQ_H */
