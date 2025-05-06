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
#include "duckdb/storage/storage_info.hpp"

namespace duckdb {

class BlockManager;
class FileBufferHandle;
class FileBufferPool;
class DatabaseInstance;

enum class BlockState : uint8_t { BLOCK_UNLOADED = 0, BLOCK_LOADED = 1 };

struct FileBufferPoolReservation {
	MemoryTag tag;
	idx_t size {0};
	FileBufferPool &pool;

	FileBufferPoolReservation(MemoryTag tag, FileBufferPool &pool);
	FileBufferPoolReservation(const FileBufferPoolReservation &) = delete;
	FileBufferPoolReservation &operator=(const FileBufferPoolReservation &) = delete;

	FileBufferPoolReservation(FileBufferPoolReservation &&) noexcept;
	FileBufferPoolReservation &operator=(FileBufferPoolReservation &&) noexcept;

	~FileBufferPoolReservation();

	void Resize(idx_t new_size);
	void Merge(FileBufferPoolReservation src);
};

struct TempFileBufferPoolReservation : FileBufferPoolReservation {
	TempFileBufferPoolReservation(MemoryTag tag, FileBufferPool &pool, idx_t size)
	    : FileBufferPoolReservation(tag, pool) {
		Resize(size);
	}

	TempFileBufferPoolReservation(TempFileBufferPoolReservation &&) = default;

	~TempFileBufferPoolReservation() {
		Resize(0);
	}
};

using BlockLock = unique_lock<mutex>;

class FileBlockHandle : public enable_shared_from_this<FileBlockHandle> {
public:
	FileBlockHandle(BlockManager &block_manager, block_id_t block_id, MemoryTag tag);
	FileBlockHandle(BlockManager &block_manager, block_id_t block_id, MemoryTag tag, unique_ptr<FileBuffer> buffer,
	                DestroyBufferUpon destroy_buffer_upon, idx_t block_size, FileBufferPoolReservation &&reservation);

	~FileBlockHandle();

	BlockManager &block_manager;

public:
	block_id_t BlockId() const {
		return block_id;
	}

	idx_t EvictionSequenceNumber() const {
		return eviction_seq_num;
	}

	idx_t NextEvictionSequenceNumber() {
		return ++eviction_seq_num;
	}

	int32_t Readers() const {
		return readers;
	}
	int32_t DecrementReaders() {
		return --readers;
	}

	inline bool IsSwizzled() const {
		return !unswizzled;
	}

	inline void SetSwizzling(const char *unswizzler) {
		unswizzled = unswizzler;
	}

	MemoryTag GetMemoryTag() const {
		return tag;
	}

	inline void SetDestroyBufferUpon(DestroyBufferUpon destroy_buffer_upon_p) {
		destroy_buffer_upon = destroy_buffer_upon_p;
	}

	inline bool MustAddToEvictionQueue() const {
		return destroy_buffer_upon != DestroyBufferUpon::UNPIN;
	}

	inline bool MustWriteToTemporaryFile() const {
		return destroy_buffer_upon == DestroyBufferUpon::BLOCK;
	}

	inline idx_t GetMemoryUsage() const {
		return memory_usage;
	}

	bool IsUnloaded() const {
		return state == BlockState::BLOCK_UNLOADED;
	}

	void SetEvictionQueueIndex(const idx_t index) {
		// can only be set once
		D_ASSERT(eviction_queue_idx == DConstants::INVALID_INDEX);
		// MANAGED_BUFFER only (at least, for now)
		D_ASSERT(GetBufferType() == DBBufferType::MANAGED_BUFFER);
		eviction_queue_idx = index;
	}

	idx_t GetEvictionQueueIndex() const {
		return eviction_queue_idx;
	}

	DBBufferType GetBufferType() const {
		return buffer_type;
	}

	BlockState GetState() const {
		return state;
	}

	int64_t GetLRUTimestamp() const {
		return lru_timestamp_msec;
	}

	void SetLRUTimestamp(int64_t timestamp_msec) {
		lru_timestamp_msec = timestamp_msec;
	}

	BlockLock GetLock() {
		return BlockLock(lock);
	}

	//! Gets a reference to the buffer - the lock must be held
	unique_ptr<FileBuffer> &GetBuffer(BlockLock &l);

	void ChangeMemoryUsage(BlockLock &l, int64_t delta);
	FileBufferPoolReservation &GetMemoryCharge(BlockLock &l);

	//! Merge a new memory reservation
	void MergeMemoryReservation(BlockLock &, FileBufferPoolReservation reservation);

	//! Resize the memory allocation
	void ResizeMemory(BlockLock &, idx_t alloc_size);

	//! Resize the actual buffer
	void ResizeBuffer(BlockLock &, idx_t block_size, int64_t memory_delta);

	FileBufferHandle Load(unique_ptr<FileBuffer> buffer = nullptr);
	FileBufferHandle LoadFromBuffer(BlockLock &l, data_ptr_t data, unique_ptr<FileBuffer> reusable_buffer,
	                                FileBufferPoolReservation reservation);

	unique_ptr<FileBuffer> UnloadAndTakeBlock(BlockLock &);
	void Unload(BlockLock &);

	//! Returns whether or not the block can be unloaded
	//! Note that while this method does not require a lock, whether or not a block can be unloaded can change if the
	//! lock is not held
	bool CanUnload() const;

	void ConvertToPersistent(BlockLock &, FileBlockHandle &new_block, unique_ptr<FileBuffer> new_buffer);

private:
	void VerifyMutex(unique_lock<mutex> &l) const;

private:
	//! The block-level lock
	mutex lock;

	//! Whether or not the block is loaded/unloaded
	atomic<BlockState> state;

	//! Amount of concurrent readers
	atomic<int32_t> readers;

	//! The block id of the block
	const block_id_t block_id;

	//! Memory tag
	const MemoryTag tag;

	//! File buffer type
	const DBBufferType buffer_type;

	//! Pointer to loaded data (if any)
	unique_ptr<FileBuffer> buffer;

	//! Internal eviction sequence number
	atomic<idx_t> eviction_seq_num;

	//! LRU timestamp (for age-based eviction)
	atomic<int64_t> lru_timestamp_msec;

	//! When to destroy the data buffer
	atomic<DestroyBufferUpon> destroy_buffer_upon;

	//! The memory usage of the block (when loaded). If we are pinning/loading
	//! an unloaded block, this tells us how much memory to reserve.
	atomic<idx_t> memory_usage;

	//! Current memory reservation / usage
	FileBufferPoolReservation memory_charge;

	//! Does the block contain any memory pointers?
	const char *unswizzled;

	//! Index for eviction queue (DBBufferType::MANAGED_BUFFER only, for now)
	atomic<idx_t> eviction_queue_idx;
};

} // namespace duckdb
