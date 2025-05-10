#pragma once

#include <cstdint>
#include <libxnvme.h>
#include <mutex>
#include <vector>
#include "duckdb/common/helper.hpp"

namespace duckdb {

struct cb_args {
	uint32_t submitted;
	uint32_t completed;
};

class QueueWrapper {
private:
	struct xnvme_queue *queue;
	std::mutex mtx;
	int id;
	int submitted;
	struct cb_args args;

public:
	QueueWrapper(xnvme_dev *dev, uint16_t qdepth, int id);
	void Release();
	bool TryLock();
	int SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint64_t amount, char *payload);
	int SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint64_t amount, char *payload);
	int GetID();
	int Drain();
	~QueueWrapper();

protected:
	static void cb_func(struct xnvme_cmd_ctx *ctx, void *cb_arg) {
		struct cb_args *cb_args = static_cast<struct cb_args *>(cb_arg);
		cb_args->completed += 1;

		xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
	}
};

class QueuePool {
public:
	QueuePool(struct xnvme_dev *dev, int pool_size, uint16_t qdepth);
	~QueuePool() {};
	QueueWrapper *GetAvailableQueue();
	void ReleaseQueue(QueueWrapper *qwrap);

private:
	static int nr_of_queues;
	std::vector<unique_ptr<QueueWrapper>> queues;
};

} // namespace duckdb
