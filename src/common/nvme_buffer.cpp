#include "duckdb/common/nvme_buffer.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/common/db_buffer.hpp"
#include <cstring>

#include <libxnvme.h>
#include <libxnvme_nvm.h>
#include <string>

namespace duckdb {

NvmeBuffer::NvmeBuffer(NvmeBuffer &source, DBBufferType type) : DBBuffer(type) {
	// take over the structures of the source buffer
	buffer = source.buffer;
	size = source.size;
	internal_buffer = source.internal_buffer;
	internal_size = source.internal_size;

	source.Init();
}

NvmeBuffer::~NvmeBuffer() {
	if (!internal_buffer) {
		goto exit;
	}
	xnvme_buf_free(dev, internal_buffer);
exit:
	xnvme_dev_close(dev);
}

void NvmeBuffer::Init() {
	buffer = nullptr;
	size = 0;
	internal_buffer = nullptr;
	internal_size = 0;
}

void NvmeBuffer::ReallocBuffer(idx_t new_size) {
	data_ptr_t new_buffer;
	if (internal_buffer) {
		new_buffer = static_cast<data_ptr_t>(xnvme_buf_realloc(dev, internal_buffer, new_size));
	} else {
		new_buffer = static_cast<data_ptr_t>(xnvme_buf_alloc(dev, new_size));
	}

	// FIXME: should we throw one of our exceptions here?
	if (!new_buffer) {
		xnvme_cli_perr("xnvme_buf_(re)alloc()", errno);
		throw std::bad_alloc();
	}
	internal_buffer = new_buffer;
	internal_size = new_size;

	// The caller must update these.
	buffer = nullptr;
	size = 0;
}

void NvmeBuffer::Read(uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(dev), 0, 0, internal_buffer, nullptr);
}

void NvmeBuffer::Write(uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_write(&ctx, xnvme_dev_get_nsid(dev), 0, 0, internal_buffer, nullptr);
}

void NvmeBuffer::Clear() {
	xnvme_buf_clear(internal_buffer, internal_size);
}

void NvmeBuffer::Initialize(DebugInitialize initialize) {
	if (initialize == DebugInitialize::NO_INITIALIZE) {
		return;
	}
	uint8_t value = initialize == DebugInitialize::DEBUG_ZERO_INITIALIZE ? 0 : 0xFF;
	xnvme_buf_fill(internal_buffer, internal_size, std::to_string(value).c_str());
}

} // namespace duckdb
