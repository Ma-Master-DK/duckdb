//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/buffer/db_buffer_handle.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class DbBufferHandle {
public:
	DUCKDB_API DbBufferHandle();

	virtual DUCKDB_API ~DbBufferHandle();

	// disable copy constructors
	DbBufferHandle(const DbBufferHandle &other) = delete;
	DbBufferHandle &operator=(const DbBufferHandle &) = delete;

public:
	//! Returns whether or not the DbBufferHandle is valid.
	virtual DUCKDB_API bool IsValid() const = 0;

	//! Destroys the buffer handle
	virtual DUCKDB_API void Destroy();

protected:
};

} // namespace duckdb
