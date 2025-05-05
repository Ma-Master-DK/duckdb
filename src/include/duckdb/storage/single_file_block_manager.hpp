//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/single_file_block_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/storage/block_manager.hpp"
#include "duckdb/storage/file_block.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/common/set.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/single_db_block_manager.hpp"
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class DatabaseInstance;
struct MetadataHandle;

//! SingleFileBlockManager is an implementation for a BlockManager which manages blocks in a single file
class SingleFileBlockManager : public SingleDbBlockManager {
public:
	SingleFileBlockManager(AttachedDatabase &db, const string &path, const StorageManagerOptions &options);

public:
	void CreateNewDatabase() override;
	void LoadExistingDatabase() override;

	//! Creates a new Block using the specified block_id and returns a pointer
	unique_ptr<FileBlock> ConvertBlock(block_id_t block_id, FileBuffer &source_buffer) override;
	unique_ptr<FileBlock> CreateBlock(block_id_t block_id, FileBuffer *source_buffer) override;

	//! Read the content of the block from disk
	void Read(FileBlock &block) override;

	//! Read the content of a range of blocks into a buffer
	void ReadBlocks(FileBuffer &buffer, block_id_t start_block, idx_t block_count) override;

	//! Write the given block to disk
	void Write(FileBuffer &block, block_id_t block_id) override;

	//! Write the header to disk, this is the final step of the checkpointing process
	void WriteHeader(DatabaseHeader header) override;

	bool IsRemote() override;

	void FileSync() override;

	void Truncate() override;

private:
	//! The location in the file where the block writing starts
	static constexpr uint64_t BLOCK_START = Storage::FILE_HEADER_SIZE * 3;

	void ReadAndChecksum(FileBuffer &handle, uint64_t location) const;
	void ChecksumAndWrite(FileBuffer &handle, uint64_t location) const;

	idx_t GetBlockLocation(block_id_t block_id) override;

	void TrimFreeBlocks() override;

private:
	//! The file handle
	unique_ptr<FileHandle> handle;

	//! The buffer used to read/write to the headers
	FileBuffer header_buffer;
};

} // namespace duckdb
