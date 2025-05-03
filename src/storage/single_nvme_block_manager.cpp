#include "duckdb/storage/single_nvme_block_manager.hpp"

#include "duckdb/common/allocator.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/nvme_buffer.hpp"
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
#include <libxnvme.h>

namespace duckdb {

void MainHeader::CheckMagicBytes(NvmeBuffer dev_buf) {
	data_t magic_bytes[MAGIC_BYTE_SIZE];
	if (dev_buf.Size() < MainHeader::MAGIC_BYTE_SIZE + MainHeader::MAGIC_BYTE_OFFSET) {
		throw IOException("The device exists, but it is not a valid DuckDB database file!");
	}

	dev_buf.Read(MAGIC_BYTE_OFFSET);
	memcpy(magic_bytes, dev_buf.InternalBuffer(), MAGIC_BYTE_SIZE);

	if (memcmp(magic_bytes, MainHeader::MAGIC_BYTES, MainHeader::MAGIC_BYTE_SIZE) != 0) {
		throw IOException("The device exists, but it is not a valid DuckDB database file!");
	}
}

SingleNvmeBlockManager::SingleNvmeBlockManager(AttachedDatabase &db, const string &path_p,
                                               const StorageManagerOptions &options)
    : SingleBaseBlockManager(db, path_p, options), dev_buf() {
}

void SingleNvmeBlockManager::CreateNewDatabase() {
	struct xnvme_opts opts = xnvme_opts_default();
	xnvme_dev *dev = xnvme_dev_open(path.c_str(), &opts);
	if (!dev) {
		xnvme_cli_perr("xnvme_dev_open()", errno);
	}

	dev_buf = NvmeBuffer(dev);

	// if we create a new file, we fill the metadata of the file
	// first fill in the new header
	dev_buf.Clear();

	options.version_number = GetVersionNumber();
	db.GetStorageManager().SetStorageVersion(options.storage_version.GetIndex());
	AddStorageVersionTag();

	MainHeader main_header = ConstructMainHeader(options.version_number.GetIndex());
	SerializeHeaderStructure<MainHeader>(main_header, dev_buf.buffer);
	// now write the header to the file
	ChecksumAndWrite(dev_buf, 0);
	dev_buf.Clear();

	// write the database headers
	// initialize meta_block and free_list to INVALID_BLOCK because the database file does not contain any actual
	// content yet
	DatabaseHeader h1;
	// header 1
	h1.iteration = 0;
	h1.meta_block = idx_t(INVALID_BLOCK);
	h1.free_list = idx_t(INVALID_BLOCK);
	h1.block_count = 0;
	// We create the SingleNvmeBlockManager with the desired block allocation size before calling CreateNewDatabase.
	h1.block_alloc_size = GetBlockAllocSize();
	h1.vector_size = STANDARD_VECTOR_SIZE;
	h1.serialization_compatibility = options.storage_version.GetIndex();
	SerializeHeaderStructure<DatabaseHeader>(h1, dev_buf.buffer);
	ChecksumAndWrite(dev_buf, Storage::FILE_HEADER_SIZE);

	// header 2
	DatabaseHeader h2;
	h2.iteration = 0;
	h2.meta_block = idx_t(INVALID_BLOCK);
	h2.free_list = idx_t(INVALID_BLOCK);
	h2.block_count = 0;
	// We create the SingleNvmeBlockManager with the desired block allocation size before calling CreateNewDatabase.
	h2.block_alloc_size = GetBlockAllocSize();
	h2.vector_size = STANDARD_VECTOR_SIZE;
	h2.serialization_compatibility = options.storage_version.GetIndex();
	SerializeHeaderStructure<DatabaseHeader>(h2, dev_buf.buffer);
	ChecksumAndWrite(dev_buf, Storage::FILE_HEADER_SIZE * 2ULL);

	// ensure that writing to disk is completed before returning
	FileSync();
	// we start with h2 as active_header, this way our initial write will be in h1
	iteration_count = 0;
	active_header = 1;
	max_block = 0;
}

void SingleNvmeBlockManager::LoadExistingDatabase() {
	struct xnvme_opts opts = xnvme_opts_default();
	xnvme_dev *dev = xnvme_dev_open(path.c_str(), &opts);
	if (!dev) {
		xnvme_cli_perr("xnvme_dev_open()", errno);
	}

	dev_buf = NvmeBuffer(dev);

	MainHeader::CheckMagicBytes(dev_buf);
	// otherwise, we check the metadata of the file
	ReadAndChecksum(dev_buf, 0);
	MainHeader main_header = DeserializeMainHeader(dev_buf.buffer);
	options.version_number = main_header.version_number;

	// read the database headers from disk
	DatabaseHeader h1;
	ReadAndChecksum(dev_buf, Storage::FILE_HEADER_SIZE);
	h1 = DeserializeDatabaseHeader(main_header, dev_buf.buffer);

	DatabaseHeader h2;
	ReadAndChecksum(dev_buf, Storage::FILE_HEADER_SIZE * 2ULL);
	h2 = DeserializeDatabaseHeader(main_header, dev_buf.buffer);

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

void SingleNvmeBlockManager::ReadAndChecksum(NvmeBuffer &block_buf, uint64_t location) const {
	// read the buffer from disk
	block_buf.Read(location);

	// compute the checksum
	auto stored_checksum = Load<uint64_t>(block_buf.InternalBuffer());
	auto computed_checksum = Checksum(block_buf.buffer, block_buf.Size());

	// verify the checksum
	if (stored_checksum != computed_checksum) {
		throw IOException("Corrupt database file: computed checksum %llu does not match stored checksum %llu in block "
		                  "at location %llu",
		                  computed_checksum, stored_checksum, location);
	}
}

void SingleNvmeBlockManager::ChecksumAndWrite(NvmeBuffer &block, uint64_t location) const {
	// compute the checksum and write it to the start of the buffer (if not temp buffer)
	uint64_t checksum = Checksum(block.buffer, block.Size());
	Store<uint64_t>(checksum, block.InternalBuffer());
	// now write the buffer
	block.Write(location);
}

bool SingleNvmeBlockManager::IsRemote() {
	return false;
}

unique_ptr<Block> SingleNvmeBlockManager::ConvertBlock(block_id_t block_id, NvmeBuffer &source_buffer) {
	D_ASSERT(source_buffer.AllocSize() == GetBlockAllocSize());
	unique_ptr<NvmeBlock> ptr = make_uniq<NvmeBlock>(source_buffer, block_id);
	return std::unique_ptr<Block>(static_cast<Block *>(ptr.release()));
}

unique_ptr<Block> SingleNvmeBlockManager::CreateBlock(block_id_t block_id, NvmeBuffer *source_buffer) {
	unique_ptr<NvmeBlock> result;
	if (source_buffer) {
		result = ConvertBlock(block_id, *source_buffer);
	} else {
		throw IOException("Block must be create from Buffer.");
	}
	result->Initialize(options.debug_initialize);
	unique_ptr<NvmeBlock> ptr = make_uniq<NvmeBlock>(source_buffer, block_id);
	return std::unique_ptr<Block>(static_cast<Block *>(ptr.release()));
}

void SingleNvmeBlockManager::ReadBlocks(NvmeBuffer &buffer, block_id_t start_block, idx_t block_count) {
	D_ASSERT(start_block >= 0);
	D_ASSERT(block_count >= 1);

	// read the buffer from disk
	auto location = GetBlockLocation(start_block);
	buffer.Read(location);

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

void SingleNvmeBlockManager::Write(NvmeBuffer &buffer, block_id_t block_id) {
	D_ASSERT(block_id >= 0);
	ChecksumAndWrite(buffer, BLOCK_START + NumericCast<idx_t>(block_id) * GetBlockAllocSize());
}

void SingleNvmeBlockManager::Truncate() {
	// TODOTODO: can we truncate with xnvme?
	return;
}

void SingleNvmeBlockManager::WriteHeader(DatabaseHeader header) {
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
	FileSync();

	dev_buf.Clear();
	// if we are upgrading the database from version 64 -> version 65, we need to re-write the main header
	if (options.version_number.GetIndex() == 64 && options.storage_version.GetIndex() >= 4) {
		// rewrite the main header
		options.version_number = 65;
		MainHeader main_header = ConstructMainHeader(options.version_number.GetIndex());
		SerializeHeaderStructure<MainHeader>(main_header, dev_buf.buffer);
		// now write the header to the file
		ChecksumAndWrite(dev_buf, 0);
		dev_buf.Clear();
	}

	// set the header inside the buffer
	MemoryStream serializer(Allocator::Get(db));
	header.Write(serializer);
	memcpy(dev_buf.buffer, serializer.GetData(), serializer.GetPosition());
	// now write the header to the file, active_header determines whether we write to h1 or h2
	// note that if active_header is h1 we write to h2, and vice versa
	ChecksumAndWrite(dev_buf, active_header == 1 ? Storage::FILE_HEADER_SIZE : Storage::FILE_HEADER_SIZE * 2);
	// switch active header to the other header
	active_header = 1 - active_header;
	//! Ensure the header write ends up on disk
	FileSync();
	// Release the free blocks to the filesystem.
	TrimFreeBlocks();
}

void SingleNvmeBlockManager::FileSync() {
	// TODOTODO: should we sync with xnvme?
	return;
}

void SingleNvmeBlockManager::TrimFreeBlocks() {
	// TODOTODO: can we trim with xnvme?
	return;
}
} // namespace duckdb
