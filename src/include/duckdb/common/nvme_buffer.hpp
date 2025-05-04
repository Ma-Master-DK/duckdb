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
	NvmeBuffer(NvmeBuffer &source, DBBufferType type);

	~NvmeBuffer() override;

public:
	//! Read into the NvmeBuffer from the specified location.
	void Read(uint64_t location);

	//! Write the contents of the NvmeBuffer to the specified location.
	void Write(uint64_t location);

	void Clear() override;

	void Initialize(DebugInitialize info) override;

	void ReallocBuffer(idx_t new_size) override;

	void Init() override;

private:
	xnvme_dev *dev;
};

} // namespace duckdb
