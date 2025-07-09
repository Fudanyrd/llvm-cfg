#include "buf.h"

void StringBuf::overflow(size_t new_capacity)
{
  if (new_capacity > capacity)
  {
    capacity = new_capacity; // Double the capacity
    char *new_buf = static_cast<char *>(realloc(buf, capacity * sizeof(char)));
    assert(new_buf != nullptr && "out of memory"); // Ensure realloc was successful
    ptrdiff_t offset = new_buf - buf;              // Calculate the offset
    buf = new_buf;

    for (auto &ptr : ptrs)
    {
      *ptr = reinterpret_cast<void *>(reinterpret_cast<char *>(*ptr) + offset); // Update pointers
    }
  }
}

void StringBuf::append(const char *str, size_t len, void **ptr)
{
  /** handle buffer overflow */
  size_t cap = capacity;
  while (cap < size + len + 1)
  {
    cap *= 2; // Double the capacity until it is sufficient
  }
  if (cap > capacity)
  {
    overflow(cap); // Ensure the buffer has enough capacity
  }

  char *start = buf + size; // Pointer to the end of the current buffer
  strncpy(start, str, len); // Copy the string into the buffer
  buf[size + len] = '\0'; // Null-terminate the string
  size += len + 1; // Update the size of the buffer
  if (ptr != nullptr)
  {
    *ptr = reinterpret_cast<void *>(start); // Store the pointer to the new string
    this->ptrs.push_back(ptr);              // Store the pointer in the vector
  }
}

void StringBuf::append_line(FILE *fp, void **ptr)
{
  char ch;
  if (ptr) {
    *ptr = reinterpret_cast<void *>(buf + size); // Store the pointer to the start of the line
    this->ptrs.push_back(ptr); // Store the pointer in the vector
  }

  while ((ch = fgetc(fp)) != EOF && ch != '\n')
  {
    if (size == capacity) {
      overflow(capacity * 2); // Ensure there is enough space for the new character
    }
    buf[size++] = ch; // Append the character to the buffer
  }

  // append 0
  if (size == capacity) {
    overflow(capacity * 2); // Ensure there is enough space for the null terminator
  }
  buf[size++] = '\0'; // Null-terminate the string
}
