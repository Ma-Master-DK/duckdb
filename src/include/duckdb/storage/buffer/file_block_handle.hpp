//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/buffer/file_block_handle.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/atomic.hpp"
#include "duckdb/common/common.hpp"
#include "duckdb/common/enums/destroy_buffer_upon.hpp"
#include "duckdb/common/enums/memory_tag.hpp"
#include "duckdb/common/file_buffer.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/common/optional_idx.hpp"
#include "duckdb/storage/buffer/db_block_handle.hpp"
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class BlockManager;
class FileBufferHandle;
class FileBufferPool;
class DatabaseInstance;

// struct FileBufferPoolReservation {
// 	MemoryTag tag;
// 	idx_t size {0};
// 	FileBufferPool &pool;
//
// 	FileBufferPoolReservation(MemoryTag tag, FileBufferPool &pool);
// 	FileBufferPoolReservation(const FileBufferPoolReservation &) = delete;
// 	FileBufferPoolReservation &operator=(const FileBufferPoolReservation &) = delete;
//
// 	FileBufferPoolReservation(FileBufferPoolReservation &&) noexcept;
// 	FileBufferPoolReservation &operator=(FileBufferPoolReservation &&) noexcept;
//
// 	~FileBufferPoolReservation();
//
// 	void Resize(idx_t new_size);
// 	void Merge(FileBufferPoolReservation src);
// };

// struct TempFileBufferPoolReservation : FileBufferPoolReservation {
// 	TempFileBufferPoolReservation(MemoryTag tag, FileBufferPool &pool, idx_t size)
// 	    : FileBufferPoolReservation(tag, pool) {
// 		Resize(size);
// 	}
//
// 	TempFileBufferPoolReservation(TempFileBufferPoolReservation &&) = default;
//
// 	~TempFileBufferPoolReservation() {
// 		Resize(0);
// 	}
// };

class FileBlockHandle : DbBlockHandle, public enable_shared_from_this<FileBlockHandle> {
public:
	FileBlockHandle(BlockManager &block_manager, block_id_t block_id, MemoryTag tag);
	FileBlockHandle(BlockManager &block_manager, block_id_t block_id, MemoryTag tag, unique_ptr<FileBuffer> buffer,
	                DestroyBufferUpon destroy_buffer_upon, idx_t block_size, DbBufferPoolReservation &&reservation);

	~FileBlockHandle() override;

public:
	//! Gets a reference to the buffer - the lock must be held
	unique_ptr<FileBuffer> &GetBuffer(BlockLock &l);

	void ResizeBuffer(BlockLock &, idx_t block_size, int64_t memory_delta) override;

	void Unload(BlockLock &) override;

	FileBufferHandle Load(unique_ptr<FileBuffer> buffer = nullptr);
	FileBufferHandle LoadFromBuffer(BlockLock &l, data_ptr_t data, unique_ptr<FileBuffer> reusable_buffer,
	                                DbBufferPoolReservation reservation);

	unique_ptr<FileBuffer> UnloadAndTakeBlock(BlockLock &);

	void ConvertToPersistent(BlockLock &, FileBlockHandle &new_block, unique_ptr<FileBuffer> new_buffer);

private:
private:
	//! Pointer to loaded data (if any)
	unique_ptr<FileBuffer> buffer;
};

} // namespace duckdb
