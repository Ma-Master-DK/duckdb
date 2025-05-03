//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/single_nvme_block_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "single_base_block_manager.hpp"
#include "duckdb/storage/nvme_block.hpp"

#include <libxnvme.h>

namespace duckdb {

//! SingleNvmeBlockManager is an implementation for a BlockManager which manages blocks on a single device
class SingleNvmeBlockManager : public SingleBaseBlockManager {
	static constexpr uint64_t BLOCK_START = Storage::NVME_HEADER_SIZE * 3;

public:
	SingleNvmeBlockManager(AttachedDatabase &db, const string &path, const StorageManagerOptions &options);

	void CreateNewDatabase() override;
	void LoadExistingDatabase() override;

	//! Creates a new Block using the specified block_id and returns a pointer
	unique_ptr<Block> ConvertBlock(block_id_t block_id, NvmeBuffer &source_buffer) override;
	unique_ptr<Block> ConvertBlock(block_id_t block_id, FileBuffer &source_buffer) override {
		throw NotImplementedException("ConvertBlock(FileBuffer) not supported in SingleNvmeBlockManager");
	}

	unique_ptr<Block> CreateBlock(block_id_t block_id, NvmeBuffer *source_buffer) override;
	unique_ptr<Block> CreateBlock(block_id_t block_id, FileBuffer &source_buffer) override {
		throw NotImplementedException("CreateBlock(FileBuffer) not supported in SingleNvmeBlockManager");
	}

	//! Read the content of a range of blocks into a buffer
	void ReadBlocks(NvmeBuffer &buffer, block_id_t start_block, idx_t block_count) override;

	void TrimFreeBlocks() override;

	//! Write the given block to disk
	void Write(NvmeBuffer &block, block_id_t block_id) override;
	void WriteHeader(DatabaseHeader header) override;

	void Truncate() override;
	void FileSync() override;

	bool IsRemote() override;

private:
	void ReadAndChecksum(NvmeBuffer &buf, uint64_t location) const;
	void ChecksumAndWrite(NvmeBuffer &buf, uint64_t location) const;

private:
	//! The path where the deivce is located
	string path;

	//! The buffer used to read/write to the headers
	NvmeBuffer dev_buf;
};
} // namespace duckdb
