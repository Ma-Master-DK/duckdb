#include "duckdb/common/db_buffer.hpp"

#include "duckdb/common/allocator.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include <cstring>

namespace duckdb {

DBBuffer::DBBuffer(Allocator &allocator, DBBufferType type) : allocator(allocator), type(type) {
}

DBBuffer::~DBBuffer() {
	if (!internal_buffer) {
		return;
	}
	allocator.FreeData(internal_buffer, internal_size);
}

void DBBuffer::Init() {
	buffer = nullptr;
	size = 0;
	internal_buffer = nullptr;
	internal_size = 0;
}

void DBBuffer::Clear() {
	memset(internal_buffer, 0, internal_size);
}

DBBuffer::MemoryRequirement DBBuffer::CalculateMemory(uint64_t user_size) {
	DBBuffer::MemoryRequirement result;

	if (type == DBBufferType::TINY_BUFFER) {
		// We never do IO on tiny buffers, so there's no need to add a header or sector-align.
		result.header_size = 0;
		result.alloc_size = user_size;
	} else {
		result.header_size = Storage::DEFAULT_BLOCK_HEADER_SIZE;
		result.alloc_size = AlignValue<idx_t, Storage::SECTOR_SIZE>(result.header_size + user_size);
	}
	return result;
}

void DBBuffer::Resize(uint64_t new_size) {
	auto req = CalculateMemory(new_size);
	ReallocBuffer(req.alloc_size);

	if (new_size > 0) {
		buffer = internal_buffer + req.header_size;
		size = internal_size - req.header_size;
	}
}

void DBBuffer::ReallocBuffer(idx_t new_size) {
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

void DBBuffer::Initialize(DebugInitialize initialize) {
	if (initialize == DebugInitialize::NO_INITIALIZE) {
		return;
	}
	uint8_t value = initialize == DebugInitialize::DEBUG_ZERO_INITIALIZE ? 0 : 0xFF;
	memset(internal_buffer, value, internal_size);
}

} // namespace duckdb
