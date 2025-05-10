#include "duckdb/storage/queue_pool.hpp"
#include "duckdb/common/helper.hpp"
#include <iostream>
#include <libxnvme.h>

namespace duckdb {

QueuePool::QueuePool(struct xnvme_dev *dev, int pool_size, uint16_t qdepth) {
	for (int i = 0; i < pool_size; ++i) {
		// struct xnvme_queue *q;
		// int ret = xnvme_queue_init(dev, qdepth, 0, &q);
		// if (ret) {
		// 	xnvme_cli_perr("xnvme_queue_init()", errno);
		// 	return;
		// }
		queues.push_back(make_uniq<QueueWrapper>(dev, qdepth, i));
	}
}

QueueWrapper *QueuePool::GetAvailableQueue() {
	for (auto &qwrap_ptr : queues) {
		auto &qwrap = *qwrap_ptr;
		if (qwrap.TryLock()) {
			std::cout << "Got queue number: " << qwrap.GetID() << "\n";
			return &qwrap;
		}
	}
	return nullptr;
}

QueueWrapper::QueueWrapper(xnvme_dev *dev, uint16_t qdepth, int id) {
	this->id = id;
	int ret = xnvme_queue_init(dev, qdepth, 0, &queue);
	if (ret) {
		xnvme_cli_perr("xnvme_queue_init()", errno);
		return;
	}
	xnvme_queue_set_cb(queue, QueueWrapper::cb_func, &args);
}

void QueueWrapper::Release() {
	mtx.unlock();
	std::cout << "Released queue number: " << id << "\n";
}

bool QueueWrapper::TryLock() {
	return mtx.try_lock();
}

int QueueWrapper::GetID() {
	return id;
}

int QueueWrapper::Drain() {
	return xnvme_queue_drain(queue);
}

int QueueWrapper::SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint64_t amount, char *payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
	int err;

submit:
	err = xnvme_nvm_read(ctx, xnvme_dev_get_nsid(dev), lba_location, 0, payload, nullptr);
	switch (err) {
	case 0:
		submitted += 1;
		break;

	case -EBUSY:
	case -EAGAIN:
		xnvme_queue_poke(queue, 0);
		goto submit;

	default:
		xnvme_cli_perr("xnvme_nvm_read()", err);
	}
	return err;
}
int QueueWrapper::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint64_t amount, char *payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
	int err;

submit:
	err = xnvme_nvm_write(ctx, xnvme_dev_get_nsid(dev), lba_location, 0, payload, nullptr);
	switch (err) {
	case 0:
		submitted += 1;
		break;

	case -EBUSY:
	case -EAGAIN:
		xnvme_queue_poke(queue, 0);
		goto submit;

	default:
		xnvme_cli_perr("xnvme_nvm_read()", err);
	}
	return err;
}

QueueWrapper::~QueueWrapper() {
	if (queue) {
		xnvme_queue_term(queue);
	}
}

} // namespace duckdb
