//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/nvme_block.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/nvme_buffer.hpp"
#include "duckdb/storage/block.hpp"

namespace duckdb {

class Serializer;
class Deserializer;

class NvmeBlock : public Block, public NvmeBuffer {
public:
	NvmeBlock(NvmeBuffer &source, block_id_t id);
};
} // namespace duckdb
