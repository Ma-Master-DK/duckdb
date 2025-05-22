#include "duckdb/common/file_buffer.hpp"

#include "duckdb/common/allocator.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/storage/queue_pool.hpp"
#include <cstdint>
#include <cstring>

#include <iostream>
#include <libxnvme.h>
#include <libxnvme_nvm.h>
#include <thread>

namespace duckdb {

FileBuffer::FileBuffer(Allocator &allocator, xnvme_dev *dev, FileBufferType type, uint64_t user_size)
    : allocator(allocator), dev(dev), type(type) {
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
	dev = source.dev;

	source.Init();
}

void FileBuffer::Close() {
	if (!internal_buffer) {
		return;
	}
	xnvme_buf_free(dev, internal_buffer);
	internal_buffer = nullptr;
}

void FileBuffer::CloseWithDev() {
	if (!internal_buffer) {
		return;
	}
	xnvme_buf_free(dev, internal_buffer);
	internal_buffer = nullptr;
	xnvme_dev_close(dev);
}

FileBuffer::~FileBuffer() {
	Close();
}

void FileBuffer::ReallocBuffer(idx_t new_size) {
	data_ptr_t new_buffer;
	if (internal_buffer) {
		new_buffer = static_cast<data_ptr_t>(xnvme_buf_alloc(dev, new_size));
		xnvme_buf_free(dev, internal_buffer);
	} else {
		new_buffer = static_cast<data_ptr_t>(xnvme_buf_alloc(dev, new_size));
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
	int err = 0;
	int ret = 0;

	// extract meta data from device
	auto geo = xnvme_dev_get_geo(dev);
	auto lba_size = geo->nbytes;
	auto lba_location = location / lba_size;
	auto lbas_to_write = (internal_size / lba_size) - 1;

	qwrap = qpool.SubmitRead(dev, lba_location, (uint16_t)lbas_to_write, internal_buffer);
	if (err) {
		goto exit;
	}

	// all is submitted
	// ret = qwrap->Drain();
	if (ret < 0) {
		xnvme_cli_perr("xnvme_queue_drain()", ret);
		goto exit;
	}

exit:
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
	int err = 0;
	int ret = 0;

	// extract meta data from device
	auto geo = xnvme_dev_get_geo(dev);
	auto lba_size = geo->nbytes;
	auto lba_location = location / lba_size;
	auto lbas_to_write = (internal_size / lba_size) - 1;

	qwrap = qpool.SubmitWrite(dev, lba_location, (uint16_t)lbas_to_write, internal_buffer);
	if (err) {
		goto exit;
	}

	// ret = qwrap->Drain();
	if (ret < 0) {
		xnvme_cli_perr("xnvme_queue_drain()", ret);
		goto exit;
	}

exit:
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
