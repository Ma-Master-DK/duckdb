#include "duckdb/common/nvme_buffer.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include <cstring>
#include <libxnvme.h>
#include <libxnvme_nvm.h>

namespace duckdb {

NvmeBuffer::NvmeBuffer() : DBBuffer() {
	Init(nullptr);
}

NvmeBuffer::NvmeBuffer(xnvme_dev *dev) : DBBuffer() {
	Init(dev);
}

NvmeBuffer::NvmeBuffer(NvmeBuffer &source) : DBBuffer() {
	// take over the structures of the source buffer
	buffer = source.buffer;
	size = source.size;
	internal_buffer = source.internal_buffer;
	internal_size = source.internal_size;

	source.Init(source.dev);
}

NvmeBuffer::~NvmeBuffer() {
	if (!internal_buffer) {
		goto exit;
	}

	xnvme_buf_free(dev, internal_buffer);
exit:
	xnvme_dev_close(dev);
}

void NvmeBuffer::Init(xnvme_dev *dev) {
	this->dev = dev;
	buffer = nullptr;
	size = 0;
	internal_buffer = nullptr;
	internal_size = xnvme_dev_get_geo(dev)->nbytes;
}

void NvmeBuffer::ReallocBuffer() {
	data_ptr_t new_buffer;
	if (internal_buffer) {
		new_buffer = static_cast<unsigned char *>(xnvme_buf_realloc(dev, internal_buffer, internal_size));
	} else {
		new_buffer = static_cast<unsigned char *>(xnvme_buf_alloc(dev, internal_size));
	}

	// FIXME: should we throw one of our exceptions here?
	if (!new_buffer) {
		xnvme_cli_perr("xnvme_buf_(er)alloc()", errno);
		throw std::bad_alloc();
	}

	internal_buffer = new_buffer;

	// The caller must update these.
	buffer = nullptr;
	size = 0;
}

void NvmeBuffer::Read(uint64_t location) {
	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);
	return;
}

void NvmeBuffer::Write(uint64_t location) {
	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_write(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);
	return;
}

void NvmeBuffer::Clear() {
	xnvme_buf_clear(internal_buffer, internal_size);
}

void NvmeBuffer::Initialize(DebugInitialize initialize) {
	if (initialize == DebugInitialize::NO_INITIALIZE) {
		return;
	}

	uint8_t value = initialize == DebugInitialize::DEBUG_ZERO_INITIALIZE ? 0 : 0xFF;
	memset(internal_buffer, value, internal_size);
}
} // namespace duckdb
