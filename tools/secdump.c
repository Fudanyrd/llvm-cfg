// dump a section in the ELF file.
// usage: secdump <binary> <section_name>

#include <stdio.h>
#include <stdlib.h>
#include <elf.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static inline void xseek(int fd, off_t offset, int whence) {
    if (lseek(fd, offset, whence) == (off_t)-1) {
        perror("lseek");
        exit(1);
    }
}

static inline ssize_t xread(int fd, void *buf, size_t count) {
    ssize_t ret = read(fd, buf, count);
    if (ret == -1) {
        perror("read");
        exit(1);
    }
    return ret;
}

static inline void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(1);
    }

    memset(ptr, 0, size);
    return ptr;
}

int main(int argc, char **argv, char **envp) {
    int fd = open(argv[1], O_RDONLY);
    static Elf64_Ehdr ehdr;
    xread(fd, &ehdr, sizeof(ehdr));

    Elf64_Shdr *shdrs = (Elf64_Shdr *)xmalloc(ehdr.e_shentsize * ehdr.e_shnum);
    xseek(fd, ehdr.e_shoff, SEEK_SET);
    xread(fd, shdrs, ehdr.e_shentsize * ehdr.e_shnum);

    char *strtab = NULL;
    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_STRTAB) {
            free(strtab);
            strtab = (char *)xmalloc(shdrs[i].sh_size);
            xseek(fd, shdrs[i].sh_offset, SEEK_SET);
            xread(fd, strtab, shdrs[i].sh_size);
        }
    }

    for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
        if (strcmp(&strtab[shdrs[i].sh_name], argv[2]) == 0) {
            uint8_t *sec = (uint8_t *)xmalloc(shdrs[i].sh_size);
            xseek(fd, shdrs[i].sh_offset, SEEK_SET);
            xread(fd, sec, shdrs[i].sh_size);

            for (size_t j = 0; j < shdrs[i].sh_size; ) {
                printf("%02x ", sec[j]);

                j += 1;
                if (j % 16 == 0) {
                    printf("\n");
                }
            }

            free(sec);
            break;
        }
    }

    putchar('\n');

    free(shdrs);
    free(strtab);
    close(fd);
    return 0;
}