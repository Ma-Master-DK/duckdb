#include "duckdb/storage/nvme_block.hpp"

#include "duckdb/common/assert.hpp"

namespace duckdb {

NvmeBlock::NvmeBlock(NvmeBuffer &source, block_id_t id) : Block(id), NvmeBuffer(source) {
	D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

} // namespace duckdb
