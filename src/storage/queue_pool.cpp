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
	while (true) {
		for (auto &qwrap_ptr : queues) {
			auto &qwrap = *qwrap_ptr;
			if (qwrap.TryLock()) {
				return &qwrap;
			}
		}

		for (auto &qwrap_ptr : queues) {
			auto &qwrap = *qwrap_ptr;
			qwrap.Poke();
		}
	}
	return nullptr;
}

void QueuePool::Close() {
	for (auto &qwrap_ptr : queues) {
		auto &qwrap = *qwrap_ptr;
		qwrap.Close();
	}
}

void QueuePool::Sync() {
	for (auto &qwrap_ptr : queues) {
		auto &qwrap = *qwrap_ptr;
		qwrap.Drain();
	}
}

QueueWrapper::QueueWrapper(xnvme_dev *dev, uint16_t qdepth, int id) {
	this->id = id;
	this->qdepth = qdepth;
	int ret = xnvme_queue_init(dev, qdepth, 0, &queue);
	if (ret) {
		xnvme_cli_perr("xnvme_queue_init()", errno);
		return;
	}
	xnvme_queue_set_cb(queue, QueueWrapper::cb_func, &args);
}

void QueueWrapper::Release() {
	mtx.unlock();
}

bool QueueWrapper::TryLock() {
	if (mtx.try_lock()) {
		if (args.inflight < qdepth) {
			return true;
		} else {
			mtx.unlock();
			return false;
		}
	}
	return false;
}

int QueueWrapper::GetID() {
	return id;
}

int QueueWrapper::Drain() {
	mtx.lock();
	int err = xnvme_queue_drain(queue);
	mtx.unlock();
	return err;
}

void QueueWrapper::Poke() {
	mtx.lock();
	xnvme_queue_poke(queue, 0);
	mtx.unlock();
}

int QueueWrapper::SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
	int err;

submit:
	err = xnvme_nvm_read(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
	switch (err) {
	case 0:
		args.submitted++;
		args.inflight++;
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
int QueueWrapper::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);
	int err;

submit:
	err = xnvme_nvm_write(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
	switch (err) {
	case 0:
		args.submitted++;
		args.inflight++;
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

void QueueWrapper::Close() {
	if (queue) {
		mtx.lock();
		xnvme_queue_term(queue);
		mtx.unlock();
		queue = nullptr;
	}
}

QueueWrapper::~QueueWrapper() {
	if (queue) {
		xnvme_queue_term(queue);
		queue = nullptr;
	}
}

} // namespace duckdb
