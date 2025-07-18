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
      *ptr = ((*ptr) + offset); // Update pointers
    }
  }
}

void StringBuf::append(const char *str, size_t len, cbuf_t ptr)
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
    *ptr = (start); // Store the pointer to the new string
    this->ptrs.push_back(ptr);              // Store the pointer in the vector
  }
}

void StringBuf::append_line(FILE *fp, cbuf_t ptr)
{
  char ch;
  if (ptr) {
    *ptr = (buf + size); // Store the pointer to the start of the line
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

void StringBuf::append_line(int fd, cbuf_t ptr)
{
  char ch;
  if (ptr) {
    *ptr = (buf + size); // Store the pointer to the start of the line
    this->ptrs.push_back(ptr); // Store the pointer in the vector
  }

  auto nb = read(fd, &ch, 1);
  while (nb > 0 && ch != '\n')
  {
    if (size == capacity) {
      overflow(capacity * 2); // Ensure there is enough space for the new character
    }
    buf[size++] = ch; // Append the character to the buffer
    nb = read(fd, &ch, 1);
  }

  // append 0
  if (size == capacity) {
    overflow(capacity * 2); // Ensure there is enough space for the null terminator
  }
  buf[size++] = '\0'; // Null-terminate the string
}

void StringBuf::append_concated(cbuf_t dst, int count, ...) {
  va_list vl;
  va_start(vl, count);

  if (dst != nullptr) {
    *dst = buf + this->size;
    ptrs.push_back(dst);
  }

  for (int i = 0; i < count; i++) {
    const char *src = va_arg(vl, const char *);
    size_t len = strlen(src);
    
    size_t cap = this->capacity;
    while (cap < len + this->size) {
      cap *= 2;
    }
    if (cap > capacity) {
      overflow(cap);
    }

    strcpy(buf + size, src);
    this->size += len;
  }

  va_end(vl);
  if (this->capacity == this->size) {
    overflow(this->capacity * 2);
    buf[this->size++] = 0;
  }
}

void CharStream::join(int count, ...) {
  va_list vl;
  va_start(vl, count);

  for (int i = 0; i < count; i++) {
    const char *src = va_arg(vl, const char *);
    if (!src) {
      continue;
    }
    size_t l = strlen(src);

    size_t newcap = capacity;
    while (l + size + 1 > newcap) {
      newcap *= 2;
    }
    if (newcap > capacity) {
      this->overflow(newcap);
    }

    strcpy(this->buf + this->size, src);
    this->size += l;
  }
  va_end(vl);
  this->buf[this->size++] = 0;
}

void CharStream::append(const char *src)
{
  size_t l = strlen(src);
  l += 1;

  size_t newcap = capacity;
  while (l + size > newcap) {
    newcap *= 2;
  }
  if (newcap > capacity) {
    this->overflow(newcap);
  }

  memcpy(this->buf + this->size, (const void *)src, l);
  this->size += l;
}

void CharStream::append(char ch) {
  if (size >= capacity) {
    overflow();
  }
  this->buf[this->size] = ch;
  this->size += 1;
}

void CharStream::append(const char *word, size_t len)
{
  size_t newcap = capacity;
  while (newcap < len + this->size) {
    newcap *= 2;
  }
  if (newcap > this->capacity) {
    this->overflow(newcap);
  } 

  memcpy(this->buf + this->size, (const void *)word, len);
  this->size += len;
}

void CharStream::replace_suffix(const char *path, const char *suffix) {
  size_t l = strlen(path);
  const size_t lpath = l;
  if (l == 0) {
    this->append(suffix);
    return;
  }

  while (l > 0 && path[l] != '.') {
    l -= 1;
  }

  if (path[l] == '.') {
    if (l != 0) {
      this->append(path, l);
    }
    this->append(suffix);
  } else {
    this->append(path, lpath);
    this->append(suffix);
  }
}
