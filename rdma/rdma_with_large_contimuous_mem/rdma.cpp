#include "rdma.h"

static void rc_die(const char *reason)
{
	extern int errno;
	fprintf(stderr, "%s\nstrerror= %s\n", reason, strerror(errno));
	exit(-1);
}

void log_info(const char *format, ...)
{
	char now_time[32];
	char s[1024];
	char content[1024];
	//char *ptr = content;
	struct tm *tmnow;
	struct timeval tv;
	bzero(content, 1024);
	va_list arg;
	va_start(arg, format);
	vsprintf(s, format, arg);
	va_end(arg);

	gettimeofday(&tv, NULL);
	tmnow = localtime(&tv.tv_sec);

	sprintf(now_time, "[%04d/%02d/%02d %02d:%02d:%02d:%06ld]->",
			tmnow->tm_year + 1900, tmnow->tm_mon + 1, tmnow->tm_mday, tmnow->tm_hour,
			tmnow->tm_min, tmnow->tm_sec, tv.tv_usec);

	sprintf(content, "%s %s", now_time, s);
	printf("%s", content);
}

long long current_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

/* 12.12.11.XXX */
/*for read remote*/
static void post_send(struct rdma_cm_id *id, ibv_wr_opcode opcode)
{
	struct context *new_ctx = (struct context *)id->context;
	struct ibv_sge sge;
	struct ibv_send_wr sr, *bad_wr = NULL;

	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)new_ctx->bitmap[1];
	sge.length = MAX_CONCURRENCY;
	sge.lkey = new_ctx->bitmap_mr[1]->lkey;

	/* prepare the send work request */
	memset(&sr, 0, sizeof(sr));
	sr.next = NULL;
	sr.wr_id = (uintptr_t)id;
	;
	sr.sg_list = &sge;
	sr.num_sge = 1;
	sr.opcode = opcode;
	sr.send_flags = IBV_SEND_SIGNALED;
	sr.wr.rdma.remote_addr = new_ctx->peer_bitmap_addr;
	sr.wr.rdma.rkey = new_ctx->peer_bitmap_rkey;
	/* there is a Receive Request in the responder side, so we won't get any into RNR flow */
	TEST_NZ(ibv_post_send(id->qp, &sr, &bad_wr));
}

static void _write_remote(struct rdma_cm_id *id, uint32_t len, uint32_t index, ibv_wr_opcode opcode)
{
	//std::cout<<"_write_remote"<<std::endl;

	struct context *new_ctx = (struct context *)id->context;

	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	//wr.wr_id = (uintptr_t)id;
	wr.wr_id = index;

	wr.opcode = opcode;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.imm_data = index; //htonl(index);

	wr.wr.rdma.remote_addr = new_ctx->peer_addr[index];
	wr.wr.rdma.rkey = new_ctx->peer_rkey[index];

	if (len)
	{
		wr.sg_list = &sge;
		wr.num_sge = 1;

		sge.addr = (uintptr_t)new_ctx->buffer + index * BUFFER_SIZE;
		sge.length = len;
		sge.lkey = new_ctx->buffer_mr->lkey;
	}

	TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void _post_receive(struct rdma_cm_id *id, uint32_t index)
{
	struct ibv_recv_wr wr, *bad_wr = NULL;
	memset(&wr, 0, sizeof(wr));
	wr.wr_id = (uint64_t)id;
	wr.sg_list = NULL;
	wr.num_sge = 0;
	TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}
/*
static void _ack_remote(struct rdma_cm_id *id, uint32_t index)
{
	struct context *new_ctx = (struct context *)id->context;

	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;

	wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.imm_data = index; //htonl(index);
	wr.wr.rdma.remote_addr = new_ctx->peer_addr[index];
	wr.wr.rdma.rkey = new_ctx->peer_rkey[index];

	new_ctx->ack[index]->index = index;

	{
		wr.sg_list = &sge;
		wr.num_sge = 1;

		sge.addr = (uintptr_t)new_ctx->ack[index];
		sge.length = sizeof(_ack_);
		sge.lkey = new_ctx->ack_mr[index]->lkey;
	}

	TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}
*/
static std::vector<int> recv_handle_bitmap(struct context *ctx)
{
	std::vector<int> available;
	for (int index = 0; index < MAX_CONCURRENCY; index++)
	{
		if (ctx->bitmap[0][index] != ctx->bitmap[1][index])
			available.push_back(index);
	}
	return available;
}

static void update_bitmap(struct context *ctx, int index)
{
	ctx->bitmap[0][index] = (ctx->bitmap[0][index] + 1) % 2;
	//ctx->bitmap[0][index / 8] = clip_bit(ctx->bitmap[0][index / 8], index % 8);
	return;
}

static void *concurrency_recv_by_RDMA(struct rdma_cm_id *id, struct ibv_wc *wc, uint32_t &recv_len)
{
	//struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
	struct context *ctx = (struct context *)id->context;
	void *_data = nullptr;

	switch (wc->opcode)
	{
	case IBV_WC_RECV_RDMA_WITH_IMM:
	{ /*
		//log_info("recv with IBV_WC_RECV_RDMA_WITH_IMM\n");
		//log_info("imm_data is %d\n", wc->imm_data);
		//uint32_t size = ntohl(wc->imm_data);
		uint32_t index = wc->imm_data;
		uint32_t size = *((uint32_t *)(ctx->buffer[index]));
		char *recv_data_ptr = ctx->buffer[index] + sizeof(uint32_t);

		recv_len = size;
		_data = (void *)std::malloc(sizeof(char) * size);

		if (_data == nullptr)
		{
			printf("fatal error in recv data malloc!!!!\n");
			exit(-1);
		}
		std::memcpy(_data, recv_data_ptr, size);

		_post_receive(id, wc->imm_data);
		_ack_remote(id, wc->imm_data);
		log_info("recv data: %s\n", _data);
		*/
		break;
	}
	case IBV_WC_RECV:
	{
		if (MSG_MR == ctx->k_exch[1]->id)
		{
			log_info("recv MD5 is %llu\n", ctx->k_exch[1]->md5);
			log_info("imm_data is %d\n", wc->imm_data);
			for (int index = 0; index < MAX_CONCURRENCY; index++)
			{
				ctx->peer_addr[index] = ctx->k_exch[1]->key_info[index].addr;
				ctx->peer_rkey[index] = ctx->k_exch[1]->key_info[index].rkey;
				struct sockaddr_in *client_addr = (struct sockaddr_in *)rdma_get_peer_addr(id);
				printf("client[%s,%d] to ", inet_ntoa(client_addr->sin_addr), client_addr->sin_port);
				printf("server ack %d: %p  ", index, ctx->peer_addr[index]);
				printf("my buffer addr: %d %p\n", index, ctx->buffer_mr->addr + index * BUFFER_SIZE);
			}
			ctx->peer_bitmap_addr = ctx->k_exch[1]->bitmap.addr;
			ctx->peer_bitmap_rkey = ctx->k_exch[1]->bitmap.rkey;
			{
				printf("peer bitmap addr : %p\npeer bitmap rkey: %u\n", ctx->peer_bitmap_addr, ctx->peer_bitmap_rkey);
				std::cout << "-----------------------------" << std::endl;
			}
			post_send(id, IBV_WR_RDMA_READ); //query peer bitmap for update
		}
		break;
	}
	case IBV_WC_RDMA_WRITE:
	{
		log_info("IBV_WC_RDMA_WRITE\n");
		break;
	}
	case IBV_WC_RDMA_READ:
	{

		std::vector<int> available = recv_handle_bitmap(ctx);

		if (available.size() != 0)
		{
			for (auto &index : available)
			{
				//uint32_t size = *((uint32_t *)(ctx->buffer))/MAX_CONCURRENCY;
				//ctx->buffer[index][size+sizeof(uint32_t)]=0;
				//char *recv_data_ptr = ctx->buffer + index * BUFFER_SIZE;
				//void* _data = (void *)std::malloc(sizeof(char) * size + 1);

				//if (_data == nullptr)
				//{
				//	printf("fatal error in recv data malloc!!!!\n");
				//	exit(-1);
				//}

				//memset(_data, 0, MSG_SIZE);

				char *recv_data_ptr = ctx->buffer + index * BUFFER_SIZE;

				update_bitmap(ctx, index);

				static unsigned long long ccc = 0;
				if ((++ccc) % loop == 0)
				{
					//for example : 1536211279992922.Hello,World!random:1871857513,index9
					int index_ = 0;
					char *buf = (char *)recv_data_ptr;
					while (index_ < MSG_SIZE)
					{
						if ('.' == buf[index_])
						{
							buf[index_] = 0;
							break;
						}
						index_++;
					}
					uint64_t val = std::stoull(std::string(buf));
					buf[index_] = '.';
					while (index_ < MSG_SIZE)
					{
						if ('i' == buf[index_] && 'n' == buf[index_ + 1])
						{
							buf[index_ + 4] = 0;
							index_ += 5;
							break;
						}
						index_++;
					}
					buf += index_;
					int index_id = std::stoi(std::string(buf));
					*(buf - 1) = 'x';
					struct ibv_send_wr wr, *bad_wr = NULL;
					struct ibv_sge sge;

					memset(&wr, 0, sizeof(wr));

					wr.wr_id = 12345;
					wr.opcode = IBV_WR_SEND;
					wr.sg_list = &sge;
					wr.num_sge = 1;
					wr.send_flags = IBV_SEND_SIGNALED;

					ctx->k_exch[0]->md5 = val;
					ctx->k_exch[0]->id = index_id + 10000;

					sge.addr = (uintptr_t)(ctx->k_exch[0]);
					sge.length = sizeof(_key_exch);
					sge.lkey = ctx->k_exch_mr[0]->lkey;

					TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
					log_info("Recv data: %s\n", recv_data_ptr);
				}
				std::free((char *)_data);
			}
		}
		post_send(id, IBV_WR_RDMA_READ); //query peer bitmap for update
		break;
	}
	case IBV_WC_SEND:
	{
		//log_info("IBV_WC_SEND\n");
		//LOG(INFO) << "IBV_WC_SEND";
		break;
	}
	default:
		LOG(INFO) << wc->opcode;
		break;
	}
	return _data;
}

static void *send_tensor(struct rdma_cm_id *id, uint32_t index)
{
	struct context *ctx = (struct context *)id->context;

	std::string msg = "Hello, World : index " + std::to_string(index);
	/*encode msg_length and buffer*/
	uint32_t msg_len = msg.length();

	if ((msg_len + sizeof(uint32_t)) > BUFFER_SIZE || (MSG_SIZE > BUFFER_SIZE))
	{
		perror("fatal error, send msg length is too long\n");
		exit(-1);
	}

	char *_buff = ctx->buffer + index * BUFFER_SIZE;
	std::memcpy(_buff, (char *)(&msg_len), sizeof(uint32_t));
	_buff += sizeof(uint32_t);
	std::memcpy(_buff, msg.c_str(), msg_len);
	_write_remote(id, msg_len + sizeof(uint32_t), index, IBV_WR_RDMA_WRITE_WITH_IMM);
	log_info("send data: %s\n", msg.c_str());
	return NULL;
}

static void *write_tensor(struct rdma_cm_id *id, uint32_t index)
{
	struct context *ctx = (struct context *)id->context;

	//for example : 1536211279992922.Hello,World!random:1871857513,index9
	std::string msg = std::to_string(current_time()) + "." + "Hello,World!" + "random:" + std::to_string(rd()) + ",index" + std::to_string(index);
	uint32_t msg_len = msg.length();

	if ((msg_len > BUFFER_SIZE) || (MSG_SIZE > BUFFER_SIZE))
	{
		perror("fatal error, send msg length is too long\n");
		exit(-1);
	}

	char *_buff = ctx->buffer + index * BUFFER_SIZE;

	memset(_buff, 0, BUFFER_SIZE); //avoid dirty data

	std::memcpy(_buff, msg.c_str(), msg_len);

	//std::cout<< _buff <<std::endl;
	_write_remote(id, MSG_SIZE, index, IBV_WR_RDMA_WRITE);

	return NULL;
}

static std::vector<int> send_handle_bitmap(struct context *ctx)
{
	std::vector<int> available;
	for (int index = 0; index < MAX_CONCURRENCY; index++)
	{
		if (ctx->bitmap[0][index] == ctx->bitmap[1][index])
			available.push_back(index);
	}
	return available;
}

static void *concurrency_send_by_RDMA(struct rdma_cm_id *id, struct ibv_wc *wc, int &mem_used)
{
	//struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
	struct context *ctx = (struct context *)id->context;

	switch (wc->opcode)
	{
	case IBV_WC_RECV_RDMA_WITH_IMM:
	{ /*
		log_info("recv with IBV_WC_RECV_RDMA_WITH_IMM\n");
		//log_info("imm_data is %d\n", wc->imm_data);
		_post_receive(id, wc->imm_data);
		//send_tensor(id, wc->imm_data);
		*/
		break;
	}
	case IBV_WC_RECV:
	{
		//at start stage, read peer virtual memory info.
		if (MSG_MR == ctx->k_exch[1]->id)
		{
			log_info("recv MD5 is %llu\n", ctx->k_exch[1]->md5);
			for (int index = 0; index < MAX_CONCURRENCY; index++)
			{
				//reserved the (buffer)key info from server.
				ctx->peer_addr[index] = ctx->k_exch[1]->key_info[index].addr;
				ctx->peer_rkey[index] = ctx->k_exch[1]->key_info[index].rkey;
				struct sockaddr_in *client_addr = (struct sockaddr_in *)rdma_get_peer_addr(id);
				printf("server[%s,%d] to ", inet_ntoa(client_addr->sin_addr), client_addr->sin_port);
				printf("client buffer %d: %p ", index, ctx->peer_addr[index]);
				printf("my ach addr: %d %p\n", index, ctx->ack_mr->addr + index * sizeof(_ack_));
			}
			ctx->peer_bitmap_addr = ctx->k_exch[1]->bitmap.addr;
			ctx->peer_bitmap_rkey = ctx->k_exch[1]->bitmap.rkey;
			{
				printf("peer bitmap addr : %p\npeer bitmap rkey: %u\n", ctx->peer_bitmap_addr, ctx->peer_bitmap_rkey);
				std::cout << "-----------------------------" << std::endl;
			}
			/**send one tensor...**/
			//send_tensor(id, 0);
			post_send(id, IBV_WR_RDMA_READ); //read from peer bitmap
			mem_used++;
			{
				struct ibv_recv_wr wr, *bad_wr = NULL;
				struct ibv_sge sge;

				memset(&wr, 0, sizeof(wr));

				wr.wr_id = 12345;
				wr.sg_list = &sge;
				wr.num_sge = 1;

				sge.addr = (uintptr_t)(ctx->k_exch[1]);
				sge.length = sizeof(_key_exch);
				sge.lkey = ctx->k_exch_mr[1]->lkey;
				TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
			}
		}
		else if (ctx->k_exch[1]->id >= 10000)
		{
			uint64_t val = current_time();
			std::cout << "[index " << ctx->k_exch[1]->id - 10000 << " , RTT : " << val - ctx->k_exch[1]->md5 << " us]" << std::endl;
			std::cout << "-----------------------------" << std::endl;
			{
				struct ibv_recv_wr wr, *bad_wr = NULL;
				struct ibv_sge sge;

				memset(&wr, 0, sizeof(wr));

				wr.wr_id = 12345;
				wr.sg_list = &sge;
				wr.num_sge = 1;

				sge.addr = (uintptr_t)(ctx->k_exch[1]);
				sge.length = sizeof(_key_exch);
				sge.lkey = ctx->k_exch_mr[1]->lkey;
				TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
			}
		}
		break;
	}
	case IBV_WC_RDMA_WRITE:
	{
		//std::cout<<"IBV_WC_RDMA_READ"<<std::endl;
		//log_info("IBV_WC_RDMA_WRITE SUCCESS with id = %u\n", wc->wr_id);
		update_bitmap(ctx, wc->wr_id);
		static long long count = 0;
		if (first)
		{
			gettimeofday(&start_, NULL);
			//tstart = clock();
			first = false;
		}
		float delta = 0.00000000000001;
		//if ((++count) % (MAX_CONCURRENCY*10000) ==0)
		if ((++count) % loop == 0)
		{
#define netbyte 1000
			//clock_t tend = clock();
			gettimeofday(&now_, NULL);
			//float time_cost = (tend - tstart) / CLOCKS_PER_SEC;
			float time_cost = (now_.tv_usec - start_.tv_usec) / 1000000.0 + now_.tv_sec - start_.tv_sec;
			printf("[total time cost: %f s, total count = %d]\n", time_cost, count);
			log_info("[rate: %f bps, %f Kbps, %f Mbps, %f Gbps]\n",
					 8.0 * MSG_SIZE * count / time_cost,
					 8.0 * MSG_SIZE * count / netbyte / time_cost,
					 8.0 * MSG_SIZE * count / netbyte / netbyte / time_cost,
					 8.0 * MSG_SIZE * count / netbyte / netbyte / netbyte / time_cost);
		}
		if (count > 5000000 || count % 10000 == 0)
			printf("%ll", count);
		break;
	}
	case IBV_WC_RDMA_READ:
	{
		//std::cout<<"IBV_WC_RDMA_READ"<<std::endl;

		std::vector<int> available = send_handle_bitmap(ctx);

		if (available.size() != 0)
		{
			for (auto &index : available)
			{
				write_tensor(id, index);
			}
		}

		//std::cout << "\nsending thread will be blocked for 1 seconds" << std::endl;
		//std::this_thread::sleep_for(std::chrono::milliseconds(1));
		post_send(id, IBV_WR_RDMA_READ); //query peer bitmap for update
		break;
	}
	case IBV_WC_SEND:
	{
		//log_info("IBV_WC_SEND\n");
		break;
	}
	default:
		break;
	}
	return NULL;
}

static void *recv_poll_cq(void *_id)
{
	struct ibv_cq *cq = NULL;
	struct ibv_wc wc[MAX_CONCURRENCY * 2];
	struct rdma_cm_id *id = (rdma_cm_id *)_id;

	struct context *ctx = (struct context *)id->context;
	void *ev_ctx = NULL;

	while (true)
	{
		TEST_NZ(ibv_get_cq_event(ctx->comp_channel, &cq, &ev_ctx));
		ibv_ack_cq_events(cq, 1);
		TEST_NZ(ibv_req_notify_cq(cq, 0));

		int wc_num = ibv_poll_cq(cq, MAX_CONCURRENCY * 2, wc);
		if (wc_num < 0)
		{
			LOG(INFO) << "error in wc num: " << wc_num;
		}

		for (int index = 0; index < wc_num; index++)
		{
			if (wc[index].status == IBV_WC_SUCCESS)
			{
				/*****here to modified recv* wc---->wc[index]****/
				//printf("in receive poll cq\n");
				void *recv_data = nullptr;
				uint32_t recv_len;
				recv_data = concurrency_recv_by_RDMA(id, &wc[index], recv_len);
			}
			else
			{
				printf("\nwc = %s\n", ibv_wc_status_str(wc[index].status));
				rc_die("poll_cq: status is not IBV_WC_SUCCESS");
			}
		}
	}
	return NULL;
}

static void *send_poll_cq(void *_id)
{
	struct ibv_cq *cq = NULL;
	struct ibv_wc wc[MAX_CONCURRENCY * 2 + 2];
	struct rdma_cm_id *id = (rdma_cm_id *)_id;

	struct context *ctx = (struct context *)id->context;
	void *ev_ctx = NULL;

	int mem_used = 0;

	while (1)
	{
		TEST_NZ(ibv_get_cq_event(ctx->comp_channel, &cq, &ev_ctx));
		ibv_ack_cq_events(cq, 1);
		TEST_NZ(ibv_req_notify_cq(cq, 0));

		while (1)
		{
			int wc_num = ibv_poll_cq(cq, MAX_CONCURRENCY * 2 + 2, wc);

			if (wc_num < 0)
			{
				perror("fatal error in ibv_poll_cq, -1");
				exit(-1);
			}

			for (int index = 0; index < wc_num; index++)
			{
				if (wc[index].status == IBV_WC_SUCCESS)
				{
					concurrency_send_by_RDMA(id, &wc[index], mem_used);
				}
				else
				{
					printf("\nwc = %s\n", ibv_wc_status_str(wc[index].status));
					rc_die("poll_cq: status is not IBV_WC_SUCCESS");
				}
			}
		}

		if (mem_used && 0)
		{
			printf("mem_used : %d\n", mem_used);
			//struct rdma_cm_id *id = (struct rdma_cm_id *)((wc[index])->wr_id);
			//struct context *ctx = (struct context *)id->context;
			for (mem_used; mem_used < MAX_CONCURRENCY; mem_used++)
			{
				send_tensor(id, mem_used);
			} /*send used next buffer*/
		}
	}
	return NULL;
}

static struct ibv_pd *rc_get_pd(struct rdma_cm_id *id)
{
	struct context *ctx = (struct context *)id->context;
	return ctx->pd;
}

static void _build_params(struct rdma_conn_param *params)
{
	memset(params, 0, sizeof(*params));
	params->initiator_depth = params->responder_resources = 1;
	params->rnr_retry_count = 7; /* infinite retry */
	params->retry_count = 7;
}

static void _build_context(struct rdma_cm_id *id, bool is_server)
{
	struct context *s_ctx = (struct context *)malloc(sizeof(struct context));
	s_ctx->ibv_ctx = id->verbs;
	TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ibv_ctx));
	TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ibv_ctx));
	TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ibv_ctx, MAX_CONCURRENCY * 2 + 10, NULL, s_ctx->comp_channel, 0));
	TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));
	id->context = (void *)s_ctx;
	if (is_server)
	{
		TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, recv_poll_cq, (void *)id));
		id->context = (void *)s_ctx;
	}
}

static void _build_qp_attr(struct ibv_qp_init_attr *qp_attr, struct rdma_cm_id *id)
{
	struct context *ctx = (struct context *)id->context;
	memset(qp_attr, 0, sizeof(*qp_attr));
	qp_attr->send_cq = ctx->cq;
	qp_attr->recv_cq = ctx->cq;
	qp_attr->qp_type = IBV_QPT_RC;

	qp_attr->cap.max_send_wr = MAX_CONCURRENCY + 2 + 1;
	qp_attr->cap.max_recv_wr = MAX_CONCURRENCY + 2 + 1;
	qp_attr->cap.max_send_sge = 1;
	qp_attr->cap.max_recv_sge = 1;
}

static void _build_connection(struct rdma_cm_id *id, bool is_server)
{
	struct ibv_qp_init_attr qp_attr;
	_build_context(id, is_server);
	_build_qp_attr(&qp_attr, id);
	struct context *ctx = (struct context *)id->context;
	TEST_NZ(rdma_create_qp(id, ctx->pd, &qp_attr));
}

static void _on_pre_conn(struct rdma_cm_id *id)
{
	struct context *new_ctx = (struct context *)id->context;

	posix_memalign((void **)(&(new_ctx->buffer)), 4096 * 4096, BUFFER_SIZE * MAX_CONCURRENCY);
	TEST_Z(new_ctx->buffer_mr = ibv_reg_mr(rc_get_pd(id), new_ctx->buffer, BUFFER_SIZE * MAX_CONCURRENCY, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

	posix_memalign((void **)(&(new_ctx->ack)), 4096 * 4096, sizeof(_ack_) * MAX_CONCURRENCY);
	TEST_Z(new_ctx->ack_mr = ibv_reg_mr(rc_get_pd(id), new_ctx->ack,
										sizeof(_ack_) * MAX_CONCURRENCY, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

	log_info("register %d tx_buffer and rx_ack\n", MAX_CONCURRENCY);

	{
		posix_memalign((void **)(&(new_ctx->k_exch[0])), 4096 * 4096, sizeof(_key_exch));
		TEST_Z(new_ctx->k_exch_mr[0] = ibv_reg_mr(rc_get_pd(id), new_ctx->k_exch[0], sizeof(_key_exch), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

		posix_memalign((void **)(&(new_ctx->k_exch[1])), 4096 * 4096, sizeof(_key_exch));
		TEST_Z(new_ctx->k_exch_mr[1] = ibv_reg_mr(rc_get_pd(id), new_ctx->k_exch[1], sizeof(_key_exch), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
	}
	log_info("register rx_k_exch (index:0) and tx_k_exch (index:1)\n");

#ifdef _ENABLE_READ_
	for (int index = 0; index < 2; index++)
	{
		posix_memalign((void **)(&(new_ctx->bitmap[index])), 4096 * 4096, MAX_CONCURRENCY);
		TEST_Z(new_ctx->bitmap_mr[index] = ibv_reg_mr(rc_get_pd(id),
													  new_ctx->bitmap[index],
													  MAX_CONCURRENCY,
													  IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
		printf("bitmap %d :%p\n", index, new_ctx->bitmap_mr[index]->addr);
	}
	log_info("register bitmap (index:0 for remote read) and (index:1 for receive the peer data)\n");

	memset(new_ctx->bitmap[0], 0, MAX_CONCURRENCY);
	memset(new_ctx->bitmap[1], 0, MAX_CONCURRENCY);
#endif
	struct ibv_recv_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	sge.addr = (uintptr_t)(new_ctx->k_exch[1]);
	sge.length = sizeof(_key_exch);
	sge.lkey = new_ctx->k_exch_mr[1]->lkey;

	TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));

	/*
	for (uint32_t index = 0; index < MAX_CONCURRENCY; index++)
	{
		//log_info("post recv index : %u\n", index);
		_post_receive(id, index);
	}
*/
}

static void _on_connection(struct rdma_cm_id *id, bool is_server)
{
	struct context *new_ctx = (struct context *)id->context;

	int index = 0;

	new_ctx->k_exch[0]->id = MSG_MR;
	if (is_server)
		new_ctx->k_exch[0]->md5 = 6666;
	else
		new_ctx->k_exch[0]->md5 = 5555;

	log_info("k_exch md5 is %llu\n", new_ctx->k_exch[0]->md5);
	if (is_server)
	{
		for (index = 0; index < MAX_CONCURRENCY; index++)
		{
			new_ctx->k_exch[0]->key_info[index].addr = (uintptr_t)(new_ctx->buffer_mr->addr + (index * BUFFER_SIZE));
			new_ctx->k_exch[0]->key_info[index].rkey = (new_ctx->buffer_mr->rkey);
		}
	}
	else
	{
		for (index = 0; index < MAX_CONCURRENCY; index++)
		{
			new_ctx->k_exch[0]->key_info[index].addr = (uintptr_t)(new_ctx->ack_mr->addr + (index * sizeof(_ack_)));
			new_ctx->k_exch[0]->key_info[index].rkey = (new_ctx->ack_mr->rkey);
		}
	}
	new_ctx->k_exch[0]->bitmap.addr = (uintptr_t)(new_ctx->bitmap_mr[0]->addr);
	new_ctx->k_exch[0]->bitmap.rkey = new_ctx->bitmap_mr[0]->rkey;

	//send to myself info to peer
	{
		struct ibv_send_wr wr, *bad_wr = NULL;
		struct ibv_sge sge;

		memset(&wr, 0, sizeof(wr));

		wr.wr_id = (uintptr_t)id;
		wr.opcode = IBV_WR_SEND;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.send_flags = IBV_SEND_SIGNALED;

		sge.addr = (uintptr_t)(new_ctx->k_exch[0]);
		sge.length = sizeof(_key_exch);
		sge.lkey = new_ctx->k_exch_mr[0]->lkey;

		TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
	}
	log_info("share my registed mem rx_buffer for peer write to\n");
}

static void _on_disconnect(struct rdma_cm_id *id)
{
	struct context *new_ctx = (struct context *)id->context;

	ibv_dereg_mr(new_ctx->buffer_mr);
	ibv_dereg_mr(new_ctx->ack_mr);
	free(new_ctx->buffer);
	free(new_ctx->ack);

	{
		ibv_dereg_mr(new_ctx->k_exch_mr[0]);
		ibv_dereg_mr(new_ctx->k_exch_mr[1]);
		free(new_ctx->k_exch[0]);
		free(new_ctx->k_exch[1]);
	}
	free(new_ctx);
}

static void recv_RDMA(Adapter &rdma_adapter)
{
	struct rdma_cm_event *event = NULL;
	struct rdma_conn_param cm_params;
	int connecting_client_cnt = 0;
	int client_counts = 1;
	printf("server is inited done (RDMA), waiting for %d client connecting....:)\n", client_counts);
	_build_params(&cm_params);

	while (rdma_get_cm_event(rdma_adapter.event_channel, &event) == 0)
	{
		struct rdma_cm_event event_copy;

		memcpy(&event_copy, event, sizeof(*event));
		rdma_ack_cm_event(event);

		if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST)
		{
			_build_connection(event_copy.id, IS_SERVER);
			_on_pre_conn(event_copy.id);
			TEST_NZ(rdma_accept(event_copy.id, &cm_params));
		}
		else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
		{
			_on_connection(event_copy.id, true);
			rdma_adapter.recv_rdma_cm_id.push_back(event_copy.id);

			struct sockaddr_in *client_addr = (struct sockaddr_in *)rdma_get_peer_addr(event_copy.id);
			printf("client[%s,%d] is connecting (RDMA) now... \n", inet_ntoa(client_addr->sin_addr), client_addr->sin_port);
			connecting_client_cnt++;
			if (connecting_client_cnt == client_counts)
				break;
		}
		else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED)
		{
			rdma_destroy_qp(event_copy.id);
			_on_disconnect(event_copy.id);
			rdma_destroy_id(event_copy.id);
			connecting_client_cnt--;
			if (connecting_client_cnt == 0)
				break;
		}
		else
		{
			rc_die("unknown event server\n");
		}
	}
	printf("%d clients have connected to my node (RDMA), ready to receiving loops\n", client_counts);

	while (1)
	{
		//std::this_thread::sleep_for(std::chrono::seconds(10));
	}

	printf("RDMA recv loops exit now...\n");
	return;
}

static void rdma_server_init(Adapter &rdma_adapter)
{
	int init_loops = 0;
	struct sockaddr_in sin;
	printf("init a server (RDMA)....\n");
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;						//ipv4
	sin.sin_port = htons(rdma_adapter.server_port); //server listen default ports:1234
	sin.sin_addr.s_addr = INADDR_ANY;				//listen any connects

	TEST_Z(rdma_adapter.event_channel = rdma_create_event_channel());
	TEST_NZ(rdma_create_id(rdma_adapter.event_channel, &rdma_adapter.listener, NULL, RDMA_PS_TCP));

	while (rdma_bind_addr(rdma_adapter.listener, (struct sockaddr *)&sin))
	{
		std::cerr << "server init failed (RDMA): error in bind socket, will try it again in 2 seconds..." << std::endl;
		if (init_loops > 10)
		{
			rdma_destroy_id(rdma_adapter.listener);
			rdma_destroy_event_channel(rdma_adapter.event_channel);
			exit(-1);
		}
		std::this_thread::sleep_for(std::chrono::seconds(2));
		init_loops++;
	}

	int client_counts = 1;
	if (rdma_listen(rdma_adapter.listener, client_counts))
	{
		std::cerr << "server init failed (RDMA): error in server listening" << std::endl;
		rdma_destroy_id(rdma_adapter.listener);
		rdma_destroy_event_channel(rdma_adapter.event_channel);
		exit(-1);
	}
	recv_RDMA(rdma_adapter);

	std::this_thread::sleep_for(std::chrono::seconds(1));
	return;
}

static void rdma_client_init(Adapter &rdma_adapter)
{
	std::cout << "client inited (RDMA) start" << std::endl;
	for (size_t index = 0; index < 1; index++)
	{
		struct rdma_cm_id *conn = NULL;
		struct rdma_event_channel *ec = NULL;
		std::string local_eth = rdma_adapter.client_ip; /*get each lev ip*/
		struct sockaddr_in ser_in, local_in;			/*server ip and local ip*/
		int connect_count = 0;
		memset(&ser_in, 0, sizeof(ser_in));
		memset(&local_in, 0, sizeof(local_in));

		/*bind remote socket*/
		ser_in.sin_family = AF_INET;
		ser_in.sin_port = htons(rdma_adapter.server_port); /*connect to public port remote*/
		inet_pton(AF_INET, rdma_adapter.server_ip.c_str(), &ser_in.sin_addr);

		/*bind local part*/
		local_in.sin_family = AF_INET;
		std::cout << local_eth.c_str() << "----->" << rdma_adapter.server_ip.c_str() << std::endl;
		inet_pton(AF_INET, local_eth.c_str(), &local_in.sin_addr);

		TEST_Z(ec = rdma_create_event_channel());
		TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
		TEST_NZ(rdma_resolve_addr(conn, (struct sockaddr *)(&local_in), (struct sockaddr *)(&ser_in), TIMEOUT_IN_MS));

		struct rdma_cm_event *event = NULL;
		struct rdma_conn_param cm_params;

		_build_params(&cm_params);
		while (rdma_get_cm_event(ec, &event) == 0)
		{
			struct rdma_cm_event event_copy;
			memcpy(&event_copy, event, sizeof(*event));
			rdma_ack_cm_event(event);
			if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED)
			{
				_build_connection(event_copy.id, IS_CLIENT);
				_on_pre_conn(event_copy.id);
				TEST_NZ(rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS));
			}
			else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED)
			{
				TEST_NZ(rdma_connect(event_copy.id, &cm_params));
			}
			else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED)
			{
				struct context *ctx = (struct context *)event_copy.id->context;
				TEST_NZ(pthread_create(&ctx->cq_poller_thread, NULL, send_poll_cq, (void *)(event_copy.id)));
				std::cout << local_eth << " has connected to server[ " << rdma_adapter.server_ip << " , " << rdma_adapter.server_port << " ]" << std::endl;

				{
					_on_connection(event_copy.id, false);
				}
				break;
			}
			else if (event_copy.event == RDMA_CM_EVENT_REJECTED)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				connect_count++;
				_on_disconnect(event_copy.id);
				rdma_destroy_qp(event_copy.id);
				rdma_destroy_id(event_copy.id);
				rdma_destroy_event_channel(ec);
				if (connect_count > 10 * 600) /*after 600 seconds, it will exit.*/
				{
					std::cerr << 600 << "seconds is passed, error in connect to server" << rdma_adapter.server_ip << ", check your network condition" << std::endl;
					exit(-1);
				}
				else
				{
					TEST_Z(ec = rdma_create_event_channel());
					TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
					TEST_NZ(rdma_resolve_addr(conn, (struct sockaddr *)(&local_in), (struct sockaddr *)(&ser_in), TIMEOUT_IN_MS));
				}
			}
			else
			{
				printf("event = %d\n", event_copy.event);
				rc_die("unknown event client\n");
			}
		}
	}
	std::cout << "client inited done" << std::endl;
	while (1)
	{
		//std::cout << "main thread sleep for 10 seconds" << std::endl;
		//std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}

void help(void)
{
	std::cout << "Useage:\n";
	std::cout << "For Server: ./rdma -s server_ip\n";
	std::cout << "For Client: ./rdma -s server_ip -c client_ip" << std::endl;
	return;
}

int main(int argc, char const *argv[])
{
	google::InitGoogleLogging(argv[0]);
	google::LogToStderr();
	Adapter rdma_adapter;
	switch (argc)
	{
	case 3:
		rdma_adapter.set_server_ip(argv[2]);
		rdma_server_init(rdma_adapter);
	case 5:
		rdma_adapter.set_server_ip(argv[2]);
		rdma_adapter.set_client_ip(argv[4]);
		rdma_client_init(rdma_adapter);
	default:
		help();
		break;
	}
	google::ShutdownGoogleLogging();
	return 0;
}
