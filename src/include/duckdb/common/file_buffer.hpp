//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/file_buffer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/debug_initialize.hpp"
#include "db_buffer.hpp"

namespace duckdb {
class Allocator;
struct FileHandle;

//! The FileBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class FileBuffer : public DBBuffer {
public:
	//! Allocates a buffer of the specified size, with room for additional header bytes
	//! (typically 8 bytes). On return, this->AllocSize() >= this->size >= user_size.
	//! Our allocation size will always be page-aligned, which is necessary to support
	//! DIRECT_IO
	FileBuffer(Allocator &allocator, DBBufferType type, uint64_t user_size);
	FileBuffer(FileBuffer &source, DBBufferType type);

	~FileBuffer() override;

	Allocator &allocator;

public:
	//! Read into the FileBuffer from the specified location.
	void Read(FileHandle &handle, uint64_t location);

	//! Write the contents of the FileBuffer to the specified location.
	void Write(FileHandle &handle, uint64_t location);

	void Clear() override;

	void Initialize(DebugInitialize info) override;

	void ReallocBuffer(idx_t new_size) override;

	void Init() override;
};

} // namespace duckdb
