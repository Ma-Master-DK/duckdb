//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/nvme_block.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/nvme_buffer.hpp"
#include "duckdb/storage/block.hpp"
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class NvmeBlock : public Block, public NvmeBuffer {
public:
	NvmeBlock(Allocator &allocator, const block_id_t id, const idx_t block_size);
	NvmeBlock(Allocator &allocator, block_id_t id, u_int32_t internal_size);
	NvmeBlock(NvmeBuffer &source, block_id_t id);
};

} // namespace duckdb
