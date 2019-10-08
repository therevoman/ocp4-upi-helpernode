#include "rdma-common.h"
#include <sys/mman.h>


static const int RDMA_BUFFER_SIZE = FRAME_SIZE;

struct message {

  msg_type type;

  union {
    struct ibv_mr mr;
  } data;
  int nAvailableFrames;
};

struct context {
  struct ibv_context *ctx;
  struct ibv_pd *pd;	//Protection Domain
  struct ibv_cq *cq;	//Completion Queue
  struct ibv_comp_channel *comp_channel;

  pthread_t cq_poller_thread;
};

static int nConnectionCnt = 0;
static connection_struct sConnections[MAX_CONNECTIONS];

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
static void post_receives(connection_struct *conn);
static void register_memory(connection_struct *conn);

static struct context *s_ctx = NULL;

double ClockSeconds()
{
	struct timeval tp;
	struct timezone tzp;

	gettimeofday(&tp,&tzp);
	return ( (double) tp.tv_sec + (double) tp.tv_usec * 1.e-6 );
}

void die(const char *reason)
{
  fprintf(stderr, "%s\n", reason);
  exit(EXIT_FAILURE);
}

void build_connection(struct rdma_cm_id *id, int nPort)
{
  connection_struct *conn;
  struct ibv_qp_init_attr qp_attr;

  build_context(id->verbs);
  build_qp_attr(&qp_attr);

  TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));

  if(nPort >= MAX_CONNECTIONS)
  {
	fprintf(stderr, "Invalid allocation calculation = %d. Max = %d.\n",nPort, MAX_CONNECTIONS);
	nPort = MAX_CONNECTIONS - 1;
  }

  fprintf(stderr, "Connection on port: %d with local QP num = %d.\n",nPort, id->qp->qp_num);

  id->context = conn = &sConnections[nPort];//(connection_struct *)malloc(sizeof(connection_struct));
  conn->nConnectionNum = nPort;

  conn->id = id;
  conn->qp = id->qp;

  conn->connected = 0;
  conn->nCurrentImageBuffer = conn->nSentFrames = 0;

  nConnectionCnt++;
  register_memory(conn);
  post_receives(conn);
}

void build_context(struct ibv_context *verbs)
{
  struct ibv_device_attr device_attr;

  if (s_ctx)
  {
    if (s_ctx->ctx != verbs)
      die("cannot handle events in more than one context.");

	//fprintf(stderr, "Will use existing context.\n");
    return;
  }

  s_ctx = (struct context *)malloc(sizeof(struct context));

  s_ctx->ctx = verbs;

  TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
  TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
  TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
  TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));

  TEST_NZ(ibv_query_device(verbs, &device_attr));  //get device attributes

  TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

void build_params(struct rdma_conn_param *params)
{
  memset(params, 0, sizeof(*params));

  params->initiator_depth = params->responder_resources = 1;
  params->rnr_retry_count = 7; /* infinite retry */
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
  memset(qp_attr, 0, sizeof(*qp_attr));

  qp_attr->send_cq = s_ctx->cq;	//set completion queue for send
  qp_attr->recv_cq = s_ctx->cq;	//set completion queue for receive
  qp_attr->qp_type = IBV_QPT_RC;

  qp_attr->cap.max_send_wr = 10;
  qp_attr->cap.max_recv_wr = 10;
  qp_attr->cap.max_send_sge = 1;
  qp_attr->cap.max_recv_sge = 1;
}

void destroy_connection(void *context)
{
  int x;
  connection_struct *conn = (connection_struct *)context;

  rdma_destroy_qp(conn->id);

  ibv_dereg_mr(conn->send_mr);
  ibv_dereg_mr(conn->recv_mr);
  ibv_dereg_mr(conn->rdma_remote_mr);

  free(conn->send_msg);
  free(conn->recv_msg);
  for(x = 0; x < NUM_IMAGE_BUFFERS; x++)
  {
	  ibv_dereg_mr(conn->rdma_local_mr[x]);
	  free(conn->rdma_local_region[x]);
  }
  free(conn->rdma_remote_region);

  rdma_destroy_id(conn->id);

  free(conn);
}

void * get_local_message_region(void *context)
{
  //if (s_mode == M_WRITE)
  //  return ((struct connection *)context)->rdma_local_region;
  //else
    return ((connection_struct *)context)->rdma_remote_region;
}

void set_available_frames_in_mr(void *context, int nFrames)
{
  int *pInt;

  ((connection_struct *)context)->send_msg->nAvailableFrames = nFrames;
  pInt = (int *)get_local_message_region(context);
  *pInt = nFrames;
  //put a string into the Image buffer
  //if((nFrames & 0xff) == 0)
  //	sprintf(get_local_message_region(context), "Image buffer with Frames =%d, pid %d", nFrames, getpid());
}


void rdma_read(connection_struct *conn)
{
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    //if (s_mode == M_WRITE)
    //  fprintf(stderr, "received MSG_MR. writing RDMA buffer to remote memory...\n");
    //else
    //  fprintf(stderr, "received MSG_MR. reading RDMA buffer from remote memory...\n");

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)conn;
    //wr.opcode = (s_mode == M_WRITE) ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uintptr_t)conn->peer_mr.addr; //received on incomming MSG_MR
    wr.wr.rdma.rkey = conn->peer_mr.rkey; //received on incomming MSG_MR

    sge.addr = (uintptr_t)conn->rdma_local_region[conn->nCurrentImageBuffer];
    sge.length = RDMA_BUFFER_SIZE;
    sge.lkey = conn->rdma_local_mr[conn->nCurrentImageBuffer]->lkey;

    TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
	//fprintf(stderr, "RDMA SEND: sending RDMA_READ.\n");

    //conn->send_msg->type = MSG_READ_DONE;
   	//send_message(conn, MSG_READ_DONE);
}

static int nRoundRobinCnt = 0;
void set_round_robin(int x)
{
	nRoundRobinCnt = x;
}

void on_completion(struct ibv_wc *wc)
{
  connection_struct *conn = (connection_struct *)(uintptr_t)wc->wr_id;
  char *pRDMAbuffer;
  int x;
  static int nReceivedFrames = 0;
  static int nSentFrames = 0;
  static int nRDMAreadsDone = 0;
  static double timeStart, timeEnd;
  static double timeStart2, timeEnd2;
  static double timeMax = 0.0;
  static double timeMax2 = 0.0;
  //int x;

	if(wc->status != IBV_WC_SUCCESS)
	{
	    fprintf(stderr, "%s\n",ibv_wc_status_str(wc->status));
    	die("on_completion: status is not IBV_WC_SUCCESS.");
	}

	//if(wc->opcode == IBV_WC_RDMA_WRITE)
	//	fprintf(stderr, "IBV_WC_RDMA_WRITE complete\n");
	//fprintf(stderr, "IBV OpCode 0x%x\n",wc->opcode);

	if(wc->opcode == IBV_WC_RDMA_READ)
	{
		pRDMAbuffer = conn->rdma_local_region[conn->nCurrentImageBuffer];//get_peer_message_region(conn, conn->nCurrentImageBuffer);//nReceivedFrames);
		if(++conn->nCurrentImageBuffer >= NUM_IMAGE_BUFFERS)
			conn->nCurrentImageBuffer = 0;

	    //fprintf(stderr, "Recieved Frames %d, Buffer Frame Value = %d\n",conn->nReceivedFrames,*(int *)pRDMAbuffer);
		if(conn->nReceivedFrames != *(int *)pRDMAbuffer)
		{
	    	fprintf(stderr, "Recieved Frames %d, Buffer Frame Value = %d\n",conn->nReceivedFrames,*(int *)pRDMAbuffer);
			die("Frame count mismatch.");
		}
		conn->nReceivedFrames++;

#ifdef SYNCED_ROUND_ROBIN
		for(x = 0; x < nConnectionCnt; x++)
	    	send_message(&sConnections[x], MSG_READ_DONE);

		if(nReceivedFrames < 5)
	    	fprintf(stderr, "Sent Read Done to %d connections.\n", nConnectionCnt);
#else
	    send_message(conn, MSG_READ_DONE);
#endif
		if((++nReceivedFrames & (TIME_EVERY_NUM_FRAMES-1)) == 5)
		{
		  	timeEnd = ClockSeconds();
			if(nReceivedFrames != 5)
			{
				timeStart = timeEnd - timeStart;
				if(timeStart > timeMax)
					timeMax = timeStart;
		    	fprintf(stderr, "Bandwidth = %lf Gbps: Received frames = %d\n", ((double)8*TIME_EVERY_NUM_FRAMES*RDMA_BUFFER_SIZE/timeStart)*1.e-9, nReceivedFrames);
			}
		  	timeStart = timeEnd;
		}
		if(nReceivedFrames < 5)
		{
			if(nReceivedFrames == 1)
			  	timeStart = ClockSeconds();
	    	fprintf(stderr, "Frame %d received at %lf sec\n", nReceivedFrames, ClockSeconds() - timeStart);
		}
		//conn->recv_msg->type = MSG_READ_DONE;
	}

  if(wc->opcode & IBV_WC_RECV)
  {
    //    send_message(conn, MSG_DONE);
	if(conn->recv_msg->type == MSG_DONE) 
	{
   		fprintf(stderr, "Min Bandwidth on receiver = %lf Gbps.\n", ((double)8*TIME_EVERY_NUM_FRAMES*RDMA_BUFFER_SIZE/timeMax)*1.e-9);
	}
	else
    //if (1)//conn->recv_msg->type == MSG_MR)
    {
      memcpy(&conn->peer_mr, &conn->recv_msg->data.mr, sizeof(conn->peer_mr));
      post_receives(conn); /* only rearm for MSG_MR */
    }

	if(conn->recv_msg->type == MSG_MR)
	{
		if(conn->recv_msg->nAvailableFrames == 0)
		{
			conn->nCurrentImageBuffer = 0;					//reset count of incomming frames;
			conn->nReceivedFrames = 0;
		}
		rdma_read(conn);
	}
	if(conn->recv_msg->type == MSG_READ_DONE) 
	{
		nRDMAreadsDone++;
		if(nSentFrames < 5)
			fprintf(stderr, "RDMA Read Done MSG's = %d.\n", nRDMAreadsDone);

#ifdef SYNCED_ROUND_ROBIN
		if(nRDMAreadsDone >= conn->nTransmitters)
#else
		if(1)
#endif
		{
			nRDMAreadsDone = 0;
			if(sConnections[nRoundRobinCnt].nSentFrames < FRAMES_TO_TRANSFER)
			{
				set_available_frames_in_mr(&sConnections[nRoundRobinCnt], sConnections[nRoundRobinCnt].nSentFrames);
				sConnections[nRoundRobinCnt].nSentFrames++;
    			send_message(&sConnections[nRoundRobinCnt++], MSG_MR);
				if(nRoundRobinCnt >= nConnectionCnt)
					nRoundRobinCnt = 0;
				if((++nSentFrames & (TIME_EVERY_NUM_FRAMES-1)) == 5)
				{
				  	timeEnd2 = ClockSeconds();
					if(nSentFrames != 5)
					{
						timeStart2 = timeEnd2 - timeStart2;
						if(timeStart2 > timeMax2)
							timeMax2 = timeStart2;
			    		fprintf(stderr, "Bandwidth = %lf Gbps. Frames sent %d\n", ((double)8*TIME_EVERY_NUM_FRAMES*RDMA_BUFFER_SIZE/timeStart2)*1.e-9, nSentFrames);
					}
				  	timeStart2 = timeEnd2;
				}
				if(nSentFrames < 5)
				{
					if(nSentFrames == 1)
					  	timeStart2 = ClockSeconds();
	    			fprintf(stderr, "Frame %d sent at %lf sec\n", nSentFrames, ClockSeconds() - timeStart2);
				}
			}
			else
			{
				fprintf(stderr, "Finnished sending!\n");
	    		fprintf(stderr, "Min Bandwidth sent from source = %lf Gbps.\n", ((double)8*TIME_EVERY_NUM_FRAMES*RDMA_BUFFER_SIZE/timeMax2)*1.e-9);
				send_message(conn, MSG_DONE);
				//rdma_disconnect(conn->id);
			}
		}
	}

  }
  
}

void on_connect(void *context, int nBeginComm, int nTransmitters)
{
	int x, y;
	static int nNumConnected = 0;
	struct ibv_qp_attr attr;
	struct ibv_qp_init_attr init_attr;
	connection_struct *conn;
 

	nNumConnected++;
	conn = ((connection_struct *)context);
	conn->connected = 1;
	conn->nTransmitters = nTransmitters;
	ibv_query_qp(conn->qp, &attr,  IBV_QP_STATE, &init_attr);
#ifdef SYNCED_ROUND_ROBIN
	fprintf(stderr, "SYNCED ROUND ROBIN: ConnectionNum = %d, QP Num Local %d, Remote %d\n",conn->nConnectionNum, conn->qp->qp_num, attr.dest_qp_num);
#else
	fprintf(stderr, "UNSYNCED ROUND ROBIN: ConnectionNum = %d, QP Num Local %d, Remote %d\n",conn->nConnectionNum, conn->qp->qp_num, attr.dest_qp_num);
#endif

	if(nNumConnected == nBeginComm)
	{
		//usleep(6000000);
#ifdef SYNCED_ROUND_ROBIN
		for(y = 0; y < nTransmitters; y++) //Equal number of image recievers for each transfer 
#else
		for(y = 0; y < 1; y++)
#endif
		{
			usleep(100000);
			for(x = 0; x < nTransmitters; x++)
				send_message(&sConnections[x], MSG_READ_DONE); //send done to all Transmitters
		}
	}

}

void * poll_cq(void *ctx)
{
  struct ibv_cq *cq;
  struct ibv_wc wc;

  while (1) {
    TEST_NZ(ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx));
    ibv_ack_cq_events(cq, 1);
    TEST_NZ(ibv_req_notify_cq(cq, 0));

    while (ibv_poll_cq(cq, 1, &wc))
      on_completion(&wc);
  }

  return NULL;
}

void post_receives(connection_struct *conn)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  wr.wr_id = (uintptr_t)conn;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)conn->recv_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->recv_mr->lkey;

  TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_memory(connection_struct *conn)
{
  int x;
  conn->send_msg = malloc(sizeof(struct message));
  conn->recv_msg = malloc(sizeof(struct message));
	

  for(x = 0; x < NUM_IMAGE_BUFFERS; x++)
	  TEST_Z(conn->rdma_local_region[x] = malloc(RDMA_BUFFER_SIZE));
  TEST_Z(conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE));

  TEST_Z(conn->send_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->send_msg, 
    sizeof(struct message), 
    IBV_ACCESS_LOCAL_WRITE));

  TEST_Z(conn->recv_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->recv_msg, 
    sizeof(struct message), 
    //IBV_ACCESS_LOCAL_WRITE | ((s_mode == M_WRITE) ? IBV_ACCESS_REMOTE_WRITE : IBV_ACCESS_REMOTE_READ)));
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));

  for(x = 0; x < NUM_IMAGE_BUFFERS; x++)
  {
	TEST_Z(conn->rdma_local_mr[x] = ibv_reg_mr(
		s_ctx->pd, 
		conn->rdma_local_region[x], 
		RDMA_BUFFER_SIZE, 
		IBV_ACCESS_LOCAL_WRITE));
  }
  TEST_Z(conn->rdma_remote_mr = ibv_reg_mr(
    s_ctx->pd, 
    conn->rdma_remote_region, 
    RDMA_BUFFER_SIZE, 
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));
}

void send_message(connection_struct *conn, msg_type type)
{
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  if(type == MSG_MR)
  {
	//fprintf(stderr, "RDMA SEND: Sending Memory Region Info.\n");
	memcpy(&conn->send_msg->data.mr, conn->rdma_remote_mr, sizeof(struct ibv_mr));
  }

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)conn;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.send_flags = IBV_SEND_SIGNALED;

  conn->send_msg->type = type;
  sge.addr = (uintptr_t)conn->send_msg;
  sge.length = sizeof(struct message);
  sge.lkey = conn->send_mr->lkey;

  while (!conn->connected);

  TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}



