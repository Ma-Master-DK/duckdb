#include "duckdb/common/db_buffer.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/storage/storage_info.hpp"
#include <cstring>

namespace duckdb {

DBBuffer::DBBuffer(DBBufferType type) : type(type) {
}
} // namespace duckdb
