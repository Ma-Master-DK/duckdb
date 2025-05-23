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

void QueuePool::SubmitRead(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	while (true) {
		for (auto &qwrap_ptr : queues) {
			if (qwrap_ptr->SubmitRead(dev, lba_location, amount, payload) == 0) {
				return;
			}
		}
	}
}

void QueuePool::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	while (true) {
		for (auto &qwrap_ptr : queues) {
			if (qwrap_ptr->SubmitWrite(dev, lba_location, amount, payload) == 0) {
				return;
			} else {
				qwrap_ptr->Poke();
			}
		}
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

void QueueWrapper::Poke() {
	if (queue) {
		mtx.lock();
		xnvme_queue_poke(queue, 0);
		mtx.unlock();
	}
}

void QueueWrapper::Sync() {
	if (queue) {
		Drain();
	}
}

int QueueWrapper::Drain() {
	if (queue) {
		mtx.lock();
		auto res = xnvme_queue_drain(queue);
		mtx.unlock();
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
		if (TryLock()) {
			struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

			int err = xnvme_nvm_read(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
			if (err == 0) {
				args.submitted++;
				args.inflight++;
			}

			mtx.unlock();
			return err;
		} else {
			return -1;
		}
	}

	return -1;
}

int QueueWrapper::SubmitWrite(xnvme_dev *dev, uint64_t lba_location, uint16_t amount, data_ptr_t payload) {
	if (queue) {
		if (TryLock()) {
			struct xnvme_cmd_ctx *ctx = xnvme_queue_get_cmd_ctx(queue);

			int err = xnvme_nvm_write(ctx, xnvme_dev_get_nsid(dev), lba_location, amount, payload, nullptr);
			if (err == 0) {
				args.submitted++;
				args.inflight++;
			}

			mtx.unlock();
			return err;

		} else {
			return -1;
		}
	}

	return -1;
}

} // namespace duckdb
