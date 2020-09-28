//
//  CallStackSymbol.h
//  CallStackDemo
//
//  Created by linwenhu on 2020/9/27.
//

#ifndef CallStackSymbol_h
#define CallStackSymbol_h

#include <stdio.h>

typedef struct {
    uint64_t addr;
    uint64_t offset;
    const char *symbol;
    const char *machOName;
} FuncInfo;

typedef struct {
    FuncInfo *stacks;
    int allocLength;
    int length;
} CallStackInfo;

void callstack_symbol(uintptr_t *pc_arr, int arr_len, CallStackInfo *csInfo);

#endif /* CallStackSymbol_h */
