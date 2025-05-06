//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/buffer/file_buffer_handle.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/buffer/db_buffer_handle.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/common/file_buffer.hpp"

namespace duckdb {
class FileBlockHandle;
class FileBuffer;

class FileBufferHandle : DbBufferHandle {
public:
	DUCKDB_API FileBufferHandle();
	DUCKDB_API explicit FileBufferHandle(shared_ptr<FileBlockHandle> handle, optional_ptr<FileBuffer> node);

	DUCKDB_API ~FileBufferHandle() override;

	//! enable move constructors
	DUCKDB_API FileBufferHandle(FileBufferHandle &&other) noexcept;
	DUCKDB_API FileBufferHandle &operator=(FileBufferHandle &&) noexcept;

public:
	//! Gets the underlying file buffer. Handle must be valid.
	DUCKDB_API FileBuffer &GetFileBuffer();

	//! Returns a pointer to the buffer data. Handle must be valid.
	inline data_ptr_t Ptr() const {
		D_ASSERT(IsValid());
		return node->buffer;
	}

	//! Returns a pointer to the buffer data. Handle must be valid.
	inline data_ptr_t Ptr() {
		D_ASSERT(IsValid());
		return node->buffer;
	}

	DUCKDB_API bool IsValid() const override;
	DUCKDB_API void Destroy() override;

	const shared_ptr<FileBlockHandle> &GetFileBlockHandle() const {
		return handle;
	}

private:
	//! The block handle
	shared_ptr<FileBlockHandle> handle;

	//! The managed buffer node
	optional_ptr<FileBuffer> node;
};

} // namespace duckdb
