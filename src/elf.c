#include "elf.h"
#include "console.h"
#include "lib.h"
#include "pmm.h"
#include "vmm.h"

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

bool elf64_load_image(const uint8_t* image, size_t size, uint64_t target_cr3, uint64_t* out_entry, uint64_t* out_brk) {
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)image;
    if (!elf_validate(eh, size)) {
        console_write("[elf] invalid ELF image\n");
        return false;
    }

    const Elf64_Phdr* ph = (const Elf64_Phdr*)(image + eh->e_phoff);

    uint64_t max_end = 0;

    /* Map and copy segments. */
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

        uint64_t seg_start = align_down_u64(vaddr, PAGE_SIZE);
        uint64_t seg_end = align_up_u64(vaddr + memsz, PAGE_SIZE);
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (ph[i].p_flags & 0x2) flags |= VMM_FLAG_WRITABLE;
        if ((ph[i].p_flags & 0x1) == 0) flags |= VMM_FLAG_NOEXEC;

        for (uint64_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uint64_t pa = pmm_alloc_pages(1);
            if (!pa) {
                console_write("[elf] out of memory mapping segment\n");
                return false;
            }
            memset((void*)(uintptr_t)pa, 0, PAGE_SIZE);
            if (!vmm_map_page(target_cr3, va, pa, flags)) {
                pmm_free_pages(pa, 1);
                console_write("[elf] map_page failed\n");
                return false;
            }
        }

        const uint8_t* src = image + off;
        uint64_t copied = 0;
        while (copied < filesz) {
            uint64_t cur_va = vaddr + copied;
            uint64_t pa = 0;
            if (!vmm_resolve(target_cr3, cur_va, &pa, 0)) {
                console_write("[elf] resolve failed during copy\n");
                return false;
            }
            size_t page_off = (size_t)(cur_va & (PAGE_SIZE - 1));
            size_t to_copy = PAGE_SIZE - page_off;
            if (to_copy > filesz - copied) to_copy = (size_t)(filesz - copied);
            memcpy((void*)(uintptr_t)(pa + page_off), src + copied, to_copy);
            copied += to_copy;
        }

        if (vaddr + memsz > max_end) max_end = align_up_u64(vaddr + memsz, PAGE_SIZE);
    }

    *out_entry = eh->e_entry;
    if (out_brk) *out_brk = max_end;
    console_write("[elf] loaded entry=");
    console_write_hex64(*out_entry);
    console_write("\n");
    return true;
}
