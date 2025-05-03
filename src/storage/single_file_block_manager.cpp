#include "duckdb/storage/single_file_block_manager.hpp"

#include "duckdb/common/allocator.hpp"
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
#include <memory>

namespace duckdb {

void MainHeader::CheckMagicBytes(FileHandle &handle) {
	data_t magic_bytes[MAGIC_BYTE_SIZE];
	if (handle.GetFileSize() < MainHeader::MAGIC_BYTE_SIZE + MainHeader::MAGIC_BYTE_OFFSET) {
		throw IOException("The file \"%s\" exists, but it is not a valid DuckDB database file!", handle.path);
	}
	handle.Read(magic_bytes, MainHeader::MAGIC_BYTE_SIZE, MainHeader::MAGIC_BYTE_OFFSET);
	if (memcmp(magic_bytes, MainHeader::MAGIC_BYTES, MainHeader::MAGIC_BYTE_SIZE) != 0) {
		throw IOException("The file \"%s\" exists, but it is not a valid DuckDB database file!", handle.path);
	}
}

SingleFileBlockManager::SingleFileBlockManager(AttachedDatabase &db, const string &path_p,
                                               const StorageManagerOptions &options)
    : SingleBaseBlockManager(db, path_p, options),
      header_buffer(Allocator::Get(db), DBBufferType::MANAGED_BUFFER,
                    Storage::FILE_HEADER_SIZE - Storage::DEFAULT_BLOCK_HEADER_SIZE) {
}

FileOpenFlags SingleFileBlockManager::GetFileFlags(bool create_new) const {
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

void SingleFileBlockManager::CreateNewDatabase() {
	auto flags = GetFileFlags(true);

	// open the RDBMS handle
	auto &fs = FileSystem::Get(db);
	handle = fs.OpenFile(path, flags);

	// if we create a new file, we fill the metadata of the file
	// first fill in the new header
	header_buffer.Clear();

	options.version_number = GetVersionNumber();
	db.GetStorageManager().SetStorageVersion(options.storage_version.GetIndex());
	AddStorageVersionTag();

	MainHeader main_header = ConstructMainHeader(options.version_number.GetIndex());
	SerializeHeaderStructure<MainHeader>(main_header, header_buffer.buffer);
	// now write the header to the file
	ChecksumAndWrite(header_buffer, 0);
	header_buffer.Clear();

	// write the database headers
	// initialize meta_block and free_list to INVALID_BLOCK because the database file does not contain any actual
	// content yet
	DatabaseHeader h1;
	// header 1
	h1.iteration = 0;
	h1.meta_block = idx_t(INVALID_BLOCK);
	h1.free_list = idx_t(INVALID_BLOCK);
	h1.block_count = 0;
	// We create the SingleFileBlockManager with the desired block allocation size before calling CreateNewDatabase.
	h1.block_alloc_size = GetBlockAllocSize();
	h1.vector_size = STANDARD_VECTOR_SIZE;
	h1.serialization_compatibility = options.storage_version.GetIndex();
	SerializeHeaderStructure<DatabaseHeader>(h1, header_buffer.buffer);
	ChecksumAndWrite(header_buffer, Storage::FILE_HEADER_SIZE);

	// header 2
	DatabaseHeader h2;
	h2.iteration = 0;
	h2.meta_block = idx_t(INVALID_BLOCK);
	h2.free_list = idx_t(INVALID_BLOCK);
	h2.block_count = 0;
	// We create the SingleFileBlockManager with the desired block allocation size before calling CreateNewDatabase.
	h2.block_alloc_size = GetBlockAllocSize();
	h2.vector_size = STANDARD_VECTOR_SIZE;
	h2.serialization_compatibility = options.storage_version.GetIndex();
	SerializeHeaderStructure<DatabaseHeader>(h2, header_buffer.buffer);
	ChecksumAndWrite(header_buffer, Storage::FILE_HEADER_SIZE * 2ULL);

	// ensure that writing to disk is completed before returning
	handle->Sync();
	// we start with h2 as active_header, this way our initial write will be in h1
	iteration_count = 0;
	active_header = 1;
	max_block = 0;
}

void SingleFileBlockManager::LoadExistingDatabase() {
	auto flags = GetFileFlags(false);

	// open the RDBMS handle
	auto &fs = FileSystem::Get(db);
	handle = fs.OpenFile(path, flags);
	if (!handle) {
		// this can only happen in read-only mode - as that is when we set FILE_FLAGS_NULL_IF_NOT_EXISTS
		throw IOException("Cannot open database \"%s\" in read-only mode: database does not exist", path);
	}

	MainHeader::CheckMagicBytes(*handle);
	// otherwise, we check the metadata of the file
	ReadAndChecksum(header_buffer, 0);
	MainHeader main_header = DeserializeMainHeader(header_buffer.buffer);
	options.version_number = main_header.version_number;

	// read the database headers from disk
	DatabaseHeader h1;
	ReadAndChecksum(header_buffer, Storage::FILE_HEADER_SIZE);
	h1 = DeserializeDatabaseHeader(main_header, header_buffer.buffer);

	DatabaseHeader h2;
	ReadAndChecksum(header_buffer, Storage::FILE_HEADER_SIZE * 2ULL);
	h2 = DeserializeDatabaseHeader(main_header, header_buffer.buffer);

	// check the header with the highest iteration count
	if (h1.iteration > h2.iteration) {
		// h1 is active header
		active_header = 0;
		Initialize(h1, GetOptionalBlockAllocSize());
	} else {
		// h2 is active header
		active_header = 1;
		Initialize(h2, GetOptionalBlockAllocSize());
	}
	AddStorageVersionTag();
	LoadFreeList();
}

void SingleFileBlockManager::ReadAndChecksum(FileBuffer &block, uint64_t location) const {
	// read the buffer from disk
	block.Read(*handle, location);

	// compute the checksum
	auto stored_checksum = Load<uint64_t>(block.InternalBuffer());
	auto computed_checksum = Checksum(block.buffer, block.Size());

	// verify the checksum
	if (stored_checksum != computed_checksum) {
		throw IOException("Corrupt database file: computed checksum %llu does not match stored checksum %llu in block "
		                  "at location %llu",
		                  computed_checksum, stored_checksum, location);
	}
}

void SingleFileBlockManager::ChecksumAndWrite(FileBuffer &block, uint64_t location) const {
	// compute the checksum and write it to the start of the buffer (if not temp buffer)
	uint64_t checksum = Checksum(block.buffer, block.Size());
	Store<uint64_t>(checksum, block.InternalBuffer());
	// now write the buffer
	block.Write(*handle, location);
}

bool SingleFileBlockManager::IsRemote() {
	return !handle->OnDiskFile();
}

unique_ptr<Block> SingleFileBlockManager::ConvertBlock(block_id_t block_id, FileBuffer &source_buffer) {
	D_ASSERT(source_buffer.AllocSize() == GetBlockAllocSize());
	unique_ptr<FileBlock> ptr = make_uniq<FileBlock>(source_buffer, block_id);
	return std::unique_ptr<Block>(static_cast<Block *>(ptr.release()));
}

unique_ptr<Block> SingleFileBlockManager::CreateBlock(block_id_t block_id, FileBuffer *source_buffer) {
	unique_ptr<FileBlock> result;
	if (source_buffer) {
		result = ConvertBlock(block_id, *source_buffer);
	} else {
		result = make_uniq<FileBlock>(Allocator::Get(db), block_id, GetBlockSize());
	}
	result->Initialize(options.debug_initialize);
	unique_ptr<FileBlock> ptr = make_uniq<FileBlock>(source_buffer, block_id);
	return std::unique_ptr<Block>(static_cast<Block *>(ptr.release()));
}

void SingleFileBlockManager::ReadBlocks(FileBuffer &buffer, block_id_t start_block, idx_t block_count) {
	D_ASSERT(start_block >= 0);
	D_ASSERT(block_count >= 1);

	// read the buffer from disk
	auto location = GetBlockLocation(start_block);
	buffer.Read(*handle, location);

	// for each of the blocks - verify the checksum
	auto ptr = buffer.InternalBuffer();
	for (idx_t i = 0; i < block_count; i++) {
		// compute the checksum
		auto start_ptr = ptr + i * GetBlockAllocSize();
		auto stored_checksum = Load<uint64_t>(start_ptr);
		uint64_t computed_checksum = Checksum(start_ptr + Storage::DEFAULT_BLOCK_HEADER_SIZE, GetBlockSize());
		// verify the checksum
		if (stored_checksum != computed_checksum) {
			throw IOException(
			    "Corrupt database file: computed checksum %llu does not match stored checksum %llu in block "
			    "at location %llu",
			    computed_checksum, stored_checksum, location + i * GetBlockAllocSize());
		}
	}
}

void SingleFileBlockManager::Write(FileBuffer &buffer, block_id_t block_id) {
	D_ASSERT(block_id >= 0);
	ChecksumAndWrite(buffer, BLOCK_START + NumericCast<idx_t>(block_id) * GetBlockAllocSize());
}

void SingleFileBlockManager::Truncate() {
	BlockManager::Truncate();
	idx_t blocks_to_truncate = 0;
	// reverse iterate over the free-list
	for (auto entry = free_list.rbegin(); entry != free_list.rend(); entry++) {
		auto block_id = *entry;
		if (block_id + 1 != max_block) {
			break;
		}
		blocks_to_truncate++;
		max_block--;
	}
	if (blocks_to_truncate == 0) {
		// nothing to truncate
		return;
	}
	// truncate the file
	free_list.erase(free_list.lower_bound(max_block), free_list.end());
	newly_freed_list.erase(newly_freed_list.lower_bound(max_block), newly_freed_list.end());
	handle->Truncate(NumericCast<int64_t>(BLOCK_START + NumericCast<idx_t>(max_block) * GetBlockAllocSize()));
}

void SingleFileBlockManager::WriteHeader(DatabaseHeader header) {
	auto free_list_blocks = GetFreeListBlocks();

	// now handle the free list
	auto &metadata_manager = GetMetadataManager();
	// add all modified blocks to the free list: they can now be written to again
	metadata_manager.MarkBlocksAsModified();

	lock_guard<mutex> lock(block_lock);
	// set the iteration count
	header.iteration = ++iteration_count;

	for (auto &block : modified_blocks) {
		free_list.insert(block);
		newly_freed_list.insert(block);
	}
	modified_blocks.clear();

	if (!free_list_blocks.empty()) {
		// there are blocks to write, either in the free_list or in the modified_blocks
		// we write these blocks specifically to the free_list_blocks
		// a normal MetadataWriter will fetch blocks to use from the free_list
		// but since we are WRITING the free_list, this behavior is sub-optimal
		FreeListBlockWriter writer(metadata_manager, std::move(free_list_blocks));

		auto ptr = writer.GetMetaBlockPointer();
		header.free_list = ptr.block_pointer;

		writer.Write<uint64_t>(free_list.size());
		for (auto &block_id : free_list) {
			writer.Write<block_id_t>(block_id);
		}
		writer.Write<uint64_t>(multi_use_blocks.size());
		for (auto &entry : multi_use_blocks) {
			writer.Write<block_id_t>(entry.first);
			writer.Write<uint32_t>(entry.second);
		}
		GetMetadataManager().Write(writer);
		writer.Flush();
	} else {
		// no blocks in the free list
		header.free_list = DConstants::INVALID_INDEX;
	}
	metadata_manager.Flush();
	header.block_count = NumericCast<idx_t>(max_block);
	header.serialization_compatibility = options.storage_version.GetIndex();

	auto &config = DBConfig::Get(db);
	if (config.options.checkpoint_abort == CheckpointAbort::DEBUG_ABORT_AFTER_FREE_LIST_WRITE) {
		throw FatalException("Checkpoint aborted after free list write because of PRAGMA checkpoint_abort flag");
	}

	// We need to fsync BEFORE we write the header to ensure that all the previous blocks are written as well
	handle->Sync();

	header_buffer.Clear();
	// if we are upgrading the database from version 64 -> version 65, we need to re-write the main header
	if (options.version_number.GetIndex() == 64 && options.storage_version.GetIndex() >= 4) {
		// rewrite the main header
		options.version_number = 65;
		MainHeader main_header = ConstructMainHeader(options.version_number.GetIndex());
		SerializeHeaderStructure<MainHeader>(main_header, header_buffer.buffer);
		// now write the header to the file
		ChecksumAndWrite(header_buffer, 0);
		header_buffer.Clear();
	}

	// set the header inside the buffer
	MemoryStream serializer(Allocator::Get(db));
	header.Write(serializer);
	memcpy(header_buffer.buffer, serializer.GetData(), serializer.GetPosition());
	// now write the header to the file, active_header determines whether we write to h1 or h2
	// note that if active_header is h1 we write to h2, and vice versa
	ChecksumAndWrite(header_buffer, active_header == 1 ? Storage::FILE_HEADER_SIZE : Storage::FILE_HEADER_SIZE * 2);
	// switch active header to the other header
	active_header = 1 - active_header;
	//! Ensure the header write ends up on disk
	handle->Sync();
	// Release the free blocks to the filesystem.
	TrimFreeBlocks();
}

void SingleFileBlockManager::FileSync() {
	handle->Sync();
}

void SingleFileBlockManager::TrimFreeBlocks() {
	if (DBConfig::Get(db).options.trim_free_blocks) {
		for (auto itr = newly_freed_list.begin(); itr != newly_freed_list.end(); ++itr) {
			block_id_t first = *itr;
			block_id_t last = first;
			// Find end of contiguous range.
			for (++itr; itr != newly_freed_list.end() && (*itr == last + 1); ++itr) {
				last = *itr;
			}
			// We are now one too far.
			--itr;
			// Trim the range.
			handle->Trim(BLOCK_START + (NumericCast<idx_t>(first) * GetBlockAllocSize()),
			             NumericCast<idx_t>(last + 1 - first) * GetBlockAllocSize());
		}
	}
	newly_freed_list.clear();
}

} // namespace duckdb
