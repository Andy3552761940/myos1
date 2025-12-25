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
#define ET_DYN  3
#define EM_X86_64 62

#define PT_LOAD 1
#define PT_DYNAMIC 2

#define DT_NULL 0
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19

#define SHN_UNDEF 0

#define R_X86_64_64 1
#define R_X86_64_RELATIVE 8
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((uint32_t)(i))

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

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} __attribute__((packed)) Elf64_Dyn;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} __attribute__((packed)) Elf64_Rela;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
} __attribute__((packed)) Elf64_Rel;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) Elf64_Sym;

static bool elf_validate(const Elf64_Ehdr* eh, size_t size) {
    if (size < sizeof(Elf64_Ehdr)) return false;
    if (eh->e_ident[0] != ELF_MAGIC0 || eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 || eh->e_ident[3] != ELF_MAGIC3) return false;
    if (eh->e_ident[4] != ELFCLASS64) return false;
    if (eh->e_ident[5] != ELFDATA2LSB) return false;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return false;
    if (eh->e_machine != EM_X86_64) return false;
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) return false;
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > size) return false;
    return true;
}

static bool elf_write_u64(uint64_t target_cr3, uint64_t va, uint64_t value) {
    uint64_t pa = 0;
    if (!vmm_resolve(target_cr3, va, &pa, 0)) return false;
    *(uint64_t*)(uintptr_t)pa = value;
    return true;
}

static bool elf_read_u64(uint64_t target_cr3, uint64_t va, uint64_t* out) {
    uint64_t pa = 0;
    if (!vmm_resolve(target_cr3, va, &pa, 0)) return false;
    *out = *(uint64_t*)(uintptr_t)pa;
    return true;
}

static const Elf64_Phdr* elf_find_phdr_for_vaddr(const Elf64_Phdr* ph, uint16_t phnum, uint64_t vaddr) {
    for (uint16_t i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (vaddr >= ph[i].p_vaddr && vaddr < ph[i].p_vaddr + ph[i].p_filesz) {
            return &ph[i];
        }
    }
    return 0;
}

static const uint8_t* elf_vaddr_to_ptr(const uint8_t* image, size_t size, const Elf64_Phdr* ph,
                                       uint16_t phnum, uint64_t vaddr) {
    const Elf64_Phdr* seg = elf_find_phdr_for_vaddr(ph, phnum, vaddr);
    if (!seg) return 0;
    uint64_t off = seg->p_offset + (vaddr - seg->p_vaddr);
    if (off >= size) return 0;
    return image + off;
}

static bool elf_apply_relocations(const uint8_t* image, size_t size, const Elf64_Ehdr* eh,
                                  const Elf64_Phdr* ph, uint64_t target_cr3, uint64_t load_bias,
                                  uint64_t rel_addr, uint64_t rel_sz, uint64_t rel_ent,
                                  uint64_t rela_addr, uint64_t rela_sz, uint64_t rela_ent,
                                  uint64_t symtab_addr, uint64_t syment, uint64_t strtab_addr) {
    if ((rel_addr || rel_sz) && rel_ent == 0) rel_ent = sizeof(Elf64_Rel);
    if ((rela_addr || rela_sz) && rela_ent == 0) rela_ent = sizeof(Elf64_Rela);
    if (syment == 0) syment = sizeof(Elf64_Sym);

    const Elf64_Sym* symtab = 0;
    const char* strtab = 0;
    if (symtab_addr) {
        symtab = (const Elf64_Sym*)elf_vaddr_to_ptr(image, size, ph, eh->e_phnum, symtab_addr);
    }
    if (strtab_addr) {
        strtab = (const char*)elf_vaddr_to_ptr(image, size, ph, eh->e_phnum, strtab_addr);
    }
    (void)strtab;

    uint64_t reloc_base = (eh->e_type == ET_DYN) ? load_bias : 0;

    if (rela_addr && rela_sz) {
        const Elf64_Rela* rela = (const Elf64_Rela*)elf_vaddr_to_ptr(image, size, ph, eh->e_phnum, rela_addr);
        if (!rela) return false;
        uint64_t count = rela_sz / rela_ent;
        for (uint64_t i = 0; i < count; i++) {
            const Elf64_Rela* r = (const Elf64_Rela*)((const uint8_t*)rela + i * rela_ent);
            uint32_t type = ELF64_R_TYPE(r->r_info);
            uint32_t sym_idx = (uint32_t)ELF64_R_SYM(r->r_info);
            uint64_t place = reloc_base + r->r_offset;
            uint64_t value = 0;

            if (type == R_X86_64_RELATIVE) {
                value = load_bias + (uint64_t)r->r_addend;
            } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                if (!symtab) return false;
                const Elf64_Sym* sym = (const Elf64_Sym*)((const uint8_t*)symtab + sym_idx * syment);
                if (sym->st_shndx == SHN_UNDEF) return false;
                value = load_bias + sym->st_value + (uint64_t)r->r_addend;
            } else {
                console_write("[elf] unsupported RELA type\n");
                return false;
            }

            if (!elf_write_u64(target_cr3, place, value)) return false;
        }
    }

    if (rel_addr && rel_sz) {
        const Elf64_Rel* rel = (const Elf64_Rel*)elf_vaddr_to_ptr(image, size, ph, eh->e_phnum, rel_addr);
        if (!rel) return false;
        uint64_t count = rel_sz / rel_ent;
        for (uint64_t i = 0; i < count; i++) {
            const Elf64_Rel* r = (const Elf64_Rel*)((const uint8_t*)rel + i * rel_ent);
            uint32_t type = ELF64_R_TYPE(r->r_info);
            uint32_t sym_idx = (uint32_t)ELF64_R_SYM(r->r_info);
            uint64_t place = reloc_base + r->r_offset;
            uint64_t addend = 0;
            if (!elf_read_u64(target_cr3, place, &addend)) return false;

            uint64_t value = 0;
            if (type == R_X86_64_RELATIVE) {
                value = load_bias + addend;
            } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                if (!symtab) return false;
                const Elf64_Sym* sym = (const Elf64_Sym*)((const uint8_t*)symtab + sym_idx * syment);
                if (sym->st_shndx == SHN_UNDEF) return false;
                value = load_bias + sym->st_value + addend;
            } else {
                console_write("[elf] unsupported REL type\n");
                return false;
            }

            if (!elf_write_u64(target_cr3, place, value)) return false;
        }
    }

    return true;
}

bool elf64_load_image(const uint8_t* image, size_t size, uint64_t target_cr3, uint64_t* out_entry, uint64_t* out_brk) {
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)image;
    if (!elf_validate(eh, size)) {
        console_write("[elf] invalid ELF image\n");
        return false;
    }

    const Elf64_Phdr* ph = (const Elf64_Phdr*)(image + eh->e_phoff);
    uint64_t load_bias = 0;
    if (eh->e_type == ET_DYN) {
        load_bias = align_up_u64(USER_REGION_BASE + 0x01000000ULL, PAGE_SIZE);
    }

    uint64_t max_end = 0;

    /* Map and copy segments. */
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = ph[i].p_vaddr + load_bias;
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

    uint64_t dyn_rela = 0;
    uint64_t dyn_relasz = 0;
    uint64_t dyn_relaent = 0;
    uint64_t dyn_rel = 0;
    uint64_t dyn_relsz = 0;
    uint64_t dyn_relent = 0;
    uint64_t dyn_symtab = 0;
    uint64_t dyn_syment = 0;
    uint64_t dyn_strtab = 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_DYNAMIC) continue;
        const Elf64_Dyn* dyn = (const Elf64_Dyn*)(image + ph[i].p_offset);
        uint64_t count = ph[i].p_filesz / sizeof(Elf64_Dyn);
        for (uint64_t j = 0; j < count; j++) {
            switch (dyn[j].d_tag) {
                case DT_NULL:
                    j = count;
                    break;
                case DT_RELA:
                    dyn_rela = dyn[j].d_val;
                    break;
                case DT_RELASZ:
                    dyn_relasz = dyn[j].d_val;
                    break;
                case DT_RELAENT:
                    dyn_relaent = dyn[j].d_val;
                    break;
                case DT_REL:
                    dyn_rel = dyn[j].d_val;
                    break;
                case DT_RELSZ:
                    dyn_relsz = dyn[j].d_val;
                    break;
                case DT_RELENT:
                    dyn_relent = dyn[j].d_val;
                    break;
                case DT_SYMTAB:
                    dyn_symtab = dyn[j].d_val;
                    break;
                case DT_SYMENT:
                    dyn_syment = dyn[j].d_val;
                    break;
                case DT_STRTAB:
                    dyn_strtab = dyn[j].d_val;
                    break;
                default:
                    break;
            }
        }
    }

    if (dyn_rela || dyn_rel) {
        if (!elf_apply_relocations(image, size, eh, ph, target_cr3, load_bias,
                                   dyn_rel, dyn_relsz, dyn_relent,
                                   dyn_rela, dyn_relasz, dyn_relaent,
                                   dyn_symtab, dyn_syment, dyn_strtab)) {
            console_write("[elf] relocation failed\n");
            return false;
        }
    }

    *out_entry = eh->e_entry + load_bias;
    if (out_brk) *out_brk = max_end;
    console_write("[elf] loaded entry=");
    console_write_hex64(*out_entry);
    console_write("\n");
    return true;
}
