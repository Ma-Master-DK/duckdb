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

	bool TryLock();
	void Unlock() {
		mtx.unlock();
	}

	int GetID();
	void Poke();
	void Sync();
	int Drain();
	void Close();

	int SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	int SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);

protected:
	static void cb_func(struct xnvme_cmd_ctx *ctx, void *cb_arg) {
		auto err = xnvme_cmd_ctx_cpl_status(ctx);
		if (err) {
			xnvme_cli_perr("Command did not complete successfully", err);
			xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
		} else {
			xnvme_cli_pinf("Command completed succesfully");
			struct cb_args *cb_args = static_cast<struct cb_args *>(cb_arg);
			cb_args->completed++;
		}

		xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
	}
};

class QueuePool {
public:
	QueuePool(struct xnvme_dev *dev, int pool_size, uint16_t qdepth);
	~QueuePool() {
		Close();
	};

	QueueWrapper *GetQueue();

	void Sync();

private:
	void Close();
	static int nr_of_queues;
	std::vector<unique_ptr<QueueWrapper>> queues;
};

} // namespace duckdb
