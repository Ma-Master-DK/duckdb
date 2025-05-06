//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/single_nvme_block_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/storage/block_manager.hpp"
#include "duckdb/storage/nvme_block.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/common/set.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/single_db_block_manager.hpp"
#include "duckdb/storage/storage_info.hpp"

#include <libxnvme.h>

namespace duckdb {

class DatabaseInstance;
struct MetadataHandle;

//! SingleNvmeBlockManager is an implementation for a BlockManager which manages blocks in a single file
class SingleNvmeBlockManager : public SingleDbBlockManager {
public:
	SingleNvmeBlockManager(AttachedDatabase &db, const string &path, const StorageManagerOptions &options);

	~SingleNvmeBlockManager() override;

public:
	void CreateNewDatabase() override;
	void LoadExistingDatabase() override;

	//! Creates a new Block using the specified block_id and returns a pointer
	unique_ptr<NvmeBlock> ConvertBlock(block_id_t block_id, NvmeBuffer &source_buffer) override;
	unique_ptr<FileBlock> ConvertBlock(block_id_t block_id, FileBuffer &source_buffer) override {
		throw IOException("FileBuffer not allowed for SingleNvmeBlockManager.");
	}

	unique_ptr<NvmeBlock> CreateBlock(block_id_t block_id, NvmeBuffer *source_buffer) override;
	unique_ptr<FileBlock> CreateBlock(block_id_t block_id, FileBuffer *source_buffer) override {
		throw IOException("FileBuffer not allowed for SingleNvmeBlockManager.");
	}

	//! Read the content of the block from disk
	void Read(NvmeBlock &block) override;
	void Read(FileBlock &block) override {
		throw IOException("FileBlock not allowed for SingleNvmeBlockManager.");
	}

	//! Read the content of a range of blocks into a buffer
	void ReadBlocks(NvmeBuffer &buffer, block_id_t start_block, idx_t block_count) override;
	void ReadBlocks(FileBuffer &buffer, block_id_t start_block, idx_t block_count) override {
		throw IOException("FileBuffer not allowed for SingleNvmeBlockManager.");
	}

	//! Write the given block to disk
	void Write(NvmeBuffer &block, block_id_t block_id) override;
	void Write(FileBuffer &block, block_id_t block_id) override {
		throw IOException("FileBuffer not allowed for SingleNvmeBlockManager.");
	}

	bool IsRemote() override;

	void FileSync() override;

	void Truncate() override;

	//! Write the header to disk, this is the final step of the checkpointing process
	void WriteHeader(DatabaseHeader header) override;

private:
	//! The location in the file where the block writing starts
	static constexpr uint64_t BLOCK_START = Storage::FILE_HEADER_SIZE * 3;

	void ReadAndChecksum(NvmeBuffer &buf, uint64_t location) const;
	void ChecksumAndWrite(NvmeBuffer &buf, uint64_t location) const;

	idx_t GetBlockLocation(block_id_t block_id) override;

	void TrimFreeBlocks() override;

private:
	//! The device handle
	xnvme_dev *dev;

	//! The buffer used to read/write to the headers
	NvmeBuffer header_buffer;
};

} // namespace duckdb
