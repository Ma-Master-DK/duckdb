//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/single_file_block_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "single_base_block_manager.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/storage/file_block.hpp"

namespace duckdb {

//! SingleFileBlockManager is an implementation for a BlockManager which manages blocks in a single file
// class SingleFileBlockManager : SingleBaseBlockManager(Storage::FILE_HEADER_SIZE) {
class SingleFileBlockManager : SingleBaseBlockManager {
public:
	SingleFileBlockManager(AttachedDatabase &db, const string &path, const StorageManagerOptions &options);

	FileOpenFlags GetFileFlags(bool create_new) const;

	//! Creates a new Block using the specified block_id and returns a pointer
	unique_ptr<FileBlock> ConvertBlock(block_id_t block_id, FileBuffer &source_buffer) override;
	unique_ptr<FileBlock> CreateBlock(block_id_t block_id, FileBuffer *source_buffer) override;

	//! Read the content of a range of blocks into a buffer
	void ReadBlocks(FileBuffer &buffer, block_id_t start_block, idx_t block_count) override;

	//! Write the given block to disk
	void Write(FileBuffer &block, block_id_t block_id) override;

private:
	void ReadAndChecksum(FileBuffer &handle, uint64_t location) const;
	void ChecksumAndWrite(FileBuffer &handle, uint64_t location) const;

private:
	//! The path where the file is stored
	string path;

	//! The file handle
	unique_ptr<FileHandle> handle;

	//! The buffer used to read/write to the headers
	FileBuffer header_buffer;
};
} // namespace duckdb
