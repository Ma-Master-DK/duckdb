#include "duckdb/storage/block.hpp"

namespace duckdb {

Block::Block(block_id_t id, DBBufferType type) : id(id), DBBuffer(type) {
}

} // namespace duckdb
