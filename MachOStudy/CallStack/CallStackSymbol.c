//
//  CallStackSymbol.c
//  CallStackDemo
//
//  Created by linwenhu on 2020/9/27.
//

#include "CallStackSymbol.h"
#include <stdlib.h>
#import <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>

typedef struct {
    const struct mach_header *header;
    const char *name;
    uintptr_t slide;
} _MachHeader;

typedef struct {
    _MachHeader *headers;
    uint32_t alloc_len;
} _MachHeaderList;

static _MachHeaderList *machHeaders = NULL;

/**
 获取所有已加载的动态库header
 */
void get_mach_headers() {
    machHeaders = (_MachHeaderList *)malloc(sizeof(_MachHeaderList));
    machHeaders->alloc_len = _dyld_image_count();
    machHeaders->headers = (_MachHeader *)malloc(sizeof(_MachHeader) * machHeaders->alloc_len);
    for (uint32_t i = 0; i < machHeaders->alloc_len; i++) {
        _MachHeader *header = &machHeaders->headers[i];
        header->header = _dyld_get_image_header(i);
        header->name = _dyld_get_image_name(i);
        header->slide = _dyld_get_image_vmaddr_slide(i);
    }
}

/**
 判断pc指针是否在某个动态库中
 */
bool is_pc_in_mach(uintptr_t slide_pc, const struct mach_header *header) {
    uintptr_t cur = (uintptr_t)(((struct mach_header_64 *)header) + 1);
    
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command *command = (struct load_command*)cur;
        // 判断是否在某个段中，__PAGEZERO, __TEXT, __DATA_CONST, __DATA, __LINKEDIT
        if (command->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *segment_command = (struct segment_command_64*)command;
            uintptr_t start = segment_command->vmaddr;
            uintptr_t end = start + segment_command->vmsize;
            if (slide_pc >= start && slide_pc <= end) {
                return true;
            }
        }
        
        cur += command->cmdsize;
    }
    
    return false;
}

/**
 获取地址范围包含pc的动态库header
 */
_MachHeader *get_mach_with_pc(uintptr_t pc) {
    if (!machHeaders) {
        get_mach_headers();
    }
    
    for (uint32_t i = 0; i < machHeaders->alloc_len; i++) {
        _MachHeader *header = &machHeaders->headers[i];
        if (is_pc_in_mach(pc - header->slide, header->header)) {
            return header;
        }
    }
    
    return NULL;
}

/**
 动态库中查找pc指向的函数
 */
void find_symbol(uintptr_t pc, _MachHeader *machHeader, CallStackInfo *csInfo) {
    if (!machHeader) {
        return;
    }
    
    // 获取linkedit段确定base address
    // 获取符号表命令确定符号表和字符串表地址
    struct segment_command_64 *segment_linkedit = NULL;
    struct symtab_command *sym_command = NULL;
    const struct mach_header *header = machHeader->header;
    uintptr_t cur = (uintptr_t)(((struct mach_header_64*)header) + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command *command = (struct load_command*)cur;
        if (command->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *segment_command = (struct segment_command_64*)command;
            if (strcmp(segment_command->segname, SEG_LINKEDIT) == 0) {
                segment_linkedit = segment_command;
            }
        } else if (command->cmd == LC_SYMTAB) {
            sym_command = (struct symtab_command*)command;
        }
        
        cur += command->cmdsize;
    }
    
    if (!segment_linkedit || !sym_command) {
        return;
    }
    
    // 符号表nlist有符号的索引值，可定位符号到字符串符号表
    uintptr_t linkedit_base = (uintptr_t)machHeader->slide + segment_linkedit->vmaddr - segment_linkedit->fileoff;
    struct nlist_64 *symtab = (struct nlist_64 *)(linkedit_base + sym_command->symoff);
    char *strtab = (char *)(linkedit_base + sym_command->stroff);
    
    uintptr_t slide_pc = pc - machHeader->slide;
    uint64_t offset = UINT64_MAX;
    int best = -1;
    for (uint32_t i = 0; i < sym_command->nsyms; i++) {
        // n_value：value of this symbol (or stab offset)
        uint64_t distance = slide_pc - symtab[i].n_value;
        if (slide_pc >= symtab[i].n_value && distance <= offset) {
            offset = distance;
            best = i;
        }
    }
    
    if (best >= 0) {
        FuncInfo *funcInfo = &csInfo->stacks[csInfo->length++];
        funcInfo->machOName = machHeader->name;
        funcInfo->addr = symtab[best].n_value;
        funcInfo->offset = offset;
        // nlist->n_un->n_str: index into the string table
        funcInfo->symbol = (char *)(strtab + symtab[best].n_un.n_strx);
        if (*funcInfo->symbol == '_') {
            funcInfo->symbol++;
        }
        
        if (funcInfo->machOName == NULL) {
            funcInfo->machOName = "";
        }
    }
}

void callstack_symbol(uintptr_t *pc_arr, int arr_len, CallStackInfo *csInfo) {
    for (int i = 0; i < arr_len; i++) {
        _MachHeader *machHeader = get_mach_with_pc(pc_arr[i]);
        if (machHeader) {
            find_symbol(pc_arr[i], machHeader, csInfo);
        }
    }
}
