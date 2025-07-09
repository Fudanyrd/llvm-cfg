#ifndef BUF_H
#define BUF_H

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#ifndef INIT_CAPACITY
#define INIT_CAPACITY 4096 // Default initial capacity for the buffer
#endif                   // INIT_CAPACITY

struct StringBuf
{
  StringBuf()
      : buf(nullptr), size(0), capacity(INIT_CAPACITY)
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

  void append(const std::string &str, void **ptr) {
    append(str.c_str(), str.size(), ptr); // Append string with its length
  }
  void append(const char *str, void **ptr) {
    append(str, strlen(str), ptr); // Append string with its length
  }
  void append(const char *str, size_t len, void **ptr);
  void append_line(FILE *fp, void **ptr);

  /** After recording, *ptr still points to its data after resizing the buffer. */
  void record(void **ptr, const char *dat) {
    if (ptr == nullptr) {
      return;
    }
    assert(dat >= this->buf && "dat should be within the buffer");
    assert(dat < this->buf + size && "dat should be within the buffer");

    *ptr = reinterpret_cast<void *>(const_cast<char *>(dat)); // Record the pointer to the string
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

private:
  void overflow(size_t new_capacity);

  char *buf;       // Pointer to the buffer
  size_t size;     // Size of the buffer
  size_t capacity; // Capacity of the buffer
  std::vector<void **> ptrs; // Pointers to the appended strings
};

#endif // BUF_H
