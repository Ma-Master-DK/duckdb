#include "duckdb/storage/buffer/file_block_handle.hpp"
#include "duckdb/storage/buffer/file_buffer_pool.hpp"

namespace duckdb {

FileBufferPoolReservation::FileBufferPoolReservation(MemoryTag tag, FileBufferPool &pool) : tag(tag), pool(pool) {
}

FileBufferPoolReservation::FileBufferPoolReservation(FileBufferPoolReservation &&src) noexcept
    : tag(src.tag), pool(src.pool) {
	size = src.size;
	src.size = 0;
}

FileBufferPoolReservation &FileBufferPoolReservation::operator=(FileBufferPoolReservation &&src) noexcept {
	tag = src.tag;
	size = src.size;
	src.size = 0;
	return *this;
}

FileBufferPoolReservation::~FileBufferPoolReservation() {
	D_ASSERT(size == 0);
}

void FileBufferPoolReservation::Resize(idx_t new_size) {
	auto delta = UnsafeNumericCast<int64_t>(new_size) - UnsafeNumericCast<int64_t>(size);
	pool.UpdateUsedMemory(tag, delta);
	size = new_size;
}

void FileBufferPoolReservation::Merge(FileBufferPoolReservation src) {
	size += src.size;
	src.size = 0;
}

} // namespace duckdb
