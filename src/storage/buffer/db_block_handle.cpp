#include "duckdb/storage/buffer/db_block_handle.hpp"

#include "duckdb/storage/block_manager.hpp"
#include "duckdb/storage/buffer/file_buffer_handle.hpp"
#include "duckdb/storage/buffer_manager.hpp"

namespace duckdb {

DbBlockHandle::DbBlockHandle(BlockManager &block_manager, block_id_t block_id_p, MemoryTag tag)
    : block_manager(block_manager), readers(0), block_id(block_id_p), tag(tag), buffer_type(DBBufferType::BLOCK),
      eviction_seq_num(0), destroy_buffer_upon(DestroyBufferUpon::BLOCK),
      memory_charge(tag, block_manager.buffer_manager.GetFileBufferPool()), unswizzled(nullptr),
      eviction_queue_idx(DConstants::INVALID_INDEX) {
	eviction_seq_num = 0;
	state = BlockState::BLOCK_UNLOADED;
	memory_usage = block_manager.GetBlockAllocSize();
}

DbBlockHandle::DbBlockHandle(BlockManager &block_manager, block_id_t block_id_p, MemoryTag tag,
                             DestroyBufferUpon destroy_buffer_upon, idx_t block_size,
                             DbBufferPoolReservation &&reservation, DBBufferType type)
    : block_manager(block_manager), readers(0), block_id(block_id_p), tag(tag), buffer_type(type), eviction_seq_num(0),
      destroy_buffer_upon(destroy_buffer_upon), memory_charge(tag, block_manager.buffer_manager.GetFileBufferPool()),
      unswizzled(nullptr), eviction_queue_idx(DConstants::INVALID_INDEX) {
	state = BlockState::BLOCK_LOADED;
	memory_usage = block_size;
	memory_charge = std::move(reservation);
}

DbBlockHandle::~DbBlockHandle() {
}

void DbBlockHandle::ChangeMemoryUsage(BlockLock &l, int64_t delta) {
	VerifyMutex(l);

	D_ASSERT(delta < 0);
	memory_usage += static_cast<idx_t>(delta);
	memory_charge.Resize(memory_usage);
}

void DbBlockHandle::VerifyMutex(BlockLock &l) const {
	D_ASSERT(l.owns_lock());
	D_ASSERT(l.mutex() == &lock);
}

DbBufferPoolReservation &DbBlockHandle::GetMemoryCharge(BlockLock &l) {
	VerifyMutex(l);
	return memory_charge;
}

void DbBlockHandle::MergeMemoryReservation(BlockLock &l, DbBufferPoolReservation reservation) {
	VerifyMutex(l);
	memory_charge.Merge(std::move(reservation));
}

void DbBlockHandle::ResizeMemory(BlockLock &l, idx_t alloc_size) {
	VerifyMutex(l);
	memory_charge.Resize(alloc_size);
}

bool DbBlockHandle::CanUnload() const {
	if (state == BlockState::BLOCK_UNLOADED) {
		// already unloaded
		return false;
	}
	if (readers > 0) {
		// there are active readers
		return false;
	}
	if (block_id >= MAXIMUM_BLOCK && MustWriteToTemporaryFile() &&
	    !block_manager.buffer_manager.HasTemporaryDirectory()) {
		// this block cannot be destroyed upon evict/unpin
		// in order to unload this block we need to write it to a temporary buffer
		// however, no temporary directory is specified!
		// hence we cannot unload the block
		return false;
	}
	return true;
}

} // namespace duckdb
