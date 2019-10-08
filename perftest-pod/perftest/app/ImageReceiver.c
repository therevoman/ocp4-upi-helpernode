#include "rdma-common.h"

static int on_connect_request(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static void usage(const char *argv0);


/*
Server Operation - A general connection flow would be:

rdma_create_event_channel		create channel to receive events
rdma_create_id					allocate an rdma_cm_id, this is conceptually similar to a socket
rdma_bind_addr					set the local port number to listen on
rdma_listen						begin listening for connection requests
rdma_get_cm_event				wait for RDMA_CM_EVENT_CONNECT_REQUEST event with a new rdma_cm_id
rdma_create_qp					allocate a QP for the communication on the new rdma_cm_id
rdma_accept						accept the connection request
rdma_ack_cm_event				ack event
rdma_get_cm_event				wait for RDMA_CM_EVENT_ESTABLISHED event
rdma_ack_cm_event				ack event

Perform data transfers over connection

rdma_get_cm_event				wait for RDMA_CM_EVENT_DISCONNECTED event
rdma_ack_cm_event				ack event
rdma_disconnect					tear-down connection
rdma_destroy_qp					destroy the QP
rdma_destroy_id					release the connected rdma_cm_id
rdma_destroy_id					release the listening rdma_cm_id
rdma_destroy_event_channel		release the event channel
*/


static int nBeginCommication;
static int nImageReceivers;
static struct rdma_cm_id *listener_id[MAX_CONNECTIONS];
static int nBasePort;

int main(int argc, char **argv)
{
   fprintf(stderr, "Debug statement start\n");
  struct sockaddr_in6 addr;
  struct rdma_cm_event *event = NULL;
  struct rdma_event_channel *ec = NULL;
  uint16_t port = 0;

   fprintf(stderr, "Debug statement 1\n");
  if (argc != 4) {
    fprintf(stderr, "Debug statement inside if\n");
    usage(argv[0]);
  }
  fprintf(stderr, "Debug statement 2\n");

  TEST_Z(ec = rdma_create_event_channel());
   fprintf(stderr, "Debug statement 3\n");

  nBasePort = strtol(argv[1],NULL,10);
   fprintf(stderr, "Debug statement 4\n");
  memset(&addr, 0, sizeof(addr));
   fprintf(stderr, "Debug statement 5\n");
  addr.sin6_family = AF_INET6;
   fprintf(stderr, "Debug statement 6\n");
  for(port = 0; port < MAX_CONNECTIONS; port++)
  {
   fprintf(stderr, "Debug statement inside loop\n");
	listener_id[port] = NULL;
	addr.sin6_port = htons(strtol(argv[1],NULL,10)+port);
	TEST_NZ(rdma_create_id(ec, &listener_id[port], NULL, RDMA_PS_TCP));
	TEST_NZ(rdma_bind_addr(listener_id[port], (struct sockaddr *)&addr));
	TEST_NZ(rdma_listen(listener_id[port], 10)); /* backlog=10 is arbitrary */
	fprintf(stderr, "Listening on port: %ld.\n", strtol(argv[1],NULL,10)+port);
  }

  nBeginCommication = strtol(argv[2],NULL,10);
  nImageReceivers = strtol(argv[3],NULL,10);
  if(nBeginCommication)
  {
	  fprintf(stderr, "There are a total of %d ImageReceivers.\n",nImageReceivers);
	  fprintf(stderr, "This node will begin comunication at %d source connections.\n",nBeginCommication);
  }

  while (rdma_get_cm_event(ec, &event) == 0) {
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    rdma_ack_cm_event(event);

    if (on_event(&event_copy))
      break;
  }

  for(port = 0; port < 4; port++)
	  rdma_destroy_id(listener_id[port]);
  rdma_destroy_event_channel(ec);

  return 0;
}

int on_connect_request(struct rdma_cm_id *id)
{
  struct rdma_conn_param cm_params;
  int port;

  //fprintf(stderr, "Received connection request from Xilinx Ernic.\n");
  for(port = 0; port < MAX_CONNECTIONS; port++)
  {
	if(listener_id[port] == id)
		break;
  }
  port = ntohs(((struct sockaddr_in6 *)&(id->route.addr.src_addr))->sin6_port)-nBasePort;
  build_connection(id, port);
  build_params(&cm_params);
  //set_available_frames_in_mr(id->context, 0);
  //put a string into the setup buffer for now.
  sprintf(get_local_message_region(id->context), "Setup Info buffer from Image Receiver with pid %d", getpid());
  TEST_NZ(rdma_accept(id, &cm_params));

  return 0;
}

int on_connection(struct rdma_cm_id *id)
{

  //fprintf(stderr, "Connected to Xilinx Ernic.\n");
  on_connect(id->context, nBeginCommication, nImageReceivers);

  return 0;
}

int on_disconnect(struct rdma_cm_id *id)
{
  fprintf(stderr, "peer disconnected.\n");

  destroy_connection(id->context);
  return 0;
}

int on_event(struct rdma_cm_event *event)
{
  int r = 0;

  if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
    r = on_connect_request(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    r = on_disconnect(event->id);
  else
    die("on_event: unknown event.");

  return r;
}

void usage(const char *argv0)
{
  fprintf(stderr, "usage: %s <mode>\n  mode = \"read\", \"write\"\n", argv0);
  exit(1);
}
