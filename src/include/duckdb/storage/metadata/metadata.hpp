//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/metadata/metadata.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/typedefs.hpp"
#include "duckdb/storage/storage_info.hpp"
namespace duckdb {

struct MetaBlockPointer {
	MetaBlockPointer(idx_t block_pointer, uint32_t offset_p) : block_pointer(block_pointer), offset(offset_p) {
	}
	MetaBlockPointer() : block_pointer(DConstants::INVALID_INDEX), offset(0) {
	}

	idx_t block_pointer;
	uint32_t offset;
	uint32_t unused_padding {0};

	bool IsValid() const {
		return block_pointer != DConstants::INVALID_INDEX;
	}
	block_id_t GetBlockId() const;
	uint32_t GetBlockIndex() const;

	void Serialize(Serializer &serializer) const;
	static MetaBlockPointer Deserialize(Deserializer &source);
};

} // namespace duckdb
