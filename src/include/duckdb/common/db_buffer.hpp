//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/db_buffer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/debug_initialize.hpp"

namespace duckdb {
class Allocator;

enum class DBBufferType : uint8_t { BLOCK = 1, MANAGED_BUFFER = 2, TINY_BUFFER = 3 };

static constexpr idx_t DB_BUFFER_TYPE_COUNT = 3;

class DBBuffer {
public:
	//! Allocates a buffer of the specified size, with room for additional header bytes
	//! (typically 8 bytes). On return, this->AllocSize() >= this->size >= user_size.
	//! Our allocation size will always be page-aligned, which is necessary to support
	//! DIRECT_IO
	DBBuffer(Allocator &allocator, DBBufferType type);

	virtual ~DBBuffer();

	//! The buffer that users can write to
	data_ptr_t buffer;

	//! The user-facing size of the buffer.
	//! This is equivalent to internal_size - BLOCK_HEADER_SIZE.
	uint64_t size;

	Allocator &allocator;

public:
	void Clear();

	DBBufferType GetBufferType() const {
		return type;
	}

	// Same rules as the constructor. We add room for a header, in addition to
	// the requested user bytes. We then sector-align the result.
	void Resize(uint64_t user_size);

	uint64_t AllocSize() const {
		return internal_size;
	}

	uint64_t Size() const {
		return size;
	}

	data_ptr_t InternalBuffer() {
		return internal_buffer;
	}

	struct MemoryRequirement {
		idx_t alloc_size;
		idx_t header_size;
	};

	MemoryRequirement CalculateMemory(uint64_t user_size);

	void Initialize(DebugInitialize info);

protected:
	//! The type of the buffer.
	DBBufferType type;

	//! The pointer to the internal buffer that will be read from or written to.
	//! This includes the buffer header.
	data_ptr_t internal_buffer;

	//! The aligned size as passed to the constructor.
	//! This is the size that is read from or written to disk.
	uint64_t internal_size;

	void ReallocBuffer(idx_t new_size);

	void Init();
};

} // namespace duckdb
