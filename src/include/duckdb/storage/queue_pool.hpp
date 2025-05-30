#pragma once

#include <cstdint>
#include <iostream>
#include <libxnvme.h>
#include <mutex>
#include "duckdb/common/helper.hpp"

namespace duckdb {

struct cb_args {
	uint32_t submitted = 0;
	uint32_t completed = 0;
};

class QueueWrapper {
private:
	uint32_t qdepth;
	struct xnvme_queue *queue;
	struct cb_args args;
	std::mutex mtx;

public:
	QueueWrapper(xnvme_dev *dev, uint16_t qdepth);
	~QueueWrapper() {
		Close();
	};

	void Poke();
	int Drain();
	void Close();

	int SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);
	int SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload);

	bool CheckCompletion() {
		return args.completed == args.submitted;
	}

protected:
	static void cb_func(struct xnvme_cmd_ctx *ctx, void *cb_arg) {
		if (xnvme_cmd_ctx_cpl_status(ctx)) {
			xnvme_cli_pinf("Command did not complete succesfully");
			xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
		} else {
			// xnvme_cli_pinf("Command completed succesfully");
			struct cb_args *cb_args = static_cast<struct cb_args *>(cb_arg);
			cb_args->completed++;
		}

		xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
	}
};

} // namespace duckdb
