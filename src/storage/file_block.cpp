#include "duckdb/storage/file_block.hpp"

#include "duckdb/common/assert.hpp"

namespace duckdb {

FileBlock::FileBlock(Allocator &allocator, const block_id_t id, const idx_t block_size)
    : Block(id), FileBuffer(allocator, FileBufferType::BLOCK, block_size) {
}

FileBlock::FileBlock(Allocator &allocator, block_id_t id, uint32_t internal_size)
    : Block(id), FileBuffer(allocator, FileBufferType::BLOCK, internal_size) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

FileBlock::FileBlock(FileBuffer &source, block_id_t id) : Block(id), FileBuffer(source, FileBufferType::BLOCK) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

} // namespace duckdb
