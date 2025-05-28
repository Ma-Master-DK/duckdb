#include "duckdb/storage/queue_pool.hpp"
#include <libxnvme.h>

namespace duckdb {

QueueWrapper::QueueWrapper(xnvme_dev *dev, uint16_t qdepth) {
	this->qdepth = qdepth;
	int ret = xnvme_queue_init(dev, qdepth, 0, &queue);
	if (ret) {
		xnvme_cli_perr("xnvme_queue_init()", errno);
		return;
	}
	xnvme_queue_set_cb(queue, QueueWrapper::cb_func, &args);
}

void QueueWrapper::Poke() {
	xnvme_queue_poke(queue, 0);
}

int QueueWrapper::Drain() {
	auto res = xnvme_queue_drain(queue);
	return res;
}

void QueueWrapper::Close() {
	xnvme_queue_term(queue);
}

int QueueWrapper::SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

submit:
	int err = xnvme_nvm_read(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
	switch (err) {
	case 0:
		args.submitted++;
		break;
	case -EBUSY:
	case -EAGAIN:
		this->Poke();
		goto submit;
	default:
		xnvme_cli_perr("xnvme_nvm_read()", errno);
		xnvme_queue_put_cmd_ctx(queue, ctx);
		break;
	}

	return err;
}

int QueueWrapper::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

submit:
	int err = xnvme_nvm_write(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
	switch (err) {
	case 0:
		args.submitted++;
		break;
	case -EBUSY:
	case -EAGAIN:
		this->Poke();
		goto submit;
	default:
		xnvme_cli_perr("xnvme_nvm_write()", errno);
		xnvme_queue_put_cmd_ctx(queue, ctx);
		break;
	}

	return err;
}

} // namespace duckdb
