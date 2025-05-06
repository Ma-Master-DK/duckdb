//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/buffer/file_buffer_handle.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/storage_info.hpp"
#include "duckdb/common/file_buffer.hpp"

namespace duckdb {
class FileBlockHandle;
class FileBuffer;

class FileBufferHandle {
public:
	DUCKDB_API FileBufferHandle();
	DUCKDB_API explicit FileBufferHandle(shared_ptr<FileBlockHandle> handle, optional_ptr<FileBuffer> node);
	DUCKDB_API ~FileBufferHandle();
	// disable copy constructors
	FileBufferHandle(const FileBufferHandle &other) = delete;
	FileBufferHandle &operator=(const FileBufferHandle &) = delete;
	//! enable move constructors
	DUCKDB_API FileBufferHandle(FileBufferHandle &&other) noexcept;
	DUCKDB_API FileBufferHandle &operator=(FileBufferHandle &&) noexcept;

public:
	//! Returns whether or not the FileBufferHandle is valid.
	DUCKDB_API bool IsValid() const;
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
	//! Gets the underlying file buffer. Handle must be valid.
	DUCKDB_API FileBuffer &GetFileBuffer();
	//! Destroys the buffer handle
	DUCKDB_API void Destroy();

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
