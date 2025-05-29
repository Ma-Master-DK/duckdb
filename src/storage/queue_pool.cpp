#include "duckdb/storage/queue_pool.hpp"
#include "duckdb/common/helper.hpp"
#include <iostream>
#include <libxnvme.h>

namespace duckdb {

QueuePool::QueuePool(struct xnvme_dev *dev, int pool_size, uint16_t qdepth) {
	for (int i = 0; i < pool_size; ++i) {
		queues.push_back(make_uniq<QueueWrapper>(dev, qdepth, i));
	}
}

void QueuePool::Close() {
	for (auto &qwrap_ptr : queues) {
		qwrap_ptr->Close();
	}
}

void QueuePool::Sync() {
	for (auto &qwrap_ptr : queues) {
		qwrap_ptr->Sync();
	}
}

QueueWrapper *QueuePool::GetQueue() {
	while (true) {
		for (auto &qp : queues) {
			auto &q = *qp;
			if (q.TryLock()) {
				return &q;
			} else {
				q.Poke();
			}
		}
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

bool QueueWrapper::TryLock() {
	if (queue && mtx.try_lock()) {
		return true;
	}
	return false;
}

int QueueWrapper::GetID() {
	return id;
}

void QueueWrapper::Poke() {
	if (queue) {
		mtx.lock();
		xnvme_queue_poke(queue, 0);
		mtx.unlock();
	}
}

void QueueWrapper::Sync() {
	if (queue) {
		mtx.lock();
		Drain();
		mtx.unlock();
	}
}

int QueueWrapper::Drain() {
	if (queue) {
		auto res = xnvme_queue_drain(queue);
		return res;
	}

	return 0;
}

void QueueWrapper::Close() {
	if (queue) {
		mtx.lock();
		xnvme_queue_term(queue);
		queue = nullptr;
		mtx.unlock();
		return;
	}
}

int QueueWrapper::SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	if (queue) {
		struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

		int err = xnvme_nvm_read(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
		if (err == 0) {
			args.submitted++;
		}

		return err;
	}

	return -1;
}

int QueueWrapper::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	if (queue) {
		struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

		int err = xnvme_nvm_write(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
		if (err == 0) {
			args.submitted++;
		}

		return err;
	}

	return -1;
}

} // namespace duckdb
