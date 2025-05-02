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

//! The FileBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class DBBuffer {
public:
	DBBuffer();

	virtual ~DBBuffer();

	//! The buffer that users can write to
	data_ptr_t buffer;

	//! The user-facing size of the buffer.
	//! This is equivalent to internal_size - BLOCK_HEADER_SIZE.
	uint64_t size;

public:
	void Clear();

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

protected:
	//! The pointer to the internal buffer that will be read from or written to.
	//! This includes the buffer header.
	data_ptr_t internal_buffer;

	//! The aligned size as passed to the constructor.
	//! This is the size that is read from or written to disk.
	uint64_t internal_size;
};

} // namespace duckdb
