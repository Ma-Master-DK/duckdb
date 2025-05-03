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

enum class DBBufferType : uint8_t { BLOCK = 1, MANAGED_BUFFER = 2, TINY_BUFFER = 3 };

static constexpr idx_t DB_BUFFER_TYPE_COUNT = 3;

//! The FileBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class DBBuffer {
public:
	DBBuffer(DBBufferType type);

	virtual ~DBBuffer();

	//! The buffer that users can write to
	data_ptr_t buffer;

	//! The user-facing size of the buffer.
	//! This is equivalent to internal_size - BLOCK_HEADER_SIZE.
	uint64_t size;

public:
	virtual void Clear();

	uint64_t AllocSize() const {
		return internal_size;
	}

	uint64_t Size() const {
		return size;
	}

	DBBufferType GetBufferType() const {
		return type;
	}

	data_ptr_t InternalBuffer() {
		return internal_buffer;
	}

	struct MemoryRequirement {
		idx_t alloc_size;
		idx_t header_size;
	};

	// Same rules as the constructor. We add room for a header, in addition to
	// the requested user bytes. We then sector-align the result.
	virtual void Resize(uint64_t user_size);

protected:
	DBBufferType type;

	//! The pointer to the internal buffer that will be read from or written to.
	//! This includes the buffer header.
	data_ptr_t internal_buffer;

	//! The aligned size as passed to the constructor.
	//! This is the size that is read from or written to disk.
	uint64_t internal_size;
};

} // namespace duckdb
