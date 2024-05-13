#include "postgres.h"
#include "port.h"

#ifdef WIN32
int pgwin32_socketpair(int domain, int type, int protocol, SOCKET socks[2])
{
  union {
     struct sockaddr_in inaddr;
     struct sockaddr addr;
  } a;
  SOCKET listener;
  int e;
  socklen_t addrlen = sizeof(a.inaddr);
  DWORD flags = 0;
  int reuse = 1;

  socks[0] = socks[1] = -1;

  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == -1)
    return SOCKET_ERROR;

  memset(&a, 0, sizeof(a));
  a.inaddr.sin_family = AF_INET;
  a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.inaddr.sin_port = 0;

  for (;;) {
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
         (char*) &reuse, (socklen_t) sizeof(reuse)) == -1)
      break;
    if  (bind(listener, &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
      break;

    memset(&a, 0, sizeof(a));
    if  (getsockname(listener, &a.addr, &addrlen) == SOCKET_ERROR)
      break;
    a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.inaddr.sin_family = AF_INET;

    if (listen(listener, 1) == SOCKET_ERROR)
      break;

    socks[0] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, flags);
    if (socks[0] == -1)
      break;
    if (connect(socks[0], &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
      break;

    socks[1] = accept(listener, NULL, NULL);
    if (socks[1] == -1)
      break;

    closesocket(listener);
    return 0;
  }

  e = WSAGetLastError();
  closesocket(listener);
  closesocket(socks[0]);
  closesocket(socks[1]);
  WSASetLastError(e);
  socks[0] = socks[1] = -1;
  return SOCKET_ERROR;
}
#endif /* WIN32 */

