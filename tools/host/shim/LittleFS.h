// Host shim for the ESP32 LittleFS API surface save.cpp actually uses:
// begin / exists / remove / open, and File's write / read / close / bool.
// Files land in a real directory (default ./host_fs, override with SH_FS_DIR),
// so desktop runs exercise the same chunk and recovery code as the device.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

class File {
  FILE* _f = nullptr;
 public:
  File() {}
  explicit File(FILE* f) : _f(f) {}
  operator bool() const { return _f != nullptr; }
  size_t write(const uint8_t* b, size_t n) { return _f ? fwrite(b, 1, n, _f) : 0; }
  size_t read(uint8_t* b, size_t n) { return _f ? fread(b, 1, n, _f) : 0; }
  bool seek(size_t p) { return _f && fseek(_f, (long)p, SEEK_SET) == 0; }
  void flush() { if (_f) fflush(_f); }
  void close() { if (_f) { fclose(_f); _f = nullptr; } }
};

class LittleFSFS {
  std::string _root;
  std::string path(const char* p) const { return _root + p; }  // p starts with '/'
 public:
  bool begin(bool /*formatOnFail*/ = false) {
    const char* dir = getenv("SH_FS_DIR");
    _root = dir ? dir : "./host_fs";
    ::mkdir(_root.c_str(), 0755);   // EEXIST is fine
    return true;
  }
  void end() {}
  bool exists(const char* p) const {
    struct stat st;
    return ::stat(path(p).c_str(), &st) == 0;
  }
  bool remove(const char* p) const { return ::remove(path(p).c_str()) == 0; }
  bool rename(const char* a, const char* b) const {
    return ::rename(path(a).c_str(), path(b).c_str()) == 0;
  }
  File open(const char* p, const char* mode) const {
    std::string m(mode); m += "b";
    return File(fopen(path(p).c_str(), m.c_str()));
  }
};
// Inline, not static: every translation unit must share the same filesystem.
inline LittleFSFS LittleFS;
