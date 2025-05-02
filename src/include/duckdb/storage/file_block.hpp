//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/file_block.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/file_buffer.hpp"
#include "duckdb/storage/block.hpp"

namespace duckdb {

class Serializer;
class Deserializer;

class FileBlock : public Block, public FileBuffer {
public:
	FileBlock(Allocator &allocator, const block_id_t id, const idx_t block_size);
	FileBlock(Allocator &allocator, block_id_t id, uint32_t internal_size);
	FileBlock(FileBuffer &source, block_id_t id);
};
} // namespace duckdb
