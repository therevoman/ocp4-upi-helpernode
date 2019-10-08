#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <sys/time.h>

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

#define SYNCED_ROUND_ROBIN
#define TIME_EVERY_NUM_FRAMES 0x400
#define FRAMES_TO_TRANSFER 0x4005
#define NUM_IMAGE_BUFFERS 2
//#define FRAME_SIZE 4096*1280*2
#define FRAME_SIZE 4096*1280*8
//#define FRAME_SIZE 1024
#define MAX_CONNECTIONS 4

typedef enum {
    MSG_MR,
    MSG_READ_DONE,
    MSG_DONE
} msg_type;

typedef struct {
  struct rdma_cm_id *id;
  struct ibv_qp *qp;

  int connected;
  int nConnectionNum;
  int nCurrentImageBuffer;
  int nSentFrames;
  int nReceivedFrames;
  int nTransmitters;
  double timeStart;
  double timeEnd;
  struct ibv_mr *recv_mr;
  struct ibv_mr *send_mr;
  struct ibv_mr *rdma_local_mr[NUM_IMAGE_BUFFERS+1];
  struct ibv_mr *rdma_remote_mr;

  struct ibv_mr peer_mr;

  struct message *recv_msg;
  struct message *send_msg;

  char *rdma_local_region[NUM_IMAGE_BUFFERS];
  char *rdma_remote_region;

} connection_struct;

void die(const char *reason);

void set_available_frames_in_mr(void *context, int nFrames);
void build_connection(struct rdma_cm_id *id, int nBasePort);
void build_params(struct rdma_conn_param *params);
void destroy_connection(void *context);
void * get_local_message_region(void *context);
void on_connect(void *context, int nBeginComm, int nReceivers);
void send_mr(void *context);
void send_message(connection_struct *conn, msg_type type);
void set_round_robin(int x);


#endif
