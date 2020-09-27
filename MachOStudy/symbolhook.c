//
//  symbolhook.c
//  SwiftDemo
//
//  Created by linwenhu on 2020/9/25.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#include <dlfcn.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "symbolhook.h"

#ifdef __LP64__
typedef struct mach_header_64 mach_header_t;
typedef struct segment_command_64 segment_command_t;
typedef struct section_64 section_t;
typedef struct nlist_64 nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT_64
#else
typedef struct mach_header mach_header_t;
typedef struct segment_command segment_command_t;
typedef struct section section_t;
typedef struct nlist nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT_32
#endif

#ifndef SEG_DATA_CONST
#define SEG_DATA_CONST  "__DATA_CONST"
#endif

struct rebinding *rebind_entry;


static vm_prot_t get_protection(void *sectionStart) {
    mach_port_t task = mach_task_self();
    vm_size_t size = 0;
    vm_address_t address = (vm_address_t)sectionStart;
    memory_object_name_t object;
#if __LP64__
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    vm_region_basic_info_data_64_t info;
    kern_return_t info_ret = vm_region_64(task,
                                          &address,
                                          &size,
                                          VM_REGION_BASIC_INFO_64,
                                          (vm_region_info_64_t)&info,
                                          &count,
                                          &object);
#else
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT;
    vm_region_basic_info_data_t info;
    kern_return_t info_ret = vm_region(task,
                                       &address,
                                       &size,
                                       VM_REGION_BASIC_INFO,
                                       (vm_region_info_t)&info,
                                       &count,
                                       &object);
#endif
    if (info_ret == KERN_SUCCESS) {
        return info.protection;
    } else {
        return VM_PROT_READ;
    }
}

static void perform_rebinding_with_section(struct rebinding *rebind, section_t *section, intptr_t slide, nlist_t *symtab, char *strtab, uint32_t *indirect_symtab) {
    const bool isDataConst = strcmp(section->segname, "__DATA_CONST") == 0;
    
    // symbol in indirect_symbol section and binding data section has same indice
    // section's reserved1 reserved (for offset or index)
    uint32_t *indirect_symbol_indices = indirect_symtab + section->reserved1;
    // indirect_symbol address
    void **indirect_symbol_bindings = (void **)((uintptr_t)slide + section->addr);
    
    // change protection of memory region
    vm_prot_t oldProtection = VM_PROT_READ;
    if (isDataConst) {
        oldProtection = get_protection(&rebind);
        mprotect(indirect_symbol_bindings, section->size, PROT_READ | PROT_WRITE);
    }
    
    for (uint i = 0; i < section->size / sizeof(void *); i++) {
        uint32_t symtab_index = indirect_symbol_indices[i];
        if (symtab_index == INDIRECT_SYMBOL_ABS ||
            symtab_index == INDIRECT_SYMBOL_LOCAL ||
            symtab_index == (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS)) {
            continue;
        }
        
        // find symbol offet in string table
        uint32_t strtab_offset = symtab[symtab_index].n_un.n_strx;
        char *symbol_name = strtab + strtab_offset;
        bool symbol_name_longer_than_1 = symbol_name[0] && symbol_name[1];
        if (rebind) {
            if (symbol_name_longer_than_1 && strcmp(&symbol_name[1], rebind->name) == 0) {
                printf("rebind--->name: %s", &symbol_name[1]);
                
                if (rebind_entry->replaced != NULL && indirect_symbol_bindings[i] != rebind_entry->replacement) {
                    *(rebind_entry->replaced) = indirect_symbol_bindings[i];
                }
                
                indirect_symbol_bindings[i] = rebind_entry->replacement;
            }
        }
    }
    
    if (isDataConst) {
        int protection = 0;
        if (oldProtection & VM_PROT_READ) {
            protection |= PROT_READ;
        }
        if (oldProtection & VM_PROT_WRITE) {
            protection |= PROT_WRITE;
        }
        if (oldProtection & VM_PROT_EXECUTE) {
            protection |= PROT_EXEC;
        }
        
        mprotect(indirect_symbol_bindings, section->size, protection);
    }
}

void dyloaded(struct mach_header *header, intptr_t slide) {
    Dl_info info;
    if (dladdr(header, &info) == 0) {
        return;
    }
    
    printf("dylib name: %s\n", info.dli_fname);
    printf("magic: %d\n", header->magic);
    printf("cuptype: %d\n", header->cputype);
    printf("subcuptype: %d\n", header->cpusubtype);
    printf("filetype: %d\n", header->filetype);
    printf("load commands count: %d\n", header->ncmds);
    printf("size of load commands: %d\n", header->sizeofcmds);
   
    // find out linkedit command, symbol table command and dynamic symbol table command
    // segment command may have some section_t
    segment_command_t *linkedit_segment = NULL;
    struct symtab_command* symtab_cmd = NULL;
    struct dysymtab_command* dysymtab_cmd = NULL;
    
    
    // iterate all commands, include segment commands and symbol commands
    segment_command_t *cur_seg_cmd = (segment_command_t *)((uintptr_t)header + sizeof(mach_header_t));
    for (int i = 0; i < header->ncmds; i++) {
        printf("current segment name: %s\n", cur_seg_cmd->segname);
        
        if (cur_seg_cmd->cmd == LC_SYMTAB) {
            symtab_cmd = (struct symtab_command *)cur_seg_cmd;
        } else if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
            if (strcmp(cur_seg_cmd->segname, SEG_LINKEDIT) == 0) {
                linkedit_segment = cur_seg_cmd;
            }
        } else if (cur_seg_cmd->cmd == LC_DYSYMTAB) {
            dysymtab_cmd = (struct dysymtab_command*)cur_seg_cmd;
        }
        
        cur_seg_cmd = (segment_command_t *)((uintptr_t)cur_seg_cmd + cur_seg_cmd->cmdsize);
    }
    
    // ASLR(address space layout randomize)
    uintptr_t linkedit_base = (uintptr_t)slide + linkedit_segment->vmaddr - linkedit_segment->fileoff;
    // symbol table position, a list
    nlist_t *symtab = (nlist_t *)(linkedit_base + symtab_cmd->symoff);
    // string table position, include all symbols
    char *strtab = (char *)(linkedit_base + symtab_cmd->stroff);
    // indirect symbol table, bind from dynamic library
    uint32_t *indirect_symtab = (uint32_t *)(linkedit_base + dysymtab_cmd->indirectsymoff);
    
    // print all local symbols
//    for (uint32_t i = 0; i < symtab_cmd->nsyms; i++) {
//        printf("%s\n", (strtab + (symtab + i)->n_un.n_strx));
//    }
    
    // find section contains symbol
    uintptr_t cur = (uintptr_t)header + sizeof(mach_header_t);
    for (uint i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
        cur_seg_cmd = (segment_command_t *)cur;
        if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
            if (strcmp(cur_seg_cmd->segname, SEG_DATA) != 0 &&
                strcmp(cur_seg_cmd->segname, SEG_DATA_CONST) != 0) {
                continue;
            }
            
            for (uint j = 0; j < cur_seg_cmd->nsects; j++) {
                section_t *sect = (section_t *)(cur + sizeof(segment_command_t)) + j;
                if ((sect->flags & SECTION_TYPE) == S_LAZY_SYMBOL_POINTERS) {
                    printf("section name: %s\n", sect->sectname);
                    perform_rebinding_with_section(rebind_entry, sect, slide, symtab, strtab, indirect_symtab);
                }
                
                if ((sect->flags & SECTION_TYPE) == S_NON_LAZY_SYMBOL_POINTERS) {
                    printf("section name: %s\n", sect->sectname);
                    perform_rebinding_with_section(rebind_entry, sect, slide, symtab, strtab, indirect_symtab);
                }
            }
        }
    }
    
    printf("---------------------------------------\n");
}

void rebind_symbol(struct rebinding rebind) {
    rebind_entry = &rebind;
    _dyld_register_func_for_add_image((void *)dyloaded);
}
