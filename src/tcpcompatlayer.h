#ifndef tcpcompatlayer_h_INCLUDED
#define tcpcompatlayer_h_INCLUDED

#include "tcpcompatlayer_error.h"
#include <stddef.h>
#include <stdint.h>

typedef struct tcp_socket TcpSocket;

// global initialize and uninitialize hooks
TcpError tcp_init_portable(void);
void tcp_cleanup_portable(void);

TcpError tcp_connect_portable(TcpSocket **out, const char *hostname, const char *port);
void tcp_close_portable(TcpSocket *socket);

TcpError tcp_recv_portable(TcpSocket *socket, size_t *recvd, uint8_t *buf, size_t buflen);
TcpError tcp_send_portable(TcpSocket *socket, size_t *sent, const uint8_t *buf, size_t buflen);

#endif // tcpcompatlayer_h_INCLUDED
