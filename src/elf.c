#include "elf.h"
#include "console.h"
#include "lib.h"
#include "pmm.h"

#define EI_NIDENT 16
#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

static bool elf_validate(const Elf64_Ehdr* eh, size_t size) {
    if (size < sizeof(Elf64_Ehdr)) return false;
    if (eh->e_ident[0] != ELF_MAGIC0 || eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 || eh->e_ident[3] != ELF_MAGIC3) return false;
    if (eh->e_ident[4] != ELFCLASS64) return false;
    if (eh->e_ident[5] != ELFDATA2LSB) return false;
    if (eh->e_type != ET_EXEC) return false;
    if (eh->e_machine != EM_X86_64) return false;
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) return false;
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > size) return false;
    return true;
}

bool elf64_load_image(const uint8_t* image, size_t size, uint64_t* out_entry) {
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)image;
    if (!elf_validate(eh, size)) {
        console_write("[elf] invalid ELF image\n");
        return false;
    }

    const Elf64_Phdr* ph = (const Elf64_Phdr*)(image + eh->e_phoff);

    /* Reserve all PT_LOAD ranges first to avoid allocator conflicts. */
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = ph[i].p_vaddr;
        uint64_t memsz = ph[i].p_memsz;
        if (memsz == 0) continue;

        /* This kernel uses identity mapping up to 4GiB. */
        if (vaddr + memsz >= (4ULL * 1024 * 1024 * 1024)) {
            console_write("[elf] segment outside identity map\n");
            return false;
        }

        pmm_reserve_range(vaddr, memsz);
    }

    /* Copy segments. */
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = ph[i].p_vaddr;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz = ph[i].p_memsz;
        uint64_t off = ph[i].p_offset;

        if (off + filesz > size) {
            console_write("[elf] segment out of file bounds\n");
            return false;
        }

        uint8_t* dst = (uint8_t*)(uintptr_t)vaddr;
        const uint8_t* src = image + off;

        if (filesz > 0) {
            memcpy(dst, src, (size_t)filesz);
        }
        if (memsz > filesz) {
            memset(dst + filesz, 0, (size_t)(memsz - filesz));
        }
    }

    *out_entry = eh->e_entry;
    console_write("[elf] loaded entry=");
    console_write_hex64(*out_entry);
    console_write("\n");
    return true;
}
