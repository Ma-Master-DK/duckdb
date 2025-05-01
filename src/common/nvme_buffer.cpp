#include "duckdb/common/nvme_buffer.hpp"

#include "duckdb/common/allocator.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include <cstring>

#include <libxnvme.h>
#include <libxnvme_nvm.h>

namespace duckdb {

NvmeBuffer::NvmeBuffer(NvmeBufferType type, uint64_t user_size) : type(type) {
	Init();
	if (user_size) {
		Resize(user_size);
	}
}

void NvmeBuffer::Init() {
	buffer = nullptr;
	size = 0;
	internal_buffer = nullptr;
	internal_size = 0;
}

NvmeBuffer::~NvmeBuffer() {
	if (!internal_buffer) {
		return;
	}
	xnvme_buf_free(dev, internal_buffer);
}

void NvmeBuffer::ReallocBuffer(idx_t new_size) {
	data_ptr_t new_buffer;
	if (internal_buffer) {
		new_buffer = static_cast<unsigned char *>(xnvme_buf_realloc(dev, internal_buffer, new_size));
	} else {
		new_buffer = static_cast<unsigned char *>(xnvme_buf_alloc(dev, new_size));
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

NvmeBuffer::MemoryRequirement NvmeBuffer::CalculateMemory(uint64_t user_size) {
	NvmeBuffer::MemoryRequirement result;

	if (type == NvmeBufferType::TINY_BUFFER) {
		// We never do IO on tiny buffers, so there's no need to add a header or sector-align.
		result.header_size = 0;
		result.alloc_size = user_size;
	} else {
		result.header_size = Storage::DEFAULT_BLOCK_HEADER_SIZE;
		result.alloc_size = AlignValue<idx_t, Storage::SECTOR_SIZE>(result.header_size + user_size);
	}
	return result;
}

void NvmeBuffer::Resize(uint64_t new_size) {
	auto req = CalculateMemory(new_size);
	ReallocBuffer(req.alloc_size);

	if (new_size > 0) {
		buffer = internal_buffer + req.header_size;
		size = internal_size - req.header_size;
	}
}

void NvmeBuffer::Read(uint64_t location) {
	D_ASSERT(type != NvmeBufferType::TINY_BUFFER);
	// handle.Read(internal_buffer, internal_size, location);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	int err = xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);

	if (err || xnvme_cmd_ctx_cpl_status(&ctx)) {
		xnvme_cli_perr("xnvme_nvm_read()", err);
	}

	return;
}

void NvmeBuffer::Write(uint64_t location) {
	D_ASSERT(type != NvmeBufferType::TINY_BUFFER);
	// handle.Write(internal_buffer, internal_size, location);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	int err = xnvme_nvm_write(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);

	if (err || xnvme_cmd_ctx_cpl_status(&ctx)) {
		xnvme_cli_perr("xnvme_nvm_write()", err);
	}

	return;
}

void NvmeBuffer::Clear() {
	// memset(internal_buffer, 0, internal_size);
	xnvme_buf_clear(internal_buffer, internal_size);
}

void NvmeBuffer::Initialize(DebugInitialize initialize) {
	if (initialize == DebugInitialize::NO_INITIALIZE) {
		return;
	}
	// string foo = std::format("{}", (initialize == DebugInitialize::DEBUG_ZERO_INITIALIZE ? 0 : 0xFF));
	const char *value = "0";
	xnvme_buf_fill(internal_buffer, internal_size, value);
}

} // namespace duckdb
