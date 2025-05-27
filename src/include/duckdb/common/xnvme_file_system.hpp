//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/xnvme_file_system.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/file_open_flags.hpp"
#include "duckdb/common/file_system.hpp"
#include <libxnvme.h>

namespace duckdb {

class XNVMEFileSystem : public FileSystem {
public:
	// FileSystem interface implementation
	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
	                                optional_ptr<FileOpener> opener = nullptr) override;

	int64_t Read(FileHandle &handle, void *buffer, int64_t nr_bytes) override;
	int64_t Write(FileHandle &handle, void *buffer, int64_t nr_bytes) override;
	void Read(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) override;
	void Write(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) override;
	bool Trim(FileHandle &handle, idx_t offset_bytes, idx_t length_bytes) override;
	int64_t GetFileSize(FileHandle &handle) override;
	time_t GetLastModifiedTime(FileHandle &handle) override;
	FileType GetFileType(FileHandle &handle) override;
	void Seek(FileHandle &handle, idx_t location) override;
	idx_t SeekPosition(FileHandle &handle) override;
	void CreateDirectory(const string &directory, optional_ptr<FileOpener> opener = nullptr) override;
	void RemoveDirectory(const string &directory, optional_ptr<FileOpener> opener = nullptr) override;
	bool ListFiles(const string &directory, const std::function<void(const string &, bool)> &callback,
	               FileOpener *opener = nullptr) override;
	void MoveFile(const string &source, const string &target, optional_ptr<FileOpener> opener = nullptr) override;
	bool IsPipe(const string &filename, optional_ptr<FileOpener> opener = nullptr) override;
	void RemoveFile(const string &filename, optional_ptr<FileOpener> opener = nullptr) override;
	vector<string> Glob(const string &path, FileOpener *opener = nullptr) override;
	bool FileExists(const string &filename, optional_ptr<FileOpener> opener = nullptr) override;
	bool DirectoryExists(const string &directory, optional_ptr<FileOpener> opener = nullptr) override;
	void Truncate(FileHandle &handle, int64_t new_size) override;
	void FileSync(FileHandle &handle) override;

	std::string GetName() const override {
		return "XNVMEFileSystem";
	}

	// Device capability checking
	bool CanSeek() override;
	bool OnDiskFile(FileHandle &handle) override;
	bool CanHandleFile(const string &path) override;

private:
	vector<string> FetchFileWithoutGlob(const string &path, FileOpener *opener, bool absolute_path);
	void SubmitRead(string &filepath, xnvme_cmd_ctx *ctx, void *buffer, int64_t nr_bytes, idx_t location);
	void SubmitWrite(string &filepath, xnvme_cmd_ctx *ctx, void *buffer, int64_t nr_bytes, idx_t location);
};

} // namespace duckdb
