#include "duckdb/storage/single_nvme_block_manager.hpp"

#include "duckdb/common/assert.hpp"
#include "duckdb/common/checksum.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/serializer/memory_stream.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/storage/metadata/metadata_reader.hpp"
#include "duckdb/storage/metadata/metadata_writer.hpp"
#include "duckdb/storage/single_db_block_manager.hpp"
#include "duckdb/storage/storage_info.hpp"
#include "duckdb/storage/storage_manager.hpp"
#include "miniz.hpp"

#include <algorithm>
#include <cstring>
#include <libxnvme.h>
#include <libxnvme_nvm.h>

namespace duckdb {

void MainHeader::CheckMagicBytes(xnvme_dev *dev) {
	data_t magic_bytes[MAGIC_BYTE_SIZE];
	if (xnvme_dev_get_geo(dev)->tbytes < MainHeader::MAGIC_BYTE_SIZE + MainHeader::MAGIC_BYTE_OFFSET) {
		xnvme_cli_perr("The device exists, but is not a valid DuckDB db file.", errno);
		return;
	}

	// handle.Read(magic_bytes, MainHeader::MAGIC_BYTE_SIZE, MainHeader::MAGIC_BYTE_OFFSET);

	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	xnvme_nvm_read(&ctx, xnvme_dev_get_nsid(dev), MainHeader::MAGIC_BYTE_OFFSET, 1, magic_bytes, nullptr);

	if (memcmp(magic_bytes, MainHeader::MAGIC_BYTES, MainHeader::MAGIC_BYTE_SIZE) != 0) {
		xnvme_cli_perr("The device exists, but is not a valid DuckDB db file.", errno);
		return;
	}
}

SingleNvmeBlockManager::SingleNvmeBlockManager(AttachedDatabase &db, const string &path,
                                               const StorageManagerOptions &options)
    : SingleDbBlockManager(db, path, options),
      header_buffer(Allocator::Get(db), DBBufferType::MANAGED_BUFFER,
                    Storage::FILE_HEADER_SIZE - Storage::DEFAULT_BLOCK_HEADER_SIZE) {
}

SingleNvmeBlockManager::~SingleNvmeBlockManager() {
	xnvme_dev_close(dev);
}

void SingleNvmeBlockManager::CreateNewDatabase() {
	struct xnvme_opts opts = xnvme_opts_default();
	dev = xnvme_dev_open(path.c_str(), &opts);
	if (!dev) {
		xnvme_cli_perr("xnvme_dev_open()", errno);
		return;
	}

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

	// We create the SingleNvmeBlockManager with the desired block allocation size before calling CreateNewDatabase.
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

	// We create the SingleNvmeBlockManager with the desired block allocation size before calling CreateNewDatabase.
	h2.block_alloc_size = GetBlockAllocSize();
	h2.vector_size = STANDARD_VECTOR_SIZE;
	h2.serialization_compatibility = options.storage_version.GetIndex();
	SerializeHeaderStructure<DatabaseHeader>(h2, header_buffer.buffer);
	ChecksumAndWrite(header_buffer, Storage::FILE_HEADER_SIZE * 2ULL);

	// ensure that writing to disk is completed before returning
	FileSync();

	// we start with h2 as active_header, this way our initial write will be in h1
	iteration_count = 0;
	active_header = 1;
	max_block = 0;
}

void SingleNvmeBlockManager::LoadExistingDatabase() {
	struct xnvme_opts opts = xnvme_opts_default();
	dev = xnvme_dev_open(path.c_str(), &opts);
	if (!dev) {
		xnvme_cli_perr("xnvme_dev_open()", errno);
		return;
	}

	MainHeader::CheckMagicBytes(dev);

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

void SingleNvmeBlockManager::ReadAndChecksum(NvmeBuffer &buf, uint64_t location) const {
	// read the buffer from disk
	buf.Read(dev, location);

	// compute the checksum
	auto stored_checksum = Load<uint64_t>(buf.InternalBuffer());
	auto computed_checksum = Checksum(buf.buffer, buf.Size());

	// verify the checksum
	if (stored_checksum != computed_checksum) {
		throw IOException("Corrupt database file: computed checksum %llu does not match stored checksum %llu in block "
		                  "at location %llu",
		                  computed_checksum, stored_checksum, location);
	}
}

void SingleNvmeBlockManager::ChecksumAndWrite(NvmeBuffer &buf, uint64_t location) const {
	// compute the checksum and write it to the start of the buffer (if not temp buffer)
	uint64_t checksum = Checksum(buf.buffer, buf.Size());
	Store<uint64_t>(checksum, buf.InternalBuffer());
	// now write the buffer
	buf.Write(dev, location);
}

bool SingleNvmeBlockManager::IsRemote() {
	return false;
}

unique_ptr<NvmeBlock> SingleNvmeBlockManager::ConvertBlock(block_id_t block_id, NvmeBuffer &source_buffer) {
	D_ASSERT(source_buffer.AllocSize() == GetBlockAllocSize());
	return make_uniq<NvmeBlock>(source_buffer, block_id);
}

unique_ptr<NvmeBlock> SingleNvmeBlockManager::CreateBlock(block_id_t block_id, NvmeBuffer *source_buffer) {
	unique_ptr<NvmeBlock> result;
	if (source_buffer) {
		result = ConvertBlock(block_id, *source_buffer);
	} else {
		result = make_uniq<NvmeBlock>(Allocator::Get(db), block_id, GetBlockSize());
	}
	result->Initialize(options.debug_initialize);
	return result;
}

idx_t SingleNvmeBlockManager::GetBlockLocation(block_id_t block_id) {
	return BLOCK_START + NumericCast<idx_t>(block_id) * GetBlockAllocSize();
}

void SingleNvmeBlockManager::Read(NvmeBlock &block) {
	D_ASSERT(block.id >= 0);
	D_ASSERT(std::find(free_list.begin(), free_list.end(), block.id) == free_list.end());
	ReadAndChecksum(block, GetBlockLocation(block.id));
}

void SingleNvmeBlockManager::ReadBlocks(NvmeBuffer &buffer, block_id_t start_block, idx_t block_count) {
	D_ASSERT(start_block >= 0);
	D_ASSERT(block_count >= 1);

	// read the buffer from disk
	auto location = GetBlockLocation(start_block);
	buffer.Read(dev, location);

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
	// TODOTODO: can we do this with xnvme?
	return;
}

void SingleNvmeBlockManager::FileSync() {
	// TODOTODO: can we do this with xnvme?
	return;
}

void SingleNvmeBlockManager::TrimFreeBlocks() {
	// TODOTODO: can we do this with xnvme?
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
	FileSync();

	// Release the free blocks to the filesystem.
	TrimFreeBlocks();
}

} // namespace duckdb
