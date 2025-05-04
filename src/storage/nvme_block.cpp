#include "duckdb/storage/nvme_block.hpp"

#include "duckdb/common/assert.hpp"
#include "duckdb/common/db_buffer.hpp"

namespace duckdb {

NvmeBlock::NvmeBlock(NvmeBuffer &source, block_id_t id) : Block(id), NvmeBuffer(source, DBBufferType::BLOCK) {
	// TODOTODO: can we do this with xnvme?
	// D_ASSERT((AllocSize() & (Storage::SECTOR_SIZE - 1)) == 0);
}

} // namespace duckdb
