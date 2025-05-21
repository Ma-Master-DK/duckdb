#include "duckdb/common/file_buffer.hpp"

#include "duckdb/common/allocator.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/storage/queue_pool.hpp"
#include <cstdint>
#include <cstring>

#include <libxnvme.h>
#include <libxnvme_nvm.h>
#include <thread>

namespace duckdb {

FileBuffer::FileBuffer(Allocator &allocator, FileBufferType type, uint64_t user_size)
    : allocator(allocator), type(type) {
	Init();
	if (user_size) {
		Resize(user_size);
	}
}

void FileBuffer::Init() {
	buffer = nullptr;
	size = 0;
	internal_buffer = nullptr;
	internal_size = 0;
}

FileBuffer::FileBuffer(FileBuffer &source, FileBufferType type_p) : allocator(source.allocator), type(type_p) {
	// take over the structures of the source buffer
	buffer = source.buffer;
	size = source.size;
	internal_buffer = source.internal_buffer;
	internal_size = source.internal_size;

	source.Init();
}

FileBuffer::~FileBuffer() {
	if (!internal_buffer) {
		return;
	}
	allocator.FreeData(internal_buffer, internal_size);
}

void FileBuffer::ReallocBuffer(idx_t new_size) {
	data_ptr_t new_buffer;
	if (internal_buffer) {
		new_buffer = allocator.ReallocateData(internal_buffer, internal_size, new_size);
	} else {
		new_buffer = allocator.AllocateData(new_size);
	}

	// FIXME: should we throw one of our exceptions here?
	if (!new_buffer) {
		throw std::bad_alloc();
	}
	internal_buffer = new_buffer;
	internal_size = new_size;

	// The caller must update these.
	buffer = nullptr;
	size = 0;
}

FileBuffer::MemoryRequirement FileBuffer::CalculateMemory(uint64_t user_size) {
	FileBuffer::MemoryRequirement result;

	if (type == FileBufferType::TINY_BUFFER) {
		// We never do IO on tiny buffers, so there's no need to add a header or sector-align.
		result.header_size = 0;
		result.alloc_size = user_size;
	} else {
		result.header_size = Storage::DEFAULT_BLOCK_HEADER_SIZE;
		result.alloc_size = AlignValue<idx_t, Storage::SECTOR_SIZE>(result.header_size + user_size);
	}
	return result;
}

void FileBuffer::Resize(uint64_t new_size) {
	auto req = CalculateMemory(new_size);
	ReallocBuffer(req.alloc_size);

	if (new_size > 0) {
		buffer = internal_buffer + req.header_size;
		size = internal_size - req.header_size;
	}
}

void FileBuffer::Read(FileHandle &handle, uint64_t location) {
	D_ASSERT(type != FileBufferType::TINY_BUFFER);
	handle.Read(internal_buffer, internal_size, location);
}

void FileBuffer::Read(xnvme_dev *dev, uint64_t location, QueuePool &qpool) {
	D_ASSERT(type != FileBufferType::TINY_BUFFER);

	// misc variables for xnvme use
	QueueWrapper *qwrap;
	while (!(qwrap = qpool.GetAvailableQueue())) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	int err = 0;
	int ret = 0;

	// extract meta data from device
	auto geo = xnvme_dev_get_geo(dev);
	auto lba_size = geo->nbytes;
	auto lba_location = location / lba_size;
	auto mdts_size = geo->mdts_nbytes;
	auto lbas_pr_mdts = mdts_size / lba_size;
	uint64_t submissions = 1 + ((internal_size - 1) / mdts_size);

	// allocate dma buffer
	// auto nvme_buf_size = lba_size * lba_amount;
	// char *nvme_buf = static_cast<char *>(xnvme_buf_alloc(dev, nvme_buf_size));
	// if (!nvme_buf) {
	// 	xnvme_cli_perr("xnvme_buf_alloc()", errno);
	// 	goto exit;
	// }

	// clear buffer before writing to it, maybe not necessary
	// xnvme_buf_clear(nvme_buf, nvme_buf_size);

	// read one lba block at a time, sequentially, into dma buffer
	for (uint64_t i = 0; i < submissions; i++) {
		auto offset = i * mdts_size;
		auto *payload = internal_buffer + offset;
		auto lbas = internal_size - offset >= mdts_size ? lbas_pr_mdts : (internal_size - offset) / lba_size;

		err = qwrap->SubmitRead(dev, lba_location + i * lbas_pr_mdts, (uint16_t)lbas - 1, payload);
		if (err) {
			goto exit;
		}
	}

	// all is submitted, now wait for completion
	ret = qwrap->Drain();
	qwrap->Release();
	if (ret < 0) {
		xnvme_cli_perr("xnvme_queue_drain()", ret);
		goto exit;
	}

	// transfer data in dma buffer to duckdb buffer
	// memcpy(internal_buffer, nvme_buf, nvme_buf_size);

exit:
	// xnvme_buf_free(dev, nvme_buf);
	return;
}

void FileBuffer::Write(FileHandle &handle, uint64_t location) {
	D_ASSERT(type != FileBufferType::TINY_BUFFER);
	handle.Write(internal_buffer, internal_size, location);
}

void FileBuffer::Write(xnvme_dev *dev, uint64_t location, QueuePool &qpool) {
	D_ASSERT(type != FileBufferType::TINY_BUFFER);

	// misc variables for xnvme use
	QueueWrapper *qwrap;
	while (!(qwrap = qpool.GetAvailableQueue())) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	int err = 0;
	int ret = 0;

	// extract meta data from device
	auto geo = xnvme_dev_get_geo(dev);
	auto lba_size = geo->nbytes;
	auto lba_location = location / lba_size;
	auto mdts_size = geo->mdts_nbytes;
	auto lbas_pr_mdts = mdts_size / lba_size;
	uint64_t submissions = 1 + ((internal_size - 1) / mdts_size);

	// allocate dma buffer
	// auto nvme_buf_size = lba_size * lba_amount;
	// char *nvme_buf = static_cast<char *>(xnvme_buf_alloc(dev, nvme_buf_size));
	// if (!nvme_buf) {
	// 	xnvme_cli_perr("xnvme_buf_alloc()", errno);
	// 	goto exit;
	// }

	// transfer data from duckdb buffer to dma buffer
	// memcpy(nvme_buf, internal_buffer, nvme_buf_size);

	// write one lba block at a time, sequentially, to disk from dma buffer
	for (uint64_t i = 0; i < submissions; i++) {
		auto offset = i * mdts_size;
		auto *payload = internal_buffer + offset;
		auto lbas = internal_size - offset >= mdts_size ? lbas_pr_mdts : (internal_size - offset) / lba_size;

		err = qwrap->SubmitWrite(dev, lba_location + i * lbas_pr_mdts, (uint16_t)lbas - 1, payload);
		if (err) {
			goto exit;
		}
	}

	// all is submitted, DO NOT DRAIN
	ret = qwrap->Drain();
	qwrap->Release();
	if (ret < 0) {
		xnvme_cli_perr("xnvme_queue_drain()", ret);
		goto exit;
	}

	// transfer data in dma buffer to duckdb buffer
	// memcpy(internal_buffer, nvme_buf, nvme_buf_size);

exit:
	// xnvme_buf_free(dev, nvme_buf);
	return;
}

void FileBuffer::Clear() {
	memset(internal_buffer, 0, internal_size);
}

void FileBuffer::Initialize(DebugInitialize initialize) {
	if (initialize == DebugInitialize::NO_INITIALIZE) {
		return;
	}
	uint8_t value = initialize == DebugInitialize::DEBUG_ZERO_INITIALIZE ? 0 : 0xFF;
	memset(internal_buffer, value, internal_size);
}

} // namespace duckdb
