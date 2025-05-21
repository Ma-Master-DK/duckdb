#pragma once

#include <cstdint>
#include <libxnvme.h>
#include <mutex>
#include <vector>
#include "duckdb/common/helper.hpp"

namespace duckdb {

struct cb_args {
	uint32_t submitted = 0;
	uint32_t completed = 0;
	uint32_t inflight = 0;
};

class QueueWrapper {
private:
	struct xnvme_queue *queue;
	std::mutex mtx;
	int id;
	struct cb_args args;
	uint32_t qdepth;

public:
	QueueWrapper(xnvme_dev *dev, uint16_t qdepth, int id);
	void Release();
	bool TryLock();
	int SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	int SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	int GetID();
	int Drain();
	void Close();
	void Poke();
	~QueueWrapper();

protected:
	static void cb_func(struct xnvme_cmd_ctx *ctx, void *cb_arg) {
		struct cb_args *cb_args = static_cast<struct cb_args *>(cb_arg);
		cb_args->completed++;
		cb_args->inflight--;

		xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
	}
};

class QueuePool {
public:
	QueuePool(struct xnvme_dev *dev, int pool_size, uint16_t qdepth);
	~QueuePool() {};
	QueueWrapper *GetAvailableQueue();
	void Close();
	void Sync();

private:
	static int nr_of_queues;
	std::vector<unique_ptr<QueueWrapper>> queues;
};

} // namespace duckdb
