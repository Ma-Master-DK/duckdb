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
	int id;
	std::mutex mtx;
	uint32_t qdepth;
	struct xnvme_queue *queue;
	struct cb_args args;

public:
	QueueWrapper(xnvme_dev *dev, uint16_t qdepth, int id);
	~QueueWrapper() {
		Close();
	};

	int GetID();
	void Poke();
	void Sync();
	int Drain();
	void Close();

	int SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	int SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);

protected:
	bool TryLock();
	void Release();

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
	~QueuePool() {
		Close();
	};

	QueueWrapper *SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	QueueWrapper *SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);

	void Sync();

private:
	void Close();
	static int nr_of_queues;
	std::vector<unique_ptr<QueueWrapper>> queues;
};

} // namespace duckdb
