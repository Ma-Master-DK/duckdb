//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/file_buffer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "db_buffer.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/debug_initialize.hpp"

namespace duckdb {
class Allocator;
struct FileHandle;

enum class FileBufferType : uint8_t { BLOCK = 1, MANAGED_BUFFER = 2, TINY_BUFFER = 3 };

static constexpr idx_t FILE_BUFFER_TYPE_COUNT = 3;

//! The FileBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class FileBuffer : DBBuffer {
public:
	//! Allocates a buffer of the specified size, with room for additional header bytes
	//! (typically 8 bytes). On return, this->AllocSize() >= this->size >= user_size.
	//! Our allocation size will always be page-aligned, which is necessary to support
	//! DIRECT_IO
	FileBuffer(Allocator &allocator, FileBufferType type, uint64_t user_size) : DBBuffer();
	FileBuffer(FileBuffer &source, FileBufferType type) : DBBuffer();

	virtual ~FileBuffer();

	Allocator &allocator;

public:
	//! Read into the FileBuffer from the specified location.
	void Read(FileHandle &handle, uint64_t location);

	//! Write the contents of the FileBuffer to the specified location.
	void Write(FileHandle &handle, uint64_t location);

	FileBufferType GetBufferType() const {
		return type;
	}

	MemoryRequirement CalculateMemory(uint64_t user_size);

	void Initialize(DebugInitialize info);

	// Same rules as the constructor. We add room for a header, in addition to
	// the requested user bytes. We then sector-align the result.
	void Resize(uint64_t user_size);

protected:
	FileBufferType type;
	void ReallocBuffer(idx_t new_size);
	void Init();
};

} // namespace duckdb
