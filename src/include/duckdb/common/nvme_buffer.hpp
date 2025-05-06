//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/nvme_buffer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/debug_initialize.hpp"
#include "db_buffer.hpp"

#include <libxnvme.h>

namespace duckdb {

//! The NvmeBuffer represents a buffer that can be read or written to an NVMe device
class NvmeBuffer : public DBBuffer {
public:
	NvmeBuffer(Allocator &allocator, DBBufferType type, uint64_t user_size);
	NvmeBuffer(NvmeBuffer &source, DBBufferType type);

public:
	//! Read into the NvmeBuffer from the specified location.
	void Read(xnvme_dev *dev, uint64_t location);

	//! Write the contents of the NvmeBuffer to the specified location.
	void Write(xnvme_dev *dev, uint64_t location);
};

} // namespace duckdb
