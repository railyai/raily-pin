#pragma once

// A RAM file system for tests/test_record_store.cpp with the semantics of
// Adafruit_LittleFS over littlefs 1.6 (Seeed core 1.1.13) that the steps in
// record_store.h rely on, and the failures that matter. Test code: keep it
// small enough to trust by reading.
//
// Semantics kept from the real library:
// - open(path, write) opens read-write + create and positions at the END
//   (Adafruit's File::_open_file seeks to LFS_SEEK_END); a new file is created
//   empty at once (lfs_file_open commits the directory entry); a file in a
//   missing directory does not open.
// - open(path, read) of a missing file fails.
// - write() returns the count stored; read() returns the count read, -1 on an
//   error; truncate() and seek() return success.
// - Writes live in the open file until close(); close() returns nothing and
//   commits the whole file at once (littlefs is copy-on-write), or loses it.
// - rename() replaces the target atomically; remove() of a missing file fails.
// Not modelled: Adafruit's leak of the lfs_file_t when an open fails (the
// steps never close a file that did not open), directories as files.
//
// Failures:
// - shortWriteBy: every write stores that many fewer bytes (and says so).
// - loseCloseOn: closing a written file at this path commits nothing; the
//   file keeps its previous content (empty when open created it).
// - failOpenOn / failReadOn: open fails / read returns -1 at this path.
// - failTruncate, failRename.
// - full: the volume is full: a new file or directory cannot be created and
//   no written file commits on close.
// - crashAtOp = k: the power goes before operation k (1-based, every call
//   counts); from there every call does nothing and fails. reboot() drops
//   what was not committed, as a power cycle would.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <set>
#include <string>
#include <vector>

struct FakeFs {
  typedef std::vector<uint8_t> Bytes;
  std::map<std::string, Bytes> files;  // committed content
  std::set<std::string> dirs;          // "/" always exists
  std::vector<std::string> trace;      // every call, for op-sequence checks

  size_t shortWriteBy = 0;
  std::string loseCloseOn, failOpenOn, failReadOn;
  bool failTruncate = false, failRename = false, full = false;
  int crashAtOp = 0;  // 0: no crash
  int ops = 0;
  bool crashed = false;
  int openFiles = 0;  // files opened and not closed

  // Counts the call; false once the power is gone.
  bool step(const std::string& what) {
    ops++;
    if (crashAtOp > 0 && ops >= crashAtOp) crashed = true;
    trace.push_back(crashed ? "(dead) " + what : what);
    return !crashed;
  }

  // A power cycle: open files vanish uncommitted, faults stay off.
  void reboot() {
    crashed = false;
    crashAtOp = 0;
    ops = 0;
    openFiles = 0;
    generation++;
  }

  static std::string parentOf(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
  }
  bool dirExists(const std::string& dir) const { return dir == "/" || dirs.count(dir) > 0; }

  bool exists(const char* path) {
    if (!step(std::string("exists ") + path)) return false;
    return files.count(path) > 0 || dirExists(path);
  }
  bool mkdir(const char* path) {
    if (!step(std::string("mkdir ") + path)) return false;
    if (full || dirExists(path) || files.count(path)) return false;
    dirs.insert(path);  // the paths used are one level deep
    return true;
  }
  bool remove(const char* path) {
    if (!step(std::string("remove ") + path)) return false;
    return files.erase(path) > 0;
  }
  bool rename(const char* from, const char* to) {
    if (!step(std::string("rename ") + from + " " + to)) return false;
    if (failRename || !files.count(from)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }

  // Raw access for the tests (no op counted).
  void put(const std::string& path, const Bytes& bytes) {
    if (parentOf(path) != "/") dirs.insert(parentOf(path));
    files[path] = bytes;
  }
  void put(const std::string& path, const void* bytes, size_t length) {
    put(path, Bytes((const uint8_t*)bytes, (const uint8_t*)bytes + length));
  }
  bool has(const std::string& path) const { return files.count(path) > 0; }
  Bytes get(const std::string& path) const {
    std::map<std::string, Bytes>::const_iterator it = files.find(path);
    return it == files.end() ? Bytes() : it->second;
  }

  int generation = 0;  // a reboot invalidates files opened before it

  class File {
   public:
    explicit File(FakeFs& fs) : fs_(fs) {}
    ~File() {}  // like Adafruit's File: nothing is closed for you

    bool open(const char* path, uint8_t mode) {
      if (open_) close();
      if (!fs_.step(std::string(mode ? "open-w " : "open-r ") + path)) return false;
      if (fs_.failOpenOn == path) return false;
      const bool present = fs_.files.count(path) > 0;
      if (mode == 0) {
        if (!present) return false;
      } else if (!present) {
        if (fs_.full || !fs_.dirExists(parentOf(path))) return false;
        fs_.files[path] = Bytes();  // created, empty, at open
      }
      path_ = path;
      writable_ = mode != 0;
      data_ = fs_.files[path];
      pos_ = writable_ ? data_.size() : 0;  // FILE_O_WRITE starts at the end
      dirty_ = false;
      open_ = true;
      generation_ = fs_.generation;
      fs_.openFiles++;
      return true;
    }
    bool seek(uint32_t pos) {
      if (!fs_.step("seek " + std::to_string(pos)) || !live()) return false;
      pos_ = pos;
      return true;
    }
    size_t write(const uint8_t* bytes, size_t length) {
      if (!fs_.step("write " + std::to_string(length)) || !live() || !writable_) return 0;
      const size_t stored = length > fs_.shortWriteBy ? length - fs_.shortWriteBy : 0;
      if (data_.size() < pos_ + stored) data_.resize(pos_ + stored, 0);
      if (stored) memcpy(&data_[pos_], bytes, stored);
      pos_ += stored;
      dirty_ = true;
      return stored;
    }
    int read(void* buffer, uint16_t length) {
      if (!fs_.step("read " + std::to_string(length)) || !live() || fs_.failReadOn == path_) return -1;
      const size_t left = pos_ < data_.size() ? data_.size() - pos_ : 0;
      const size_t n = length < left ? length : left;
      if (n) memcpy(buffer, &data_[pos_], n);
      pos_ += n;
      return (int)n;
    }
    bool truncate(uint32_t size) {
      if (!fs_.step("truncate " + std::to_string(size)) || !live() || !writable_ || fs_.failTruncate) return false;
      data_.resize(size, 0);
      dirty_ = true;
      return true;
    }
    void close() {
      if (!open_) return;
      open_ = false;
      const bool alive = fs_.step("close") && generation_ == fs_.generation;
      if (generation_ == fs_.generation) fs_.openFiles--;
      if (!alive || !writable_ || !dirty_) return;
      if (fs_.full || fs_.loseCloseOn == path_) return;  // lfs_file_close failed: nothing committed
      fs_.files[path_] = data_;
    }

   private:
    bool live() const { return open_ && generation_ == fs_.generation; }
    FakeFs& fs_;
    std::string path_;
    Bytes data_;
    size_t pos_ = 0;
    bool open_ = false, writable_ = false, dirty_ = false;
    int generation_ = 0;
  };
};
