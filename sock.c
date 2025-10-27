#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "debug.h"
#include "sock.h"

ssize_t sock_read(int sock_fd, void *buffer, size_t len) {
  ssize_t nr, tot_read;
  char *buf = buffer; // avoid pointer arithmetic on void pointer
  tot_read = 0;

  while (len != 0 && (nr = read(sock_fd, buf, len)) != 0) {
    if (nr < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        return -1;
      }
    }
    len -= nr;
    buf += nr;
    tot_read += nr;
  }

  return tot_read;
}

ssize_t sock_write(int sock_fd, void *buffer, size_t len) {
  ssize_t nw, tot_written;
  const char *buf = buffer; // avoid pointer arithmetic on void pointer

  for (tot_written = 0; tot_written < len;) {
    nw = write(sock_fd, buf, len - tot_written);

    if (nw <= 0) {
      if (nw == -1 && errno == EINTR) {
        continue;
      } else {
        return -1;
      }
    }

    tot_written += nw;
    buf += nw;
  }
  return tot_written;
}

void print_addrinfo(const struct addrinfo *ai) {
  char host[NI_MAXHOST];
  char serv[NI_MAXSERV];

  int ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof(host), serv,
                        sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
  if (ret != 0) {
    fprintf(stderr, "getnameinfo: %s\n", gai_strerror(ret));
    return;
  }

  printf("Address family : %s\n", (ai->ai_family == AF_INET)    ? "IPv4"
                                  : (ai->ai_family == AF_INET6) ? "IPv6"
                                                                : "Unknown");
  printf("Socket type    : %s\n", (ai->ai_socktype == SOCK_STREAM)  ? "TCP"
                                  : (ai->ai_socktype == SOCK_DGRAM) ? "UDP"
                                                                    : "Other");
  printf("Protocol       : %d\n", ai->ai_protocol);
  printf("Address        : %s\n", host);
  printf("Port           : %s\n\n", serv);
}

int sock_create_bind(char *port) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sock_fd = -1, ret = 0;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_PASSIVE;

  ret = getaddrinfo(NULL, port, &hints, &result);
  check(ret == 0, "getaddrinfo error.");
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock_fd < 0) {
      continue;
    }
    printf("printing all addr info on server side\n");
    print_addrinfo(rp);
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock_fd < 0) {
      continue;
    }
    if ((rp->ai_family != AF_INET6))
      continue;

    ret = bind(sock_fd, rp->ai_addr, rp->ai_addrlen);
    printf("using following addr info\n");
    print_addrinfo(rp);
    if (ret == 0) {
      /* bind success */
      break;
    }

    close(sock_fd);
    sock_fd = -1;
  }

  check(rp != NULL, "creating socket.");

  freeaddrinfo(result);
  return sock_fd;

error:
  if (result) {
    freeaddrinfo(result);
  }
  if (sock_fd > 0) {
    close(sock_fd);
  }
  return -1;
}

int sock_create_connect(char *server_name, char *port) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sock_fd = -1, ret = 0;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  ret = getaddrinfo(server_name, port, &hints, &result);
  check(ret == 0, "[ERROR] %s", gai_strerror(ret));

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock_fd == -1) {
      continue;
    }

    ret = connect(sock_fd, rp->ai_addr, rp->ai_addrlen);
    if (ret == 0) {
      /* connection success */
      break;
    }

    close(sock_fd);
    sock_fd = -1;
  }

  check(rp != NULL, "could not connect.");

  freeaddrinfo(result);
  return sock_fd;

error:
  if (result) {
    freeaddrinfo(result);
  }
  if (sock_fd != -1) {
    close(sock_fd);
  }
  return -1;
}

int sock_set_qp_info(int sock_fd, struct QPInfo *qp_info) {
  int n;
  struct QPInfo tmp_qp_info;

  tmp_qp_info.lid = htons(qp_info->lid);
  tmp_qp_info.qp_num = htonl(qp_info->qp_num);
  tmp_qp_info.spn = htonll(qp_info->spn);
  tmp_qp_info.iid = htonll(qp_info->iid);
  tmp_qp_info.rkey = htonl(qp_info->rkey);
  tmp_qp_info.raddr = htonll(qp_info->raddr);

  n = sock_write(sock_fd, (char *)&tmp_qp_info, sizeof(struct QPInfo));
  check(n == sizeof(struct QPInfo), "write qp_info to socket.");

  return 0;

error:
  return -1;
}

int sock_get_qp_info(int sock_fd, struct QPInfo *qp_info) {
  int n;
  struct QPInfo tmp_qp_info;

  n = sock_read(sock_fd, (char *)&tmp_qp_info, sizeof(struct QPInfo));
  check(n == sizeof(struct QPInfo), "read qp_info from socket.");

  qp_info->lid = ntohs(tmp_qp_info.lid);
  qp_info->qp_num = ntohl(tmp_qp_info.qp_num);
  qp_info->spn = ntohll(tmp_qp_info.spn);
  qp_info->iid = ntohll(tmp_qp_info.iid);
  qp_info->rkey = ntohl(tmp_qp_info.rkey);
  qp_info->raddr = ntohll(tmp_qp_info.raddr);

  return 0;

error:
  return -1;
}
