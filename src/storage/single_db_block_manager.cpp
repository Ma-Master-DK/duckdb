#include "duckdb/storage/single_db_block_manager.hpp"

#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/storage/metadata/metadata_reader.hpp"
#include "duckdb/storage/metadata/metadata_writer.hpp"
#include "duckdb/storage/storage_manager.hpp"

#include <algorithm>
#include <cstring>

namespace duckdb {

const char MainHeader::MAGIC_BYTES[] = "DUCK";

void SerializeVersionNumber(WriteStream &ser, const string &version_str) {
	data_t version[MainHeader::MAX_VERSION_SIZE];
	memset(version, 0, MainHeader::MAX_VERSION_SIZE);
	memcpy(version, version_str.c_str(), MinValue<idx_t>(version_str.size(), MainHeader::MAX_VERSION_SIZE));
	ser.WriteData(version, MainHeader::MAX_VERSION_SIZE);
}

void DeserializeVersionNumber(ReadStream &stream, data_t *dest) {
	memset(dest, 0, MainHeader::MAX_VERSION_SIZE);
	stream.ReadData(dest, MainHeader::MAX_VERSION_SIZE);
}

void MainHeader::Write(WriteStream &ser) {
	ser.WriteData(const_data_ptr_cast(MAGIC_BYTES), MAGIC_BYTE_SIZE);
	ser.Write<uint64_t>(version_number);
	for (idx_t i = 0; i < FLAG_COUNT; i++) {
		ser.Write<uint64_t>(flags[i]);
	}
	SerializeVersionNumber(ser, DuckDB::LibraryVersion());
	SerializeVersionNumber(ser, DuckDB::SourceID());
}

MainHeader MainHeader::Read(ReadStream &source) {
	data_t magic_bytes[MAGIC_BYTE_SIZE];
	MainHeader header;
	source.ReadData(magic_bytes, MainHeader::MAGIC_BYTE_SIZE);
	if (memcmp(magic_bytes, MainHeader::MAGIC_BYTES, MainHeader::MAGIC_BYTE_SIZE) != 0) {
		throw IOException("The file is not a valid DuckDB database file!");
	}
	header.version_number = source.Read<uint64_t>();
	// check the version number
	if (header.version_number < VERSION_NUMBER_LOWER || header.version_number > VERSION_NUMBER_UPPER) {
		auto version = GetDuckDBVersion(header.version_number);
		string version_text;
		if (!version.empty()) {
			// known version
			version_text = "DuckDB version " + string(version);
		} else {
			version_text = string("an ") +
			               (VERSION_NUMBER_UPPER > header.version_number ? "older development" : "newer") +
			               string(" version of DuckDB");
		}
		throw IOException(
		    "Trying to read a database file with version number %lld, but we can only read versions between %lld and "
		    "%lld.\n"
		    "The database file was created with %s.\n\n"
		    "Newer DuckDB version might introduce backward incompatible changes (possibly guarded by compatibility "
		    "settings)"
		    "See the storage page for migration strategy and more information: https://duckdb.org/internals/storage",
		    header.version_number, VERSION_NUMBER_LOWER, VERSION_NUMBER_UPPER, version_text);
	}
	// read the flags
	for (idx_t i = 0; i < FLAG_COUNT; i++) {
		header.flags[i] = source.Read<uint64_t>();
	}
	DeserializeVersionNumber(source, header.library_git_desc);
	DeserializeVersionNumber(source, header.library_git_hash);
	return header;
}

void DatabaseHeader::Write(WriteStream &ser) {
	ser.Write<uint64_t>(iteration);
	ser.Write<idx_t>(meta_block);
	ser.Write<idx_t>(free_list);
	ser.Write<uint64_t>(block_count);
	ser.Write<idx_t>(block_alloc_size);
	ser.Write<idx_t>(vector_size);
	ser.Write<idx_t>(serialization_compatibility);
}

DatabaseHeader DatabaseHeader::Read(const MainHeader &main_header, ReadStream &source) {
	DatabaseHeader header;
	header.iteration = source.Read<uint64_t>();
	header.meta_block = source.Read<idx_t>();
	header.free_list = source.Read<idx_t>();
	header.block_count = source.Read<uint64_t>();
	header.block_alloc_size = source.Read<idx_t>();

	// backwards compatibility
	if (!header.block_alloc_size) {
		header.block_alloc_size = DEFAULT_BLOCK_ALLOC_SIZE;
	}

	header.vector_size = source.Read<idx_t>();
	if (!header.vector_size) {
		// backwards compatibility
		header.vector_size = DEFAULT_STANDARD_VECTOR_SIZE;
	}
	if (header.vector_size != STANDARD_VECTOR_SIZE) {
		throw IOException("Cannot read database file: DuckDB's compiled vector size is %llu bytes, but the file has a "
		                  "vector size of %llu bytes.",
		                  STANDARD_VECTOR_SIZE, header.vector_size);
	}
	if (main_header.version_number == 64) {
		// version number 64 does not have the serialization compatibility in the file - default to 1
		header.serialization_compatibility = 1;
	} else {
		// read from the file
		header.serialization_compatibility = source.Read<idx_t>();
	}
	return header;
}

SingleDbBlockManager::SingleDbBlockManager(AttachedDatabase &db, const string &path_p,
                                           const StorageManagerOptions &options)
    : BlockManager(BufferManager::GetBufferManager(db), options.block_alloc_size), db(db), path(path_p),
      iteration_count(0), options(options) {
}

SingleDbBlockManager::~SingleDbBlockManager() {
}

FileOpenFlags SingleDbBlockManager::GetFileFlags(bool create_new) const {
	FileOpenFlags result;
	if (options.read_only) {
		D_ASSERT(!create_new);
		result = FileFlags::FILE_FLAGS_READ | FileFlags::FILE_FLAGS_NULL_IF_NOT_EXISTS | FileLockType::READ_LOCK;
	} else {
		result = FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_READ | FileLockType::WRITE_LOCK;
		if (create_new) {
			result |= FileFlags::FILE_FLAGS_FILE_CREATE;
		}
	}
	if (options.use_direct_io) {
		result |= FileFlags::FILE_FLAGS_DIRECT_IO;
	}
	// database files can be read from in parallel
	result |= FileFlags::FILE_FLAGS_PARALLEL_ACCESS;
	return result;
}

void SingleDbBlockManager::AddStorageVersionTag() {
	db.tags["storage_version"] = GetStorageVersionName(options.storage_version.GetIndex());
}

uint64_t SingleDbBlockManager::GetVersionNumber() {
	uint64_t version_number = VERSION_NUMBER;
	if (options.storage_version.GetIndex() >= 4) {
		version_number = 65;
	}
	return version_number;
}

void SingleDbBlockManager::Initialize(const DatabaseHeader &header, const optional_idx block_alloc_size) {
	free_list_id = header.free_list;
	meta_block = header.meta_block;
	iteration_count = header.iteration;
	max_block = NumericCast<block_id_t>(header.block_count);
	if (options.storage_version.IsValid()) {
		// storage version specified explicity - use requested storage version
		auto requested_compat_version = options.storage_version.GetIndex();
		if (requested_compat_version < header.serialization_compatibility) {
			throw InvalidInputException(
			    "Error opening \"%s\": cannot initialize database with storage version %d - which is lower than what "
			    "the database itself uses (%d). The storage version of an existing database cannot be lowered.",
			    path, requested_compat_version, header.serialization_compatibility);
		}
	} else {
		// load storage version from header
		options.storage_version = header.serialization_compatibility;
	}
	if (header.serialization_compatibility > SerializationCompatibility::Latest().serialization_version) {
		throw InvalidInputException(
		    "Error opening \"%s\": file was written with a storage version greater than the latest version supported "
		    "by this DuckDB instance. Try opening the file with a newer version of DuckDB.",
		    path);
	}
	db.GetStorageManager().SetStorageVersion(options.storage_version.GetIndex());

	if (block_alloc_size.IsValid() && block_alloc_size.GetIndex() != header.block_alloc_size) {
		throw InvalidInputException(
		    "Error opening \"%s\": cannot initialize the same database with a different block size: provided block "
		    "size: %llu, file block size: %llu",
		    path, GetBlockAllocSize(), header.block_alloc_size);
	}
	SetBlockAllocSize(header.block_alloc_size);
}

void SingleDbBlockManager::LoadFreeList() {
	MetaBlockPointer free_pointer(free_list_id, 0);
	if (!free_pointer.IsValid()) {
		// no free list
		return;
	}
	MetadataReader reader(GetMetadataManager(), free_pointer, nullptr, BlockReaderType::REGISTER_BLOCKS);
	auto free_list_count = reader.Read<uint64_t>();
	free_list.clear();
	for (idx_t i = 0; i < free_list_count; i++) {
		auto block = reader.Read<block_id_t>();
		free_list.insert(block);
		newly_freed_list.insert(block);
	}
	auto multi_use_blocks_count = reader.Read<uint64_t>();
	multi_use_blocks.clear();
	for (idx_t i = 0; i < multi_use_blocks_count; i++) {
		auto block_id = reader.Read<block_id_t>();
		auto usage_count = reader.Read<uint32_t>();
		multi_use_blocks[block_id] = usage_count;
	}
	GetMetadataManager().Read(reader);
	GetMetadataManager().MarkBlocksAsModified();
}

bool SingleDbBlockManager::IsRootBlock(MetaBlockPointer root) {
	return root.block_pointer == meta_block;
}

block_id_t SingleDbBlockManager::GetFreeBlockId() {
	lock_guard<mutex> lock(block_lock);
	block_id_t block;
	if (!free_list.empty()) {
		// The free list is not empty, so we take its first element.
		block = *free_list.begin();
		// erase the entry from the free list again
		free_list.erase(free_list.begin());
		newly_freed_list.erase(block);
	} else {
		block = max_block++;
	}
	return block;
}

block_id_t SingleDbBlockManager::PeekFreeBlockId() {
	lock_guard<mutex> lock(block_lock);
	if (!free_list.empty()) {
		return *free_list.begin();
	} else {
		return max_block;
	}
}

void SingleDbBlockManager::MarkBlockAsFree(block_id_t block_id) {
	lock_guard<mutex> lock(block_lock);
	D_ASSERT(block_id >= 0);
	D_ASSERT(block_id < max_block);
	if (free_list.find(block_id) != free_list.end()) {
		throw InternalException("MarkBlockAsFree called but block %llu was already freed!", block_id);
	}
	multi_use_blocks.erase(block_id);
	free_list.insert(block_id);
	newly_freed_list.insert(block_id);
}

void SingleDbBlockManager::MarkBlockAsUsed(block_id_t block_id) {
	lock_guard<mutex> lock(block_lock);
	D_ASSERT(block_id >= 0);
	if (max_block <= block_id) {
		// the block is past the current max_block
		// in this case we need to increment  "max_block" to "block_id"
		// any blocks in the middle are added to the free list
		// i.e. if max_block = 0, and block_id = 3, we need to add blocks 1 and 2 to the free list
		while (max_block < block_id) {
			free_list.insert(max_block);
			max_block++;
		}
		max_block++;
	} else if (free_list.find(block_id) != free_list.end()) {
		// block is currently in the free list - erase
		free_list.erase(block_id);
		newly_freed_list.erase(block_id);
	} else {
		// block is already in use - increase reference count
		IncreaseBlockReferenceCountInternal(block_id);
	}
}

void SingleDbBlockManager::MarkBlockAsModified(block_id_t block_id) {
	lock_guard<mutex> lock(block_lock);
	D_ASSERT(block_id >= 0);
	D_ASSERT(block_id < max_block);

	// check if the block is a multi-use block
	auto entry = multi_use_blocks.find(block_id);
	if (entry != multi_use_blocks.end()) {
		// it is! reduce the reference count of the block
		entry->second--;
		// check the reference count: is the block still a multi-use block?
		if (entry->second <= 1) {
			// no longer a multi-use block!
			multi_use_blocks.erase(entry);
		}
		return;
	}
	// Check for multi-free
	// TODO: Fix the bug that causes this assert to fire, then uncomment it.
	// D_ASSERT(modified_blocks.find(block_id) == modified_blocks.end());
	D_ASSERT(free_list.find(block_id) == free_list.end());
	modified_blocks.insert(block_id);
}

void SingleDbBlockManager::IncreaseBlockReferenceCountInternal(block_id_t block_id) {
	D_ASSERT(block_id >= 0);
	D_ASSERT(block_id < max_block);
	D_ASSERT(free_list.find(block_id) == free_list.end());
	auto entry = multi_use_blocks.find(block_id);
	if (entry != multi_use_blocks.end()) {
		entry->second++;
	} else {
		multi_use_blocks[block_id] = 2;
	}
}

void SingleDbBlockManager::VerifyBlocks(const unordered_map<block_id_t, idx_t> &block_usage_count) {
	// probably don't need this?
	lock_guard<mutex> lock(block_lock);
	// all blocks should be accounted for - either in the block_usage_count, or in the free list
	set<block_id_t> referenced_blocks;
	for (auto &block : block_usage_count) {
		if (block.first == INVALID_BLOCK) {
			continue;
		}
		if (block.first >= max_block) {
			throw InternalException("Block %lld is used, but it is bigger than the max block %d", block.first,
			                        max_block);
		}
		referenced_blocks.insert(block.first);
		if (block.second > 1) {
			// multi-use block
			auto entry = multi_use_blocks.find(block.first);
			if (entry == multi_use_blocks.end()) {
				throw InternalException("Block %lld was used %llu times, but not present in multi_use_blocks",
				                        block.first, block.second);
			}
			if (entry->second != block.second) {
				throw InternalException(
				    "Block %lld was used %llu times, but multi_use_blocks says it is used %llu times", block.first,
				    block.second, entry->second);
			}
		} else {
			D_ASSERT(block.second > 0);
			auto entry = free_list.find(block.first);
			if (entry != free_list.end()) {
				throw InternalException("Block %lld was used, but it is present in the free list", block.first);
			}
		}
	}
	for (auto &free_block : free_list) {
		referenced_blocks.insert(free_block);
	}
	if (referenced_blocks.size() != NumericCast<idx_t>(max_block)) {
		// not all blocks are accounted for
		string missing_blocks;
		for (block_id_t i = 0; i < max_block; i++) {
			if (referenced_blocks.find(i) == referenced_blocks.end()) {
				if (!missing_blocks.empty()) {
					missing_blocks += ", ";
				}
				missing_blocks += to_string(i);
			}
		}
		throw InternalException(
		    "Blocks %s were neither present in the free list or in the block_usage_count (max block %lld)",
		    missing_blocks, max_block);
	}
}

void SingleDbBlockManager::IncreaseBlockReferenceCount(block_id_t block_id) {
	lock_guard<mutex> lock(block_lock);
	IncreaseBlockReferenceCountInternal(block_id);
}

idx_t SingleDbBlockManager::GetMetaBlock() {
	return meta_block;
}

idx_t SingleDbBlockManager::TotalBlocks() {
	lock_guard<mutex> lock(block_lock);
	return NumericCast<idx_t>(max_block);
}

idx_t SingleDbBlockManager::FreeBlocks() {
	lock_guard<mutex> lock(block_lock);
	return free_list.size();
}

vector<MetadataHandle> SingleDbBlockManager::GetFreeListBlocks() {
	vector<MetadataHandle> free_list_blocks;
	auto &metadata_manager = GetMetadataManager();

	// reserve all blocks that we are going to write the free list to
	// since these blocks are no longer free we cannot just include them in the free list!
	auto block_size = metadata_manager.GetMetadataBlockSize() - sizeof(idx_t);
	idx_t allocated_size = 0;
	while (true) {
		auto free_list_size = sizeof(uint64_t) + sizeof(block_id_t) * (free_list.size() + modified_blocks.size());
		auto multi_use_blocks_size =
		    sizeof(uint64_t) + (sizeof(block_id_t) + sizeof(uint32_t)) * multi_use_blocks.size();
		auto metadata_blocks =
		    sizeof(uint64_t) + (sizeof(block_id_t) + sizeof(idx_t)) * GetMetadataManager().BlockCount();
		auto total_size = free_list_size + multi_use_blocks_size + metadata_blocks;
		if (total_size < allocated_size) {
			break;
		}
		auto free_list_handle = GetMetadataManager().AllocateHandle();
		free_list_blocks.push_back(std::move(free_list_handle));
		allocated_size += block_size;
	}

	return free_list_blocks;
}

} // namespace duckdb
