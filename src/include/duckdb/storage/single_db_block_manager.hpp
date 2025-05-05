//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/single_db_block_manager.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckdb/storage/block_manager.hpp"
#include "duckdb/storage/block.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/common/set.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/storage/metadata/metadata_writer.hpp"
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class DatabaseInstance;
struct MetadataHandle;

struct StorageManagerOptions {
	bool read_only = false;
	bool use_direct_io = false;
	DebugInitialize debug_initialize = DebugInitialize::NO_INITIALIZE;
	optional_idx block_alloc_size;
	optional_idx storage_version;
	optional_idx version_number;
};

//! SingleDbBlockManager is an implementation for a BlockManager which manages blocks in a single file
class SingleDbBlockManager : public BlockManager {
public:
	SingleDbBlockManager(AttachedDatabase &db, const string &path, const StorageManagerOptions &options);

	virtual ~SingleDbBlockManager();

public:
	FileOpenFlags GetFileFlags(bool create_new) const;

	//! Creates a new database.
	virtual void CreateNewDatabase() = 0;

	//! Loads an existing database. We pass the provided block allocation size as a parameter
	//! to detect inconsistencies with the file header.
	virtual void LoadExistingDatabase() = 0;

	//! Return the next free block id
	block_id_t GetFreeBlockId() override;

	//! Check the next free block id - but do not assign or allocate it
	block_id_t PeekFreeBlockId() override;

	//! Returns whether or not a specified block is the root block
	bool IsRootBlock(MetaBlockPointer root) override;

	//! Mark a block as free (immediately re-writeable)
	void MarkBlockAsFree(block_id_t block_id) override;

	//! Mark a block as used (no longer re-writeable)
	void MarkBlockAsUsed(block_id_t block_id) override;

	//! Mark a block as modified (re-writeable after a checkpoint)
	void MarkBlockAsModified(block_id_t block_id) override;

	//! Increase the reference count of a block. The block should hold at least one reference
	void IncreaseBlockReferenceCount(block_id_t block_id) override;

	//! Return the meta block id
	idx_t GetMetaBlock() override;

	//! Truncate the underlying database file after a checkpoint
	bool InMemory() override {
		return false;
	}
	//! Returns the number of total blocks
	idx_t TotalBlocks() override;

	//! Returns the number of free blocks
	idx_t FreeBlocks() override;

protected:
	//! Loads the free list of the file.
	void LoadFreeList();

	//! Initializes the database header. We pass the provided block allocation size as a parameter
	//!	to detect inconsistencies with the file header.
	void Initialize(const DatabaseHeader &header, const optional_idx block_alloc_size);

	virtual idx_t GetBlockLocation(block_id_t block_id) = 0;

	//! Return the blocks to which we will write the free list and modified blocks
	vector<MetadataHandle> GetFreeListBlocks();
	virtual void TrimFreeBlocks() = 0;

	void IncreaseBlockReferenceCountInternal(block_id_t block_id);

	//! Verify the block usage count
	void VerifyBlocks(const unordered_map<block_id_t, idx_t> &block_usage_count) override;

	void AddStorageVersionTag();
	uint64_t GetVersionNumber();

	template <class T>
	void SerializeHeaderStructure(T header, data_ptr_t ptr) {
		MemoryStream ser(ptr, Storage::FILE_HEADER_SIZE);
		header.Write(ser);
	}

	MainHeader DeserializeMainHeader(data_ptr_t ptr) {
		MemoryStream source(ptr, Storage::FILE_HEADER_SIZE);
		return MainHeader::Read(source);
	}

	DatabaseHeader DeserializeDatabaseHeader(const MainHeader &main_header, data_ptr_t ptr) {
		MemoryStream source(ptr, Storage::FILE_HEADER_SIZE);
		return DatabaseHeader::Read(main_header, source);
	}

	MainHeader ConstructMainHeader(idx_t version_number) {
		MainHeader main_header;
		main_header.version_number = version_number;
		memset(main_header.flags, 0, sizeof(uint64_t) * MainHeader::FLAG_COUNT);
		return main_header;
	}

protected:
	AttachedDatabase &db;

	//! The active DatabaseHeader, either 0 (h1) or 1 (h2)
	uint8_t active_header;

	//! The path where the file is stored
	string path;

	//! The list of free blocks that can be written to currently
	set<block_id_t> free_list;

	//! The list of blocks that were freed since the last checkpoint.
	set<block_id_t> newly_freed_list;

	//! The list of multi-use blocks (i.e. blocks that have >1 reference in the file)
	//! When a multi-use block is marked as modified, the reference count is decreased by 1 instead of directly
	//! Appending the block to the modified_blocks list
	unordered_map<block_id_t, uint32_t> multi_use_blocks;

	//! The list of blocks that will be added to the free list
	unordered_set<block_id_t> modified_blocks;

	//! The current meta block id
	idx_t meta_block;

	//! The current maximum block id, this id will be given away first after the free_list runs out
	block_id_t max_block;

	//! The block id where the free list can be found
	idx_t free_list_id;

	//! The current header iteration count
	uint64_t iteration_count;

	//! The storage manager options
	StorageManagerOptions options;

	//! Lock for performing various operations in the single file block manager
	mutex block_lock;
};

class FreeListBlockWriter : public MetadataWriter {
public:
	FreeListBlockWriter(MetadataManager &manager, vector<MetadataHandle> free_list_blocks_p)
	    : MetadataWriter(manager), free_list_blocks(std::move(free_list_blocks_p)), index(0) {
	}

	vector<MetadataHandle> free_list_blocks;
	idx_t index;

protected:
	MetadataHandle NextHandle() override {
		if (index >= free_list_blocks.size()) {
			throw InternalException(
			    "Free List Block Writer ran out of blocks, this means not enough blocks were allocated up front");
		}
		return std::move(free_list_blocks[index++]);
	}
};

} // namespace duckdb
