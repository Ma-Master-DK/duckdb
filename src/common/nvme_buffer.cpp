#include "duckdb/common/nvme_buffer.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/common/db_buffer.hpp"
#include <cstdint>
#include <cstring>

#include <libxnvme.h>
#include <libxnvme_nvm.h>
#include <string>

namespace duckdb {

NvmeBuffer::NvmeBuffer(Allocator &allocator, DBBufferType type, uint64_t user_size) : DBBuffer(allocator, type) {
	Init();
	if (user_size) {
		Resize(user_size);
	}
}

NvmeBuffer::NvmeBuffer(NvmeBuffer &source, DBBufferType type) : DBBuffer(source.allocator, type) {
	// take over the structures of the source buffer
	buffer = source.buffer;
	size = source.size;
	internal_buffer = source.internal_buffer;
	internal_size = source.internal_size;

	source.Init();
}

void NvmeBuffer::Read(xnvme_dev *dev, uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);
}

void NvmeBuffer::Write(xnvme_dev *dev, uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_write(&ctx, xnvme_dev_get_nsid(dev), location, 1, internal_buffer, nullptr);
}

} // namespace duckdb
