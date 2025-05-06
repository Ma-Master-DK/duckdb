#include "duckdb/storage/nvme_block.hpp"

#include "duckdb/common/assert.hpp"
#include "duckdb/common/db_buffer.hpp"

namespace duckdb {

NvmeBlock::NvmeBlock(Allocator &allocator, const block_id_t id, const idx_t block_size)
    : Block(id), NvmeBuffer(allocator, DBBufferType::BLOCK, block_size) {
}

NvmeBlock::NvmeBlock(Allocator &allocator, block_id_t id, uint32_t internal_size)
    : Block(id), NvmeBuffer(allocator, DBBufferType::BLOCK, internal_size) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

NvmeBlock::NvmeBlock(NvmeBuffer &source, block_id_t id) : Block(id), NvmeBuffer(source, DBBufferType::BLOCK) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

} // namespace duckdb
