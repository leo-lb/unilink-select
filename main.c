#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"
#include "unilink.h"

int
net_cb_fn_test(int event, void* event_data, void** p)
{
  (void)event_data;
  (void)p;

  printf("event: ");

  switch (event) {
    case NET_EVENT_ESTABLISHED:
      printf("NET_EVENT_ESTABLISHED - fd: %d",
             ((struct net_event_data_established*)event_data)->tcp_conn->fd);
      break;
    case NET_EVENT_CLOSED:
      printf("NET_EVENT_CLOSED - fd: %d",
             ((struct net_event_data_closed*)event_data)->tcp_conn->fd);
      break;
    case NET_EVENT_SENT:
      printf("NET_EVENT_SENT - fd: %d count: %ld",
             ((struct net_event_data_sent*)event_data)->tcp_conn->fd,
             ((struct net_event_data_sent*)event_data)->count);
      break;
    case NET_EVENT_RECEIVED:
      printf("NET_EVENT_RECEIVED - fd: %d count: %ld size: %ld size_int: %d "
             "data: \"%.*s\"",
             ((struct net_event_data_received*)event_data)->tcp_conn->fd,
             ((struct net_event_data_received*)event_data)->count,
             ((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (int)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (int)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (const char*)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.p);

      mem_free_buf(
        &((struct net_event_data_received*)event_data)->tcp_conn->receive_buf);

      char* to_send = "Hello\n";
      mem_grow_buf(
        &((struct net_event_data_received*)event_data)->tcp_conn->send_buf,
        to_send,
        strlen(to_send));
      break;
  }

  printf("\n");
  return 0;
}

int
main(int argc, char* argv[])
{
  if (argc > 1) {
    unsigned short port = (unsigned short)atoi(argv[1]);

    int socket_ret = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ret == -1) {
      perror("socket");
      return EXIT_FAILURE;
    }

    int tcp_fd = socket_ret;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t salen = sizeof sa;

    int bind_ret = bind(tcp_fd, (struct sockaddr*)&sa, salen);
    if (bind_ret == -1) {
      perror("bind");
      close(tcp_fd);
      return EXIT_FAILURE;
    }

    int listen_ret = listen(tcp_fd, 256);
    if (listen_ret == -1) {
      perror("listen");
      close(tcp_fd);
      return EXIT_FAILURE;
    }

    struct net_context ctx;
    memset(&ctx, 0, sizeof ctx);

    ctx.tcp_boundfds[0] = tcp_fd;
    ctx.tcp_boundfds[1] = -1;
    ctx.tcp_boundfds[2] = -1;
    ctx.tcp_boundfds[3] = -1;

    ctx.udp_boundfds[0] = -1;
    ctx.udp_boundfds[1] = -1;
    ctx.udp_boundfds[2] = -1;
    ctx.udp_boundfds[3] = -1;

    struct sockaddr_in sa2;
    memset(&sa2, 0, sizeof sa2);

    socklen_t sa2len = sizeof sa2;

    int getsockname_ret = getsockname(tcp_fd, (struct sockaddr*)&sa2, &sa2len);
    if (getsockname_ret == -1) {
      perror("getsockname");
      close(tcp_fd);
      return EXIT_FAILURE;
    }

    printf("listening on port %hu\n", ntohs(sa2.sin_port));

    struct net_callback* net_cb = calloc(1, sizeof *net_cb);
    if (net_cb == NULL) {
      perror("calloc");
      close(tcp_fd);
      return EXIT_FAILURE;
    }

    net_cb->events = ~0;
    net_cb->cb = net_cb_fn_test;

    LIST_INSERT_HEAD(&ctx.callbacks, net_cb, entry);

    int nonblock_ret = net_set_nonblock(tcp_fd);
    if (nonblock_ret != NET_SET_NONBLOCK_OK) {
      close(tcp_fd);
      return EXIT_FAILURE;
    }

    net_loop(&ctx);

    free(net_cb);
  }

  return EXIT_SUCCESS;
}