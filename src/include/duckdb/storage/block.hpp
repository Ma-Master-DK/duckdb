
//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/block.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class Serializer;
class Deserializer;

class Block {
public:
	Block(block_id_t id);

	block_id_t id;
};

struct BlockPointer {
	BlockPointer(block_id_t block_id_p, uint32_t offset_p) : block_id(block_id_p), offset(offset_p) {
	}
	BlockPointer() : block_id(INVALID_BLOCK), offset(0) {
	}

	block_id_t block_id;
	uint32_t offset;
	uint32_t unused_padding {0};

	bool IsValid() const {
		return block_id != INVALID_BLOCK;
	}

	void Serialize(Serializer &serializer) const;
	static BlockPointer Deserialize(Deserializer &source);
};

} // namespace duckdb
