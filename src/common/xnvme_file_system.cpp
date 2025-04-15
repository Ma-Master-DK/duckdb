#include "duckdb/common/xnvme_file_system.hpp"
#include <iostream>
#include <libxnvme.h>
#include <unistd.h>
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/function/scalar/string_common.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include <pwd.h>

#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace duckdb {

struct XNVMEFileHandle : public FileHandle {
public:
	XNVMEFileHandle(FileSystem &file_system, string path, struct xnvme_dev *dev, FileOpenFlags flags)
	    : FileHandle(file_system, std::move(path), flags), dev(dev) {
		if (flags.AsyncIO()) {
			int ret = xnvme_queue_init(dev, qdepth, 0, &queue);
			if (ret) {
				throw IOException("Could not initialize NVMe command queue!", {{"errno", std::to_string(errno)}}, path,
				                  strerror(errno));
			}
			xnvme_queue_set_cb(queue, cb_fn, nullptr);
		}
	};
	~XNVMEFileHandle() override {
		XNVMEFileHandle::Close();
	};

	struct xnvme_dev *dev;
	struct xnvme_queue *queue;
	const int qdepth = 16;

public:
	void Close() override {
		if (queue) {
			xnvme_queue_term(queue);
			queue = nullptr;
		}
		if (dev) {
			xnvme_dev_close(dev);
			dev = nullptr;
		}
	}

private:
	static void cb_fn(struct xnvme_cmd_ctx *ctx, void *arg_unused) {
		if (xnvme_cmd_ctx_cpl_status(ctx)) {
			xnvme_cli_pinf("Command did not complete successfully");
			xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
		} else {
			xnvme_cli_pinf("Command completed succesfully");
		}

		// Completed: Put the command-context back in the queue
		xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);
	}
};

bool XNVMEFileSystem::FileExists(const string &filename, optional_ptr<FileOpener> opener) {
	if (!filename.empty()) {
		if (access(filename.c_str(), 0) == 0) {
			struct stat status;
			stat(filename.c_str(), &status);
			if (S_ISREG(status.st_mode)) {
				return true;
			}
		}
	}
	return false;
}

bool XNVMEFileSystem::IsPipe(const string &filename, optional_ptr<FileOpener> opener) {
	if (!filename.empty()) {
		if (access(filename.c_str(), 0) == 0) {
			struct stat status;
			stat(filename.c_str(), &status);
			if (S_ISFIFO(status.st_mode)) {
				return true;
			}
		}
	}
	// if any condition fails
	return false;
}

unique_ptr<FileHandle> XNVMEFileSystem::OpenFile(const string &path_p, FileOpenFlags flags,
                                                 optional_ptr<FileOpener> opener) {
	auto path = FileSystem::ExpandPath(path_p, opener);
	if (flags.Compression() != FileCompressionType::UNCOMPRESSED) {
		throw NotImplementedException("Unsupported compression type for default file system");
	}

	if (opener) {
		DUCKDB_LOG_INFO(*opener, "duckdb.FileSystem.XNVMeFileSystem.OpenFile", path_p);
	}

	flags.Verify();

	bool open_read = flags.OpenForReading();
	bool open_write = flags.OpenForWriting();
	xnvme_opts opts = xnvme_opts_default();
	opts.rdwr = 0;
	if (open_read && open_write) {
		opts.rdwr = 1;
	} else if (open_read) {
		opts.rdonly = 1;
	} else if (open_write) {
		opts.wronly = 1;
	} else {
		throw InternalException("READ, WRITE or both should be specified when opening a file");
	}
	if (open_write) {
		// need Read or Write
		D_ASSERT(flags.OpenForWriting());
		if (flags.CreateFileIfNotExists()) {
			opts.create = 1;
		} else if (flags.OverwriteExistingFile()) {
			opts.create = 1;
			opts.truncate = 1;
		}
	}
	if (flags.DirectIO()) {
		opts.direct = 1;
	}

	// Open the file
	struct xnvme_dev *dev = xnvme_file_open(path.c_str(), &opts);

	if (!dev) {
		if (flags.ReturnNullIfNotExists() && errno == ENOENT) {
			return nullptr;
		}
		if (flags.ReturnNullIfExists() && errno == EEXIST) {
			return nullptr;
		}
		throw IOException("Cannot open file \"%s\": %s", {{"errno", std::to_string(errno)}}, path, strerror(errno));
	}

	return make_uniq<XNVMEFileHandle>(*this, path, dev, flags);
}

void XNVMEFileSystem::Read(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	auto queue = handle.Cast<XNVMEFileHandle>().queue;
	auto read_buffer = char_ptr_cast(buffer);
	xnvme_cmd_ctx ctx;
	if (queue) {
		ctx = *xnvme_cmd_ctx_from_queue(queue);
	} else {
		ctx = xnvme_cmd_ctx_from_dev(dev);
	}

	while (nr_bytes > 0) {
		SubmitRead(handle.path, queue, &ctx, read_buffer, nr_bytes, location);
		if (queue) {
			xnvme_queue_drain(queue);
		}
		int64_t bytes_read = UnsafeNumericCast<int64_t>(ctx.cpl.result);
		if (bytes_read == 0) {
			throw IOException(
			    "Could not read enough bytes from file \"%s\": attempted to read %llu bytes from location %llu",
			    handle.path, nr_bytes, location);
		}
		read_buffer += bytes_read;
		nr_bytes -= bytes_read;
		location += UnsafeNumericCast<idx_t>(bytes_read);
	}
}

int64_t XNVMEFileSystem::Read(FileHandle &handle, void *buffer, int64_t nr_bytes) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	auto queue = handle.Cast<XNVMEFileHandle>().queue;
	xnvme_cmd_ctx ctx;
	if (queue) {
		ctx = *xnvme_cmd_ctx_from_queue(queue);
	} else {
		ctx = xnvme_cmd_ctx_from_dev(dev);
	}
	SubmitRead(handle.path, queue, &ctx, buffer, nr_bytes, 0);
	int64_t bytes_read = UnsafeNumericCast<int64_t>(ctx.cpl.result);
	return bytes_read;
}

void XNVMEFileSystem::SubmitRead(string &filepath, struct xnvme_queue *queue, xnvme_cmd_ctx *ctx, void *buffer,
                                 int64_t nr_bytes, idx_t location) {
	int err = xnvme_file_pread(ctx, buffer, UnsafeNumericCast<size_t>(nr_bytes), UnsafeNumericCast<off_t>(location));
	while (err != 0) {
		switch (err) {
		case 0:
			xnvme_cli_pinf("Command completed successfully");
		case -EBUSY:
		case -EAGAIN:
			xnvme_queue_poke(queue, 0);
			err = xnvme_file_pread(ctx, buffer, UnsafeNumericCast<size_t>(nr_bytes), 0);
		default:
			xnvme_queue_put_cmd_ctx(queue, ctx);
			throw IOException("Could not read from file \"%s\": %s", {{"errno", std::to_string(errno)}}, filepath,
			                  strerror(errno));
		}
	}
}

void XNVMEFileSystem::SubmitWrite(string &filepath, struct xnvme_queue *queue, xnvme_cmd_ctx *ctx, void *buffer,
                                  int64_t nr_bytes, idx_t location) {
	int err = xnvme_file_pwrite(ctx, buffer, UnsafeNumericCast<size_t>(nr_bytes), UnsafeNumericCast<off_t>(location));
	while (err != 0) {
		switch (err) {
		case 0:
			xnvme_cli_pinf("Command completed successfully");
		case -EBUSY:
		case -EAGAIN:
			xnvme_queue_poke(queue, 0);
			err = xnvme_file_pwrite(ctx, buffer, UnsafeNumericCast<size_t>(nr_bytes), 0);
		default:
			xnvme_queue_put_cmd_ctx(queue, ctx);
			throw IOException("Could not write to file \"%s\": %s", {{"errno", std::to_string(errno)}}, filepath,
			                  strerror(errno));
		}
	}
}

void XNVMEFileSystem::Write(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	auto queue = handle.Cast<XNVMEFileHandle>().queue;
	xnvme_cmd_ctx ctx;
	if (queue) {
		ctx = *xnvme_cmd_ctx_from_queue(queue);
	} else {
		ctx = xnvme_cmd_ctx_from_dev(dev);
	}
	auto write_buffer = char_ptr_cast(buffer);
	while (nr_bytes > 0) {
		auto bytes_to_write = MinValue<idx_t>(idx_t(NumericLimits<int32_t>::Maximum()), idx_t(nr_bytes));
		SubmitWrite(handle.path, queue, &ctx, write_buffer, UnsafeNumericCast<int64_t>(bytes_to_write), location);
		int64_t bytes_written = UnsafeNumericCast<int64_t>(ctx.cpl.result);
		write_buffer += bytes_written;
		nr_bytes -= bytes_written;
		location += UnsafeNumericCast<idx_t>(bytes_written);
	}
}

int64_t XNVMEFileSystem::Write(FileHandle &handle, void *buffer, int64_t nr_bytes) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	int64_t bytes_written = 0;
	xnvme_cmd_ctx ctx = xnvme_cmd_ctx_from_dev(dev);
	while (nr_bytes > 0) {
		auto bytes_to_write = MinValue<idx_t>(idx_t(NumericLimits<int32_t>::Maximum()), idx_t(nr_bytes));
		SubmitWrite(handle.path, nullptr, &ctx, buffer, UnsafeNumericCast<int64_t>(bytes_to_write), 0);
		int64_t current_bytes_written = UnsafeNumericCast<int64_t>(ctx.cpl.result);
		bytes_written += current_bytes_written;
		buffer = (void *)(data_ptr_cast(buffer) + current_bytes_written);
		nr_bytes -= current_bytes_written;
	}
	return bytes_written;
}

bool XNVMEFileSystem::Trim(FileHandle &handle, idx_t offset_bytes, idx_t length_bytes) {
	string path = handle.GetPath();
	int fd = open(path.c_str(), O_RDWR);
	int res = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, UnsafeNumericCast<int64_t>(offset_bytes),
	                    UnsafeNumericCast<int64_t>(length_bytes));
	close(fd);
	return res == 0;
};

int64_t XNVMEFileSystem::GetFileSize(FileHandle &handle) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	const struct xnvme_geo *geo = xnvme_dev_get_geo(dev);
	return UnsafeNumericCast<int64_t>(geo->tbytes);
}

time_t XNVMEFileSystem::GetLastModifiedTime(FileHandle &handle) {
	struct stat s;
	string path = handle.GetPath();
	if (stat(path.c_str(), &s) == -1) {
		throw IOException("Failed to get last modified time for file \"%s\": %s", {{"errno", std::to_string(errno)}},
		                  handle.path, strerror(errno));
	}
	return s.st_mtime;
}

void XNVMEFileSystem::Truncate(FileHandle &handle, int64_t new_size) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	string path = handle.GetPath();
	if (truncate(path.c_str(), new_size) != 0) {
		throw IOException("Could not truncate file \"%s\": %s", {{"errno", std::to_string(errno)}}, handle.path,
		                  strerror(errno));
	}
}

bool XNVMEFileSystem::CanSeek() {
	return false;
}

bool XNVMEFileSystem::OnDiskFile(FileHandle &handle) {
	return true;
}

void XNVMEFileSystem::Seek(FileHandle &handle, idx_t location) {
	if (!CanSeek()) {
		throw IOException("Cannot seek in files of this type");
	}
}

idx_t XNVMEFileSystem::SeekPosition(FileHandle &handle) {
	if (!CanSeek()) {
		throw IOException("Cannot seek in files of this type");
	}
	return 0;
}

bool XNVMEFileSystem::DirectoryExists(const string &directory, optional_ptr<FileOpener> opener) {
	if (!directory.empty()) {
		if (access(directory.c_str(), 0) == 0) {
			struct stat status;
			stat(directory.c_str(), &status);
			if (status.st_mode & S_IFDIR) {
				return true;
			}
		}
	}
	// if any condition fails
	return false;
}

void XNVMEFileSystem::FileSync(FileHandle &handle) {
	auto dev = handle.Cast<XNVMEFileHandle>().dev;
	auto queue = handle.Cast<XNVMEFileHandle>().queue;
	if (xnvme_file_sync(dev) != 0) {
		throw IOException("Could not sync file \"%s\": %s", {{"errno", std::to_string(errno)}}, handle.path,
		                  strerror(errno));
	}
	if (queue) {
		int ret = xnvme_queue_drain(queue);
		if (ret < 0) {
			throw IOException("Could not drain queue for file \"%s\": %s", {{"errno", std::to_string(errno)}},
			                  handle.path, strerror(errno));
		}
	}
}

unique_ptr<FileSystem> FileSystem::CreateXNVME() {
	return make_uniq<XNVMEFileSystem>();
}

FileType XNVMEFileSystem::GetFileType(FileHandle &handle) { // LCOV_EXCL_START
	string path = handle.GetPath();
	struct stat s;
	if (stat(path.c_str(), &s) == -1) {
		return FileType::FILE_TYPE_INVALID;
	}
	switch (s.st_mode & S_IFMT) {
	case S_IFBLK:
		return FileType::FILE_TYPE_BLOCKDEV;
	case S_IFCHR:
		return FileType::FILE_TYPE_CHARDEV;
	case S_IFIFO:
		return FileType::FILE_TYPE_FIFO;
	case S_IFDIR:
		return FileType::FILE_TYPE_DIR;
	case S_IFLNK:
		return FileType::FILE_TYPE_LINK;
	case S_IFREG:
		return FileType::FILE_TYPE_REGULAR;
	case S_IFSOCK:
		return FileType::FILE_TYPE_SOCKET;
	default:
		return FileType::FILE_TYPE_INVALID;
	}
}

void XNVMEFileSystem::CreateDirectory(const string &directory, optional_ptr<FileOpener> opener) {
	struct stat st;

	if (stat(directory.c_str(), &st) != 0) {
		/* Directory does not exist. EEXIST for race condition */
		if (mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
			throw IOException("Failed to create directory \"%s\": %s", {{"errno", std::to_string(errno)}}, directory,
			                  strerror(errno));
		}
	} else if (!S_ISDIR(st.st_mode)) {
		throw IOException("Failed to create directory \"%s\": path exists but is not a directory!",
		                  {{"errno", std::to_string(errno)}}, directory);
	}
}

int RemoveDirectoryRecursiveX(const char *path) {
	DIR *d = opendir(path);
	idx_t path_len = (idx_t)strlen(path);
	int r = -1;

	if (d) {
		struct dirent *p;
		r = 0;
		while (!r && (p = readdir(d))) {
			int r2 = -1;
			char *buf;
			idx_t len;
			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
				continue;
			}
			len = path_len + (idx_t)strlen(p->d_name) + 2;
			buf = new (std::nothrow) char[len];
			if (buf) {
				struct stat statbuf;
				snprintf(buf, len, "%s/%s", path, p->d_name);
				if (!stat(buf, &statbuf)) {
					if (S_ISDIR(statbuf.st_mode)) {
						r2 = RemoveDirectoryRecursiveX(buf);
					} else {
						r2 = unlink(buf);
					}
				}
				delete[] buf;
			}
			r = r2;
		}
		closedir(d);
	}
	if (!r) {
		r = rmdir(path);
	}
	return r;
}

void XNVMEFileSystem::RemoveDirectory(const string &directory, optional_ptr<FileOpener> opener) {
	RemoveDirectoryRecursiveX(directory.c_str());
}

void XNVMEFileSystem::RemoveFile(const string &filename, optional_ptr<FileOpener> opener) {
	if (std::remove(filename.c_str()) != 0) {
		throw IOException("Could not remove file \"%s\": %s", {{"errno", std::to_string(errno)}}, filename,
		                  strerror(errno));
	}
}

bool XNVMEFileSystem::ListFiles(const string &directory, const std::function<void(const string &, bool)> &callback,
                                FileOpener *opener) {
	auto dir = opendir(directory.c_str());
	if (!dir) {
		return false;
	}

	// RAII wrapper around DIR to automatically free on exceptions in callback
	std::unique_ptr<DIR, std::function<void(DIR *)>> dir_unique_ptr(dir, [](DIR *d) { closedir(d); });

	struct dirent *ent;
	// loop over all files in the directory
	while ((ent = readdir(dir)) != nullptr) {
		string name = string(ent->d_name);
		// skip . .. and empty files
		if (name.empty() || name == "." || name == "..") {
			continue;
		}
		// now stat the file to figure out if it is a regular file or directory
		string full_path = JoinPath(directory.c_str(), name);
		struct stat status;
		auto res = stat(full_path.c_str(), &status);
		if (res != 0) {
			continue;
		}
		if (!(status.st_mode & S_IFREG) && !(status.st_mode & S_IFDIR)) {
			// not a file or directory: skip
			continue;
		}
		// invoke callback
		callback(name, status.st_mode & S_IFDIR);
	}

	return true;
}

void XNVMEFileSystem::MoveFile(const string &source, const string &target, optional_ptr<FileOpener> opener) {
	//! FIXME: rename does not guarantee atomicity or overwriting target file if it exists
	if (rename(source.c_str(), target.c_str()) != 0) {
		throw IOException("Could not rename file!", {{"errno", std::to_string(errno)}});
	}
}

bool XNVMEFileSystem::CanHandleFile(const string &path) {
	size_t res = path.find("/mnt/");
	if (res == 0) {
		return true;
	}
	return false;
}

static idx_t GetFileUrlOffsetX(const string &path) {
	if (!StringUtil::StartsWith(path, "file:/")) {
		return 0;
	}

	// Url without host: file:/some/path
	if (path[6] != '/') {
#ifdef _WIN32
		return 6;
#else
		return 5;
#endif
	}

	// Url with empty host: file:///some/path
	if (path[7] == '/') {
#ifdef _WIN32
		return 8;
#else
		return 7;
#endif
	}

	// Url with localhost: file://localhost/some/path
	if (path.compare(7, 10, "localhost/") == 0) {
#ifdef _WIN32
		return 17;
#else
		return 16;
#endif
	}

	// unkown file:/ url format
	return 0;
}

vector<string> XNVMEFileSystem::FetchFileWithoutGlob(const string &path, FileOpener *opener, bool absolute_path) {
	vector<string> result;
	if (FileExists(path, opener) || IsPipe(path, opener)) {
		result.push_back(path);
	} else if (!absolute_path) {
		Value value;
		if (opener && opener->TryGetCurrentSetting("file_search_path", value)) {
			auto search_paths_str = value.ToString();
			vector<std::string> search_paths = StringUtil::Split(search_paths_str, ',');
			for (const auto &search_path : search_paths) {
				auto joined_path = JoinPath(search_path, path);
				if (FileExists(joined_path, opener) || IsPipe(joined_path, opener)) {
					result.push_back(joined_path);
				}
			}
		}
	}
	return result;
}

static bool IsSymbolicLinkX(const string &path) {
#ifndef _WIN32
	struct stat status;
	return (lstat(path.c_str(), &status) != -1 && S_ISLNK(status.st_mode));
#else
	auto attributes = WindowsGetFileAttributes(path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return false;
	return attributes & FILE_ATTRIBUTE_REPARSE_POINT;
#endif
}

static void RecursiveGlobDirectoriesX(FileSystem &fs, const string &path, vector<string> &result, bool match_directory,
                                      bool join_path) {

	fs.ListFiles(path, [&](const string &fname, bool is_directory) {
		string concat;
		if (join_path) {
			concat = fs.JoinPath(path, fname);
		} else {
			concat = fname;
		}
		if (IsSymbolicLinkX(concat)) {
			return;
		}
		if (is_directory == match_directory) {
			result.push_back(concat);
		}
		if (is_directory) {
			RecursiveGlobDirectoriesX(fs, concat, result, match_directory, true);
		}
	});
}

static void GlobFilesInternalX(FileSystem &fs, const string &path, const string &glob, bool match_directory,
                               vector<string> &result, bool join_path) {
	fs.ListFiles(path, [&](const string &fname, bool is_directory) {
		if (is_directory != match_directory) {
			return;
		}
		if (Glob(fname.c_str(), fname.size(), glob.c_str(), glob.size())) {
			if (join_path) {
				result.push_back(fs.JoinPath(path, fname));
			} else {
				result.push_back(fname);
			}
		}
	});
}

vector<string> XNVMEFileSystem::Glob(const string &path, FileOpener *opener) {
	if (path.empty()) {
		return vector<string>();
	}
	// split up the path into separate chunks
	vector<string> splits;

	bool is_file_url = StringUtil::StartsWith(path, "file:/");
	idx_t file_url_path_offset = GetFileUrlOffsetX(path);

	idx_t last_pos = 0;
	for (idx_t i = file_url_path_offset; i < path.size(); i++) {
		if (path[i] == '\\' || path[i] == '/') {
			if (i == last_pos) {
				// empty: skip this position
				last_pos = i + 1;
				continue;
			}
			if (splits.empty()) {
				//				splits.push_back(path.substr(file_url_path_offset, i-file_url_path_offset));
				splits.push_back(path.substr(0, i));
			} else {
				splits.push_back(path.substr(last_pos, i - last_pos));
			}
			last_pos = i + 1;
		}
	}
	splits.push_back(path.substr(last_pos, path.size() - last_pos));
	// handle absolute paths
	bool absolute_path = false;
	if (IsPathAbsolute(path)) {
		// first character is a slash -  unix absolute path
		absolute_path = true;
	} else if (StringUtil::Contains(splits[0], ":")) { // TODO: this is weird? shouldn't IsPathAbsolute handle this?
		// first split has a colon -  windows absolute path
		absolute_path = true;
	} else if (splits[0] == "~") {
		// starts with home directory
		auto home_directory = GetHomeDirectory(opener);
		if (!home_directory.empty()) {
			absolute_path = true;
			splits[0] = home_directory;
			D_ASSERT(path[0] == '~');
			if (!HasGlob(path)) {
				return Glob(home_directory + path.substr(1));
			}
		}
	}
	// Check if the path has a glob at all
	if (!HasGlob(path)) {
		// no glob: return only the file (if it exists or is a pipe)
		return FetchFileWithoutGlob(path, opener, absolute_path);
	}
	vector<string> previous_directories;
	if (absolute_path) {
		// for absolute paths, we don't start by scanning the current directory
		previous_directories.push_back(splits[0]);
	} else {
		// If file_search_path is set, use those paths as the first glob elements
		Value value;
		if (opener && opener->TryGetCurrentSetting("file_search_path", value)) {
			auto search_paths_str = value.ToString();
			vector<std::string> search_paths = StringUtil::Split(search_paths_str, ',');
			for (const auto &search_path : search_paths) {
				previous_directories.push_back(search_path);
			}
		}
	}

	if (std::count(splits.begin(), splits.end(), "**") > 1) {
		throw IOException("Cannot use multiple \'**\' in one path");
	}

	idx_t start_index;
	if (is_file_url) {
		start_index = 1;
	} else if (absolute_path) {
		start_index = 1;
	} else {
		start_index = 0;
	}

	for (idx_t i = start_index ? 1 : 0; i < splits.size(); i++) {
		bool is_last_chunk = i + 1 == splits.size();
		bool has_glob = HasGlob(splits[i]);
		// if it's the last chunk we need to find files, otherwise we find directories
		// not the last chunk: gather a list of all directories that match the glob pattern
		vector<string> result;
		if (!has_glob) {
			// no glob, just append as-is
			if (previous_directories.empty()) {
				result.push_back(splits[i]);
			} else {
				if (is_last_chunk) {
					for (auto &prev_directory : previous_directories) {
						const string filename = JoinPath(prev_directory, splits[i]);
						if (FileExists(filename, opener) || DirectoryExists(filename, opener)) {
							result.push_back(filename);
						}
					}
				} else {
					for (auto &prev_directory : previous_directories) {
						result.push_back(JoinPath(prev_directory, splits[i]));
					}
				}
			}
		} else {
			if (splits[i] == "**") {
				if (!is_last_chunk) {
					result = previous_directories;
				}
				if (previous_directories.empty()) {
					RecursiveGlobDirectoriesX(*this, ".", result, !is_last_chunk, false);
				} else {
					for (auto &prev_dir : previous_directories) {
						RecursiveGlobDirectoriesX(*this, prev_dir, result, !is_last_chunk, true);
					}
				}
			} else {
				if (previous_directories.empty()) {
					// no previous directories: list in the current path
					GlobFilesInternalX(*this, ".", splits[i], !is_last_chunk, result, false);
				} else {
					// previous directories
					// we iterate over each of the previous directories, and apply the glob of the current directory
					for (auto &prev_directory : previous_directories) {
						GlobFilesInternalX(*this, prev_directory, splits[i], !is_last_chunk, result, true);
					}
				}
			}
		}
		if (result.empty()) {
			// no result found that matches the glob
			// last ditch effort: search the path as a string literal
			return FetchFileWithoutGlob(path, opener, absolute_path);
		}
		if (is_last_chunk) {
			return result;
		}
		previous_directories = std::move(result);
	}
	return vector<string>();
}

}; // namespace duckdb
