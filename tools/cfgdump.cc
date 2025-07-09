// Take an ELF file instrumented with
// -fsanitize-coverage=trace-pc-guard,pc-table(,no-prune), recover its control
// flow graph, including intra-function control-flow and inter-function
// control-flow.

extern "C" {
#include <elf.h>
#include <unistd.h>
}

#include "api/sancov_sec.h"

#include <algorithm>
#include <cassert>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

static const char *usage = "Usage: cfg <input file>\n";

static void *xmalloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    perror("malloc");
    exit(1);
  }
  memset(ptr, 0, size);
  return ptr;
}

struct ElfFile {
  ElfFile() = default;
  ~ElfFile() {
    if (fd != -1) { close(fd); }

    free(strtab);
    free(shdrs);
  }

  void open(const char *filename) {
    fd = ::open(filename, O_RDONLY);
    if (fd == -1) {
      perror("open");
      exit(1);
    }
    xreadat(&ehdr, 0, sizeof(ehdr));
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
      std::cerr << "Not a valid ELF file: " << filename << std::endl;
      exit(1);
    }

    if (ehdr.e_shstrndx == SHN_UNDEF) {
      std::cerr << "No string table found in" << filename << std::endl;
      exit(1);
    }
  }

  const char *string_table(void) {
    if (strtab) { return strtab; }

    if (!shdrs) {
      shdrs = (Elf64_Shdr *)xmalloc(ehdr.e_shentsize * ehdr.e_shnum);
      xreadat(shdrs, ehdr.e_shoff, ehdr.e_shentsize * ehdr.e_shnum);
    }

    // Find the section header string table.
    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
      if (shdrs[i].sh_type == SHT_STRTAB && i == ehdr.e_shstrndx) {
        free(strtab);
        strtab = (char *)xmalloc(shdrs[i].sh_size);
        xreadat(strtab, shdrs[i].sh_offset, shdrs[i].sh_size);
        return strtab;
      }
    }

    return const_cast<const char *>(strtab);
  }

  Elf64_Shdr *get_section_hdr(const char *name) {
    if (!shdrs) {
      shdrs = (Elf64_Shdr *)xmalloc(ehdr.e_shentsize * ehdr.e_shnum);
      xreadat(shdrs, ehdr.e_shoff, ehdr.e_shentsize * ehdr.e_shnum);
    }

    const char *strtab = string_table();
    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
      if (strcmp(&strtab[shdrs[i].sh_name], name) == 0) { return &shdrs[i]; }
    }
    return nullptr;
  }

  bool get_section_data(const char *name, uint8_t *data) {
    Elf64_Shdr *shdr = get_section_hdr(name);
    if (!shdr) { return false; }

    xreadat(data, shdr->sh_offset, shdr->sh_size);
    return true;
  }

 private:
  int         fd{-1};
  Elf64_Ehdr  ehdr;
  char       *strtab{nullptr};
  Elf64_Shdr *shdrs{nullptr};

  void xseek(off_t offset, int whence) {
    if (lseek(fd, offset, whence) == (off_t)-1) {
      perror("lseek");
      exit(1);
    }
  }

  void xreadat(void *dst, off_t offset, size_t count) {
    this->xseek(offset, SEEK_SET);
    if (read(fd, dst, count) == -1) {
      perror("read");
      exit(1);
    }
  }
};

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << usage;
    return 1;
  }

  ElfFile elf_obj;
  elf_obj.open(argv[1]);

  /** Read the address of sancov guard. */
  Elf64_Shdr *sancov_guard_sec = elf_obj.get_section_hdr("__sancov_guards");
  assert(sancov_guard_sec &&
         "Section __sancov_guards not found in the ELF file\n"
         "compile the program with -fsanitize-coverage=trace-pc-guard,pc-table "
         "to generate this section.\n");
  void *start_sancov_guard = (void *)sancov_guard_sec->sh_addr;

  /** Load intra control-flow, ie. edges between basic blocks
   *  inside a function.
   */
  Elf64_Shdr *sancov_cfg_edges_sec =
      elf_obj.get_section_hdr("__sancov_cfg_edges");
  struct SancovCfgEdge *edges =
      (struct SancovCfgEdge *)xmalloc(sancov_cfg_edges_sec->sh_size);
  if (!elf_obj.get_section_data("__sancov_cfg_edges", (uint8_t *)edges)) {
    fprintf(
        stderr,
        "Cannot read section __sancov_cfg_edges\n"
        "compile the program with -fsanitize-coverage=trace-pc-guard,pc-table "
        "to generate this section.\n");
    exit(1);
  }
  std::vector<std::pair<void *, void *>> edge_list;
  struct {
    bool operator()(const std::pair<void *, void *> &a,
                    const std::pair<void *, void *> &b) const {
      return a.first < b.first || (a.first == b.first && a.second < b.second);
    }
  } edgeCmp;
  for (uint16_t i = 0;
       i < sancov_cfg_edges_sec->sh_size / sizeof(SancovCfgEdge); i++) {
    SancovCfgEdge &edge = edges[i];
    if (!edge.src || !edge.dst) {
      // Skip edges with null src or dst.
      continue;
    }
    assert(
        edge.src >= start_sancov_guard && edge.dst >= start_sancov_guard &&
        "Invalid edge in __sancov_cfg_edges section\n"
        "compile the program with -fsanitize-coverage=trace-pc-guard,pc-table "
        "to generate this section.\n");
    std::pair<void *, void *> dat;
    dat.first = edge.src;
    dat.second = edge.dst;
    edge_list.push_back(dat);
  }
  free(edges);

  /** Load the guard to the entry block of each function.*/
  std::unordered_map<void *, void *> func_to_entry_block;
  Elf64_Shdr *sancov_entry_sec = elf_obj.get_section_hdr("__sancov_entries");
  struct SancovEntry *entries =
      (struct SancovEntry *)xmalloc(sancov_entry_sec->sh_size);
  if (!elf_obj.get_section_data("__sancov_entries", (uint8_t *)entries)) {
    fprintf(
        stderr,
        "Cannot read section __sancov_entries\n"
        "compile the program with -fsanitize-coverage=trace-pc-guard,pc-table "
        "to generate this section.\n");
    exit(1);
  }
  for (uint16_t i = 0; i < sancov_entry_sec->sh_size / sizeof(SancovEntry);
       i++) {
    SancovEntry &entry = entries[i];
    if (entry.func && entry.guard) {
      assert(entry.guard >= start_sancov_guard &&
             "Invalid entry in __sancov_entries section\n"
             "compile the program with "
             "-fsanitize-coverage=trace-pc-guard,pc-table "
             "to generate this section.\n");
      func_to_entry_block[entry.func] = entry.guard;
    }
  }
  free(entries);

  /** Stitch inter-function control flow graph. */
  Elf64_Shdr *sancov_func_call_sec = elf_obj.get_section_hdr("__sancov_func");
  struct SancovFuncCall *calls =
      (struct SancovFuncCall *)xmalloc(sancov_func_call_sec->sh_size);
  if (!elf_obj.get_section_data("__sancov_func", (uint8_t *)calls)) {
    fprintf(
        stderr,
        "Cannot read section __sancov_func_calls\n"
        "compile the program with -fsanitize-coverage=trace-pc-guard,pc-table "
        "to generate this section.\n");
    exit(1);
  }
  for (uint16_t i = 0;
       i < sancov_func_call_sec->sh_size / sizeof(SancovFuncCall); i++) {
    SancovFuncCall &call = calls[i];
    if (call.func && call.guard) {
      assert(call.guard >= start_sancov_guard &&
             "Invalid call in __sancov_func_calls section\n"
             "compile the program with "
             "-fsanitize-coverage=trace-pc-guard,pc-table "
             "to generate this section.\n");
      std::pair<void *, void *> dat;

      auto ptr = func_to_entry_block.find(call.func);
      if (ptr != func_to_entry_block.end()) {
        dat.first = call.guard;
        dat.second = ptr->second;
        edge_list.push_back(dat);
      }
    }
  }
  free(calls);

  /** Sort, dedup and print the control flow graph. */
  std::sort(edge_list.begin(), edge_list.end(), edgeCmp);
  const size_t len = edge_list.size();
  size_t       i = 0;
  while (i < len) {
    size_t      j = i + 1;
    const auto &edge = edge_list[i];
    // dedup
    while (j < len && edge.first == edge_list[j].first &&
           edge.second == edge_list[j].second) {
      j++;
    }
    printf("%ld %ld\n",
           ((uintptr_t)edge.first - (uintptr_t)start_sancov_guard) / 4,
           ((uintptr_t)edge.second - (uintptr_t)start_sancov_guard) / 4);
    i = j;
  }

  return 0;
}
