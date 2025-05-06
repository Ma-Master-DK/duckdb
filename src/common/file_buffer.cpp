#include "duckdb/common/file_buffer.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/common/db_buffer.hpp"
#include <cstring>

namespace duckdb {

FileBuffer::FileBuffer(Allocator &allocator, DBBufferType type, uint64_t user_size) : DBBuffer(allocator, type) {
	Init();
	if (user_size) {
		Resize(user_size);
	}
}

FileBuffer::FileBuffer(FileBuffer &source, DBBufferType type) : DBBuffer(source.allocator, type) {
	// take over the structures of the source buffer
	buffer = source.buffer;
	size = source.size;
	internal_buffer = source.internal_buffer;
	internal_size = source.internal_size;

	source.Init();
}

void FileBuffer::Read(FileHandle &handle, uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);
	handle.Read(internal_buffer, internal_size, location);
}

void FileBuffer::Write(FileHandle &handle, uint64_t location) {
	D_ASSERT(type != DBBufferType::TINY_BUFFER);
	handle.Write(internal_buffer, internal_size, location);
}

} // namespace duckdb
