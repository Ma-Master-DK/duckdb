//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/nvme_buffer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "db_buffer.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/debug_initialize.hpp"

#include <libxnvme.h>

namespace duckdb {

//! The NvmeBuffer represents a buffer that can be read or written to a Direct IO FileHandle.
class NvmeBuffer : DBBuffer {
public:
	NvmeBuffer();
	explicit NvmeBuffer(xnvme_dev *dev);
	NvmeBuffer(NvmeBuffer &source);

	virtual ~NvmeBuffer();

	xnvme_dev *dev;

public:
	//! Read into the NvmeBuffer from the specified location.
	void Read(uint64_t location);

	//! Write the contents of the NvmeBuffer to the specified location.
	void Write(uint64_t location);

	void ReallocBuffer();

protected:
	void Init(xnvme_dev *dev);
};
} // namespace duckdb
