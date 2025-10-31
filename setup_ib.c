#include <arpa/inet.h>
#include <malloc.h>
#include <unistd.h>

#include "config.h"
#include "debug.h"
#include "ib.h"
#include "setup_ib.h"
#include "sock.h"

struct IBRes ib_res;

int connect_qp_server(struct ibv_context *ctx) {
  int ret = 0, n = 0;
  int sockfd = 0;
  int peer_sockfd = 0;
  struct sockaddr_in peer_addr;
  socklen_t peer_addr_len = sizeof(struct sockaddr_in);
  char sock_buf[64] = {'\0'};
  struct QPInfo local_qp_info, remote_qp_info;

  sockfd = sock_create_bind(config_info.sock_port);
  check(sockfd > 0, "Failed to create server socket.");
  listen(sockfd, 5);

  peer_sockfd = accept(sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len);
  check(peer_sockfd > 0, "Failed to create peer_sockfd");

  union ibv_gid gid;
  if (ibv_query_gid(ctx, 1 /* port number*/, 3 /* GID INDEX */, &gid)) {
    return 1;
  }

  /* init local qp_info */
  local_qp_info.spn = gid.global.subnet_prefix;
  local_qp_info.iid = gid.global.interface_id;
  local_qp_info.lid = ib_res.port_attr.lid;
  local_qp_info.qp_num = ib_res.qp->qp_num;
  local_qp_info.rkey = ib_res.mr->rkey;
  local_qp_info.raddr = (uintptr_t)ib_res.ib_buf;

  printf("[Server] Generated local QP info: qp_num=%d, lid=%d, spn=%ld, "
         "iid=%ld, raddr=%lu\n",
         local_qp_info.qp_num, local_qp_info.lid, local_qp_info.spn,
         local_qp_info.iid, local_qp_info.raddr);
  /* get qp_info from client */
  ret = sock_get_qp_info(peer_sockfd, &remote_qp_info);
  check(ret == 0, "Failed to get qp_info from client");

  /* send qp_info to client */
  ret = sock_set_qp_info(peer_sockfd, &local_qp_info);
  check(ret == 0, "Failed to send qp_info to client");

  /* store rkey and raddr info */
  ib_res.rkey = remote_qp_info.rkey;
  ib_res.raddr = remote_qp_info.raddr;

  /* change send QP state to RTS */
  printf("[Server] Received QP info from client: qp_num=%d, lid=%d, spn=%ld, "
         "iid=%ld, raddr=%lu\n",
         remote_qp_info.qp_num, remote_qp_info.lid, remote_qp_info.spn,
         remote_qp_info.iid, remote_qp_info.raddr);

  ret = modify_qp_to_rts(ib_res.qp, &remote_qp_info);
  check(ret == 0, "Failed to modify qp to rts");

  log(LOG_SUB_HEADER, "Start of IB Config");
  log("\tqp[%" PRIu32 "] <-> qp[%" PRIu32 "]", ib_res.qp->qp_num,
      remote_qp_info.qp_num);
  log("\traddr[%" PRIu64 "] <-> raddr[%" PRIu64 "]", local_qp_info.raddr,
      ib_res.raddr);
  log(LOG_SUB_HEADER, "End of IB Config");

  /* sync with clients */
  n = sock_read(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
  check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");

  n = sock_write(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
  check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");

  close(peer_sockfd);
  close(sockfd);
  printf("[Server] Connected to client [%" PRIu32 "]\n", remote_qp_info.qp_num);
  return 0;

error:
  if (peer_sockfd > 0) {
    close(peer_sockfd);
  }
  if (sockfd > 0) {
    close(sockfd);
  }

  return -1;
}

int connect_qp_client(struct ibv_context *ctx) {
  int ret = 0, n = 0;
  int peer_sockfd = 0;
  char sock_buf[64] = {'\0'};

  struct QPInfo local_qp_info, remote_qp_info;

  peer_sockfd =
      sock_create_connect(config_info.server_name, config_info.sock_port);
  check(peer_sockfd > 0, "Failed to create peer_sockfd");

  union ibv_gid gid;
  if (ibv_query_gid(ctx, 1 /* port number*/, 3 /* GID INDEX */, &gid)) {
    return 1;
  }

  /* init local qp_info */
  local_qp_info.spn = gid.global.subnet_prefix;
  local_qp_info.iid = gid.global.interface_id;
  local_qp_info.lid = ib_res.port_attr.lid;
  local_qp_info.qp_num = ib_res.qp->qp_num;
  local_qp_info.rkey = ib_res.mr->rkey;
  local_qp_info.raddr = (uintptr_t)ib_res.ib_buf;

  /* send qp_info to server */
  printf("[Client] Generated local QP info: qp_num=%d, lid=%d, spn=%ld, "
         "iid=%ld, raddr=%lu\n",
         local_qp_info.qp_num, local_qp_info.lid, local_qp_info.spn,
         local_qp_info.iid, local_qp_info.raddr);
  ret = sock_set_qp_info(peer_sockfd, &local_qp_info);
  check(ret == 0, "Failed to send qp_info to server");

  /* get qp_info from server */
  ret = sock_get_qp_info(peer_sockfd, &remote_qp_info);
  check(ret == 0, "Failed to get qp_info from server");
  printf("[Client] Received QP info from server: qp_num=%d, lid=%d, spn=%ld, "
         "iid=%ld, raddr=%lu\n",
         remote_qp_info.qp_num, remote_qp_info.lid, remote_qp_info.spn,
         remote_qp_info.iid, remote_qp_info.raddr);
  /* store rkey and raddr info */
  ib_res.rkey = remote_qp_info.rkey;
  ib_res.raddr = remote_qp_info.raddr;

  /* change QP state to RTS */
  ret = modify_qp_to_rts(ib_res.qp, &remote_qp_info);
  check(ret == 0, "Failed to modify qp to rts");

  log(LOG_SUB_HEADER, "IB Config");
  log("\tqp[%" PRIu32 "] <-> qp[%" PRIu32 "]", ib_res.qp->qp_num,
      remote_qp_info.qp_num);
  log("\traddr[%" PRIu64 "] <-> raddr[%" PRIu64 "]", local_qp_info.raddr,
      ib_res.raddr);
  log(LOG_SUB_HEADER, "End of IB Config");

  /* sync with server */
  n = sock_write(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
  check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");

  n = sock_read(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
  check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");

  close(peer_sockfd);
  printf("[Client] Connected to server [%" PRIu32 "]\n", remote_qp_info.qp_num);
  return 0;

error:
  if (peer_sockfd > 0) {
    close(peer_sockfd);
  }

  return -1;
}

int setup_ib() {
  int ret = 0;
  struct ibv_device **dev_list = NULL;
  memset(&ib_res, 0, sizeof(struct IBRes));

  /* get IB device list */
  dev_list = ibv_get_device_list(NULL);
  check(dev_list != NULL, "Failed to get ib device list.");

  /* create IB context */
  ib_res.ctx = ibv_open_device(*dev_list);
  check(ib_res.ctx != NULL, "Failed to open ib device.");

  /* allocate protection domain */
  ib_res.pd = ibv_alloc_pd(ib_res.ctx);
  check(ib_res.pd != NULL, "Failed to allocate protection domain.");

  /* query IB port attribute */
  ret = ibv_query_port(ib_res.ctx, IB_PORT, &ib_res.port_attr);
  check(ret == 0, "Failed to query IB port information.");

  /* set the buf_size (msg_size + 1) * num_concurr_msgs */
  /* the recv buffer is of size msg_size * num_concurr_msgs */
  /* followed by a sending buffer of size msg_size since we */
  /* assume all msgs are of the same content */
  ib_res.ib_buf_size =
      config_info.msg_size * (config_info.num_concurr_msgs + 1);
  ib_res.ib_buf = (char *)memalign(4096, ib_res.ib_buf_size);
  check(ib_res.ib_buf != NULL, "Failed to allocate ib_buf");

  ib_res.mr = ibv_reg_mr(ib_res.pd, (void *)ib_res.ib_buf, ib_res.ib_buf_size,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                             IBV_ACCESS_REMOTE_WRITE);
  check(ib_res.mr != NULL, "Failed to register mr");

  /* reset receiving buffer to all '0' */
  size_t buf_len = config_info.msg_size * config_info.num_concurr_msgs;
  memset(ib_res.ib_buf, '\0', buf_len);

  /* set sending buffer to all 'A' */
  memset(ib_res.ib_buf + buf_len, 'A', config_info.msg_size);

  /* query IB device attr */
  ret = ibv_query_device(ib_res.ctx, &ib_res.dev_attr);
  check(ret == 0, "Failed to query device");

  /* create cq */
  int cq_depth = 1024;
  ib_res.cq = ibv_create_cq(ib_res.ctx, cq_depth, NULL, NULL, 0);
  check(ib_res.cq != NULL, "Failed to create cq");

  /* conservative QP caps; no SRQ in this example */
  struct ibv_qp_init_attr qp_init_attr = {
      .send_cq = ib_res.cq,
      .recv_cq = ib_res.cq,
      .cap =
          {
              .max_send_wr = 8192, /* 256–1024 are usually fine */
              .max_recv_wr = 8192,
              .max_send_sge = 1,
              .max_recv_sge = 1,
              .max_inline_data = config_info.msg_size,
          },
      .qp_type = IBV_QPT_RC,
  };
  ib_res.qp = ibv_create_qp(ib_res.pd, &qp_init_attr);
  check(ib_res.qp != NULL, "Failed to create qp");

  /* connect QP */
  if (config_info.is_server) {
    ret = connect_qp_server(ib_res.ctx);
  } else {
    ret = connect_qp_client(ib_res.ctx);
  }
  check(ret == 0, "Failed to connect qp");

  ibv_free_device_list(dev_list);
  return 0;

error:
  if (dev_list != NULL) {
    ibv_free_device_list(dev_list);
  }
  return -1;
}

void close_ib_connection() {
  if (ib_res.qp != NULL) {
    ibv_destroy_qp(ib_res.qp);
  }

  if (ib_res.cq != NULL) {
    ibv_destroy_cq(ib_res.cq);
  }

  if (ib_res.mr != NULL) {
    ibv_dereg_mr(ib_res.mr);
  }

  if (ib_res.pd != NULL) {
    ibv_dealloc_pd(ib_res.pd);
  }

  if (ib_res.ctx != NULL) {
    ibv_close_device(ib_res.ctx);
  }

  if (ib_res.ib_buf != NULL) {
    free(ib_res.ib_buf);
  }
}
