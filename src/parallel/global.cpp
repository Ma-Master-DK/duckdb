#include "duckdb/parallel/global.hpp"

namespace duckdb {
thread_local xnvme_queue *queue_ptr = nullptr;
}
