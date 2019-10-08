#include "rdma-common.h"

const int TIMEOUT_IN_MS = 500; /* ms */

static int on_addr_resolved(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static int on_route_resolved(struct rdma_cm_id *id);
static void usage(const char *argv0);

/*
Client Operation - A general connection flow would be:

rdma_getaddrinfo			retrieve address information of the destination
rdma_create_event_channel	create channel to receive events
rdma_create_id				allocate an rdma_cm_id, this is conceptually similar to a socket
rdma_resolve_addr			obtain a local RDMA device to reach the remote address
rdma_get_cm_event			wait for RDMA_CM_EVENT_ADDR_RESOLVED event
rdma_ack_cm_event			ack event
rdma_create_qp				allocate a QP for the communication
rdma_resolve_route			determine the route to the remote address
rdma_get_cm_event			wait for RDMA_CM_EVENT_ROUTE_RESOLVED event
rdma_ack_cm_event			ack event
rdma_connect				connect to the remote server
rdma_get_cm_event			wait for RDMA_CM_EVENT_ESTABLISHED event
rdma_ack_cm_event			ack event

Perform data transfers over connection

rdma_disconnect				tear-down connection
rdma_get_cm_event			wait for RDMA_CM_EVENT_DISCONNECTED event
rdma_ack_cm_event			ack event
rdma_destroy_qp				destroy the QP
rdma_destroy_id				release the rdma_cm_id
rdma_destroy_event_channel	release the event channel
*/

static	struct rdma_cm_id *conn_id[MAX_CONNECTIONS];
static int nTransmitters;

int main(int argc, char **argv)
{
	struct addrinfo *addr;
	struct rdma_cm_event *event = NULL;
	struct rdma_event_channel *ec = NULL;

	struct ibv_device **dev_list;
	int i, nRoundRobin;
 
	if (argc < 4)
		usage(argv[0]);
	
	nRoundRobin = strtol(argv[1],NULL,10);
	printf("Setting this node to intitial round robin = %d\n",nRoundRobin);
	set_round_robin(nRoundRobin);

	nTransmitters = strtol(argv[2],NULL,10);
	printf("Total Transmitters set to %d\n",nTransmitters);

	TEST_Z(ec = rdma_create_event_channel());

	for(i = 0; i < argc - 4; i++)
	{
		conn_id[i] = NULL;
		TEST_NZ(getaddrinfo(argv[4+i], argv[3], NULL, &addr));
		TEST_NZ(rdma_create_id(ec, &conn_id[i], NULL, RDMA_PS_TCP));
		TEST_NZ(rdma_resolve_addr(conn_id[i], NULL, addr->ai_addr, TIMEOUT_IN_MS));
		//usleep(100000);
	}
	while(i < MAX_CONNECTIONS)
		conn_id[i++] = NULL;

	while (rdma_get_cm_event(ec, &event) == 0)
	{
		struct rdma_cm_event event_copy;

		memcpy(&event_copy, event, sizeof(*event));
		rdma_ack_cm_event(event);

		if (on_event(&event_copy)>0)
			break;
	}

	freeaddrinfo(addr);
	rdma_destroy_event_channel(ec);

	return 0;
}

int on_addr_resolved(struct rdma_cm_id *id)
{

  int port;

  for(port = 0; port < MAX_CONNECTIONS; port++)
  {
	if(conn_id[port] == id)
		break;
  }
  if(port >= MAX_CONNECTIONS)
  {
	  printf("Connection not found for connection request!\n");
	  return 0;
  }

  build_connection(id,port);

  TEST_NZ(rdma_resolve_route(id, TIMEOUT_IN_MS));

  return 0;
}

int on_connection(struct rdma_cm_id *id)
{
	//int nFrameCnt = 0;
	on_connect(id->context, 0, nTransmitters);

	//set_available_frames_in_mr(id->context, nFrameCnt);
	//printf("Connected: Sending AvailableFrames %d Info to ImageReceiver.\n", nFrameCnt);
	//send_message((connection_struct *)id->context, MSG_MR);
	/*usleep(1000000);
	usleep(1000000);
	usleep(1000000);*/

	return -1;
}

int on_disconnect(struct rdma_cm_id *id)
{
  printf("Disconnected.\n");

  destroy_connection(id->context);
  return 1; /* exit event loop */
}

int on_event(struct rdma_cm_event *event)
{
 	int r = 0;

	switch(event->event)
	{
		case RDMA_CM_EVENT_ADDR_RESOLVED:
			r = on_addr_resolved(event->id);
			break;

		case RDMA_CM_EVENT_ROUTE_RESOLVED:
		    r = on_route_resolved(event->id);
			break;

		case RDMA_CM_EVENT_ESTABLISHED:
		    r = on_connection(event->id);
			break;

		case RDMA_CM_EVENT_DISCONNECTED:
		    r = on_disconnect(event->id);
			break;

		case RDMA_CM_EVENT_ADDR_ERROR:
		case RDMA_CM_EVENT_ROUTE_ERROR:
		case RDMA_CM_EVENT_CONNECT_REQUEST:
		case RDMA_CM_EVENT_CONNECT_RESPONSE:
		case RDMA_CM_EVENT_CONNECT_ERROR:
		case RDMA_CM_EVENT_UNREACHABLE:
		case RDMA_CM_EVENT_REJECTED:
		case RDMA_CM_EVENT_DEVICE_REMOVAL:
		case RDMA_CM_EVENT_MULTICAST_JOIN:
		case RDMA_CM_EVENT_MULTICAST_ERROR:
		case RDMA_CM_EVENT_ADDR_CHANGE:
		case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		default:
		    fprintf(stderr, "on_event: %d\n", event->event);
		    die("on_event: unknown event.");
			break;
  	}

	return r;
}

int on_route_resolved(struct rdma_cm_id *id)
{
	struct rdma_conn_param cm_params;

	//printf("Route resolved.\n");
	build_params(&cm_params);
	TEST_NZ(rdma_connect(id, &cm_params));

	return 0;
}

void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s <server-address> <server-port>\n", argv0);
	exit(1);
}
