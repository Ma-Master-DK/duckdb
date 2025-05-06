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
struct FileHandle;

//! The FileBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class FileBuffer : public DBBuffer {
public:
	FileBuffer(Allocator &allocator, DBBufferType type, uint64_t user_size);
	FileBuffer(FileBuffer &source, DBBufferType type);

public:
	//! Read into the FileBuffer from the specified location.
	void Read(FileHandle &handle, uint64_t location);

	//! Write the contents of the FileBuffer to the specified location.
	void Write(FileHandle &handle, uint64_t location);
};

} // namespace duckdb
