#ifndef BUF_H
#define BUF_H

#ifndef INIT_ARG_LEN
#define INIT_ARG_LEN 64 // Default initial length for argument buffer
#endif                  // INIT_ARG_LEN

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdarg.h>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef INIT_CAPACITY
#define INIT_CAPACITY 4096 // Default initial capacity for the buffer
#endif                   // INIT_CAPACITY

#ifndef DEBUG_PREFIX
#define DEBUG_PREFIX "\033[01;36m[#]\033[0;m "
#endif 
#ifndef ERROR_PREFIX
#define ERROR_PREFIX "\033[01;31m[ERR]\033[0;m "
#endif

#ifndef CFG_MKTEMP_TEMPLATE
#define CFG_MKTEMP_TEMPLATE "/tmp/tmp.XXXXXXXXXX"
#endif

#ifndef SANCOV_DEFAULT_DEF
#define SANCOV_DEFAULT_DEF "-fsanitize-coverage=trace-pc-guard,pc-table,no-prune"
#endif // SANCOV_DEFAULT_DEF

#ifndef CFG_EDGE_PASS
#error "CFG_EDGE_PASS is not defined"
#endif
#ifndef FUNC_CALL_PASS
#error "FUNC_CALL_PASS is not defined"
#endif
#ifndef FUNC_ENTRY_PASS
#error "FUNC_ENTRY_PASS is not defined"
#endif


typedef const char *ccharptr_t;
typedef ccharptr_t *cbuf_t;

struct CharStream;
struct StringBuf
{
  StringBuf(size_t cap = INIT_CAPACITY)
      : buf(nullptr), size(0), capacity(cap)
  {
    buf = static_cast<char *>(malloc(capacity * sizeof(char)));
    if (!buf)
    {
      throw std::bad_alloc(); // Handle memory allocation failure
    }

    memset(buf, 0, capacity * sizeof(char)); // Initialize buffer to zero
  }

  ~StringBuf()
  {
    free(buf); // Free the allocated memory
  }

  void append(const std::string &str, cbuf_t ptr) {
    append(str.c_str(), str.size(), ptr); // Append string with its length
  }
  void append(const char *str, cbuf_t ptr) {
    append(str, strlen(str), ptr); // Append string with its length
  }
  void append(const char *str, size_t len, cbuf_t ptr);
  void append_line(FILE *fp, cbuf_t ptr);
  void append_line(int fd, cbuf_t ptr);
  void append_concated(cbuf_t dst, int count, ...);

  /** After recording, *ptr still points to its data after resizing the buffer. */
  void record(cbuf_t ptr, const char *dat) {
    if (ptr == nullptr) {
      return;
    }
    assert(dat >= this->buf && "dat should be within the buffer");
    assert(dat < this->buf + size && "dat should be within the buffer");

    *ptr = dat; // Record the pointer to the string
    ptrs.push_back(ptr); // Store the pointer in the vector
  }

  const char *buffer() const 
  {
    return buf; // Return the buffer pointer
  }

  const char *buffer_end() const 
  {
    return buf + size; // Return the end pointer of the buffer
  }

  const char *next(const char *current) const
  {
    if (current >= buf + size)
    {
      return buf + size; // Return nullptr if current pointer is beyond the buffer
    }
    return current + strlen(current) + 1; // Move to the next string in the buffer
  }

  size_t getCapacity(void) const {
    return capacity;
  }

private:
  void overflow(size_t new_capacity);

  char *buf;       // Pointer to the buffer
  size_t size;     // Size of the buffer
  size_t capacity; // Capacity of the buffer

  std::vector<cbuf_t> ptrs; // Pointers to the appended strings
};

struct ArgList
{
  cbuf_t buf;
  size_t size, capacity;

  ArgList(size_t size = INIT_ARG_LEN)
  {
    assert(size > 0 && "Size must be greater than zero");
    buf = (cbuf_t )(malloc(size * sizeof(char *)));
    if (!buf)
    {
      throw std::bad_alloc(); // Handle memory allocation failure
    }
    this->size = 0;
    this->capacity = size; // Initialize capacity
  }
  ~ArgList()
  {
    free(buf); // Free the allocated memory
  }

  void push(const char *arg)
  {
    if (size == capacity)
    {
      overflow(capacity * 2); // Double the capacity if needed
    }
    buf[size++] = arg; // Add the new argument to the buffer
  }

  void clear()
  {
    size = 0;
  }

private:
  void overflow(size_t new_capacity)
  {
    if (new_capacity > capacity)
    {
      capacity = new_capacity; // Double the capacity
      cbuf_t new_buf = (cbuf_t )(realloc(buf, capacity * sizeof(char *)));
      if (!new_buf)
      {
        throw std::bad_alloc(); // Handle memory allocation failure
      }
      buf = new_buf;
    }
  }
};

struct CharStream {
  CharStream(size_t cap = INIT_CAPACITY) {
    buf = (char *)malloc(cap);
    size = 0;
    capacity = cap;
  }
  ~CharStream() { free(buf); }

  const char *buffer() const { return buf; }

  /** read from `fd` till end, and close it. 
   * @return 0 if success, failure otherwise
   */
  int readfrom(int fd) {
    ssize_t nb = 1;
    size_t vol;

    while (nb > 0) {
      vol = capacity - size;
      if (vol == 0) {
        overflow();
        vol = capacity - size;
      }

      nb = read(fd, buf + size, vol);
    }

    close(fd);
    return nb == 0;
  }

  void clear() {
    size = 0;
  }

  void join(int count, ...);
  void append(const char *word);
  void append(const char *word, size_t len);
  void append(char ch);

  void extend(size_t newsize) {
    newsize += this->size;
    size_t newcap = this->capacity;
    while (newcap < newsize) {
      newcap *= 2;
    }
    if (newcap > this->capacity) {
      this->overflow(newcap);
    }
  }

  void replace_suffix(const char *path, const char *suffix);

  CharStream(const CharStream &that) = delete;
  CharStream &operator=(const CharStream &that) = delete;
  CharStream(CharStream &&that) {
    this->buf = that.buf;
    this->size = that.size;
    this->capacity = that.capacity;
    that.buf = nullptr;
  }

 private:
  char *buf{nullptr};
  size_t size, capacity{INIT_CAPACITY};

  void overflow() {
    size_t cap = capacity * 2;
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, DEBUG_PREFIX "CharStream::overflow(%ld)\n", cap);
    #endif
    buf = (char *)realloc(buf, cap);
    if (buf == nullptr) {
      throw std::bad_alloc{};
    }
    capacity = cap;
  }

  void overflow(size_t newcap) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, DEBUG_PREFIX "CharStream::overflow(%ld)\n", newcap);
    #endif
    size_t cap = newcap;
    buf = (char *)realloc(buf, cap);
    if (buf == nullptr) {
      throw std::bad_alloc{};
    }
    capacity = cap;
  }
};

struct FileDescriptor {
 public:
  int fd{-1};
  
  FileDescriptor() = default;
  FileDescriptor(int ofd): fd(ofd) {}
  ~FileDescriptor() {
    if (valid()) {
      close(fd);
    }
  }

  bool valid() const { return fd >= 0; }
  void setFd(int ofd) {
    if (fd != ofd) {
      if (valid()) {
        close(fd);
      } 
      fd = ofd;
    }
  }

  /** disallow copy, otherwise a fd may be closed twice. */
  FileDescriptor(const FileDescriptor &other) = delete;
  FileDescriptor &operator=(const FileDescriptor &other) = delete;
};

#endif // BUF_H
