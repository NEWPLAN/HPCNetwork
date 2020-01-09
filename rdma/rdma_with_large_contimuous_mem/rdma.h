#ifndef __RDMA_H__
#define __RDMA_H__

#include <iostream>
#include <vector>
#include <string>
#include <pthread.h>
#include <stdarg.h>
#include <rdma/rdma_cma.h>
#include <glog/logging.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <random>

//--- config ---
#define MAX_CONCURRENCY 1024

#define mr_size_in_kb 32
const size_t BUFFER_SIZE = mr_size_in_kb * 1 * 1024;

#define message_size_in_kb 0.5
//#define message_size_in_kb 0.0625
const uint32_t MSG_SIZE = message_size_in_kb * 1 * 1024;

#define loop 1000000
//--- end of config ---

#define _ENABLE_READ_
#define IS_CLIENT false
#define IS_SERVER true
//50 M for default size;
#define TIMEOUT_IN_MS 500
#define __RDMA_SLOW__ 1
#define MIN_CQE 10
#define clip_bit(num, mask) ((num) & (~(1 << (7 - (mask))))) | ((num) ^ (1 << (7 - (mask))))

static clock_t tstart = 0;
bool first = true;
struct timeval start_, now_;

struct timeval tem1, tem2;

typedef struct _data_list_
{
	char *data_ptr;
	struct _data_list_ *next;
	uint32_t data_len;
} node_item;

typedef struct _rdma_pack_
{
	struct rdma_cm_id *rdma_id;
	node_item *nit;
} _rdma_thread_pack_;

enum message_id
{
	MSG_INVALID = 0,
	MSG_MR,
	MSG_READY,
	MSG_DONE
};

typedef struct _key_exchange_
{
	int id;
	uint64_t md5;
	struct _k_
	{
		uint64_t addr;
		uint32_t rkey;
	} key_info[MAX_CONCURRENCY], bitmap;

} _key_exch;

typedef struct _ack
{
	int index;
	uint64_t time_cost;
} _ack_;

struct context
{
	struct ibv_context *ibv_ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_comp_channel *comp_channel;

	pthread_t cq_poller_thread;

	//register buffer for remote to write
	char *buffer;			  //MAX_CONCURRENCY------>one
	struct ibv_mr *buffer_mr; //MAX_CONCURRENCY------>one

	//register ack mem is used for write to remote
	_ack_ *ack;			   //MAX_CONCURRENCY------>one
	struct ibv_mr *ack_mr; //MAX_CONCURRENCY------>one

	//indicate current status of each peer exchange
	bool is_busy[MAX_CONCURRENCY];
	// index 0: store for local as tx
	// index 1: used to recv the remote info
	_key_exch *k_exch[2];
	struct ibv_mr *k_exch_mr[2];

	/*store the peer addr and rkey*/
	uint64_t peer_addr[MAX_CONCURRENCY];
	uint32_t peer_rkey[MAX_CONCURRENCY];

#ifdef _ENABLE_READ_
	/**used for bitmap**/
	char *bitmap[2];
	struct ibv_mr *bitmap_mr[2]; /*expose index 0 to peer for reading*/
	uint64_t peer_bitmap_addr;
	uint32_t peer_bitmap_rkey;
#endif
};

struct Adapter
{
	unsigned short server_port;
	struct rdma_event_channel *event_channel;
	struct rdma_cm_id *listener;
	std::vector<rdma_cm_id *> recv_rdma_cm_id;

	std::string server_ip;
	std::string client_ip;
	Adapter()
	{
		server_port = 1234;
		server_ip = "";
		client_ip = "";
	}
	void set_server_ip(const char *_server_ip)
	{
		server_ip = _server_ip;
		std::cout << "Server IP : " << server_ip << std::endl;
	}
	void set_client_ip(const char *_client_ip)
	{
		client_ip = _client_ip;
		std::cout << "Client IP : " << client_ip << std::endl;
	}
};

#define TEST_NZ(x)                                               \
	do                                                           \
	{                                                            \
		if ((x))                                                 \
			rc_die("error: " #x " failed (returned non-zero)."); \
	} while (0)
#define TEST_Z(x)                                                 \
	do                                                            \
	{                                                             \
		if (!(x))                                                 \
			rc_die("error: " #x " failed (returned zero/null)."); \
	} while (0)

static void rc_die(const char *reason);
void log_info(const char *format, ...);
long long current_time(void);
static void post_send(struct rdma_cm_id *id, ibv_wr_opcode opcode);
static void _write_remote(struct rdma_cm_id *id, uint32_t len, uint32_t index, ibv_wr_opcode opcode);
static void _post_receive(struct rdma_cm_id *id, uint32_t index);
static void _ack_remote(struct rdma_cm_id *id, uint32_t index);
/*map 0 is used for peer read and bitmap 1 is used to store the peer info*/
static std::vector<int> recv_handle_bitmap(struct context *ctx);
static void update_bitmap(struct context *ctx, int index);
static void *concurrency_recv_by_RDMA(struct rdma_cm_id *id, struct ibv_wc *wc, uint32_t &recv_len);
static void *send_tensor(struct rdma_cm_id *id, uint32_t index);
std::random_device rd;
static void *write_tensor(struct rdma_cm_id *id, uint32_t index);
static std::vector<int> send_handle_bitmap(struct context *ctx);
static void *concurrency_send_by_RDMA(struct rdma_cm_id *id, struct ibv_wc *wc, int &mem_used);
static void *recv_poll_cq(void *_id);
static void *send_poll_cq(void *_id);
static struct ibv_pd *rc_get_pd(struct rdma_cm_id *id);
static void _build_params(struct rdma_conn_param *params);
static void _build_context(struct rdma_cm_id *id, bool is_server);
static void _build_qp_attr(struct ibv_qp_init_attr *qp_attr, struct rdma_cm_id *id);
static void _build_connection(struct rdma_cm_id *id, bool is_server);
static void _on_pre_conn(struct rdma_cm_id *id);
/*server on connection*/
static void _on_connection(struct rdma_cm_id *id, bool is_server);
static void _on_disconnect(struct rdma_cm_id *id);
static void recv_RDMA(Adapter &rdma_adapter);
static void rdma_server_init(Adapter &rdma_adapter);
static void rdma_client_init(Adapter &rdma_adapter);
void help(void);
#endif
