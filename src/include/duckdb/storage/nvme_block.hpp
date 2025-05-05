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
	NvmeBlock(block_id_t id);
	NvmeBlock(NvmeBuffer &source, block_id_t id);
};

} // namespace duckdb
