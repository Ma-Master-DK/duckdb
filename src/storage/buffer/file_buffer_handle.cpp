#include "duckdb/storage/buffer/file_buffer_handle.hpp"
#include "duckdb/storage/buffer_manager.hpp"
#include "duckdb/storage/buffer/file_block_handle.hpp"

namespace duckdb {

FileBufferHandle::FileBufferHandle() : handle(nullptr), node(nullptr) {
}

FileBufferHandle::FileBufferHandle(shared_ptr<FileBlockHandle> handle, optional_ptr<FileBuffer> node)
    : handle(std::move(handle)), node(node) {
}

FileBufferHandle::FileBufferHandle(FileBufferHandle &&other) noexcept : node(nullptr) {
	std::swap(node, other.node);
	std::swap(handle, other.handle);
}

FileBufferHandle &FileBufferHandle::operator=(FileBufferHandle &&other) noexcept {
	std::swap(node, other.node);
	std::swap(handle, other.handle);
	return *this;
}

FileBufferHandle::~FileBufferHandle() {
	Destroy();
}

bool FileBufferHandle::IsValid() const {
	return node != nullptr;
}

void FileBufferHandle::Destroy() {
	if (!handle || !IsValid()) {
		return;
	}
	handle->block_manager.buffer_manager.Unpin(handle);
	handle.reset();
	node = nullptr;
}

FileBuffer &FileBufferHandle::GetFileBuffer() {
	D_ASSERT(node);
	return *node;
}

} // namespace duckdb
