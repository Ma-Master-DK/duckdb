#include "duckdb/storage/file_block.hpp"

#include "duckdb/common/assert.hpp"

namespace duckdb {

FileBlock::FileBlock(Allocator &allocator, const block_id_t id, const idx_t block_size)
    : Block(id, DBBufferType::BLOCK), FileBuffer(allocator, DBBufferType::BLOCK, block_size) {
}

FileBlock::FileBlock(Allocator &allocator, block_id_t id, uint32_t internal_size)
    : Block(id, DBBufferType::BLOCK), FileBuffer(allocator, DBBufferType::BLOCK, internal_size) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

FileBlock::FileBlock(FileBuffer &source, block_id_t id)
    : Block(id, DBBufferType::BLOCK), FileBuffer(source, DBBufferType::BLOCK) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

} // namespace duckdb
