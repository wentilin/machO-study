//
//  CallStack.m
//  SwiftDemo
//
//  Created by linwenhu on 2020/9/27.
//

#include <mach/mach_init.h>
#include <mach/task.h>
#include <mach/machine/_structs.h>
#include <mach/vm_map.h>
#include <mach/thread_act.h>

#import "CallStack.h"
#include "CallStackSymbol.h"

#if defined(__arm64__)
#define CS_THREAD_STATE_COUNT ARM_THREAD_STATE64_COUNT
#define CS_THREAD_STATE ARM_THREAD_STATE64
#define CS_FRAME_POINTER __fp
#define CS_LINKER_POINTER __lr
#define CS_INSTRUCTION_ADDRESS __pc

#elif defined(__arm__)
#define CS_THREAD_STATE_COUNT ARM_THREAD_STATE_COUNT
#define CS_THREAD_STATE ARM_THREAD_STATE
#define CS_FRAME_POINTER __r[7]
#define CS_LINKER_POINTER __lr
#define CS_INSTRUCTION_ADDRESS __pc

#elif defined(__x86_64__)
#define CS_THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT
#define CS_THREAD_STATE x86_THREAD_STATE64
#define CS_FRAME_POINTER __rbp
#define CS_LINKER_POINTER __rlr
#define CS_INSTRUCTION_ADDRESS __rip

#elif defined(__i386__)
#define CS_THREAD_STATE_COUNT x86_THREAD_STATE32_COUNT
#define CS_THREAD_STATE x86_THREAD_STATE32
#define CS_FRAME_POINTER __ebp
#define CS_LINKER_POINTER __elr
#define CS_INSTRUCTION_ADDRESS __eip

#endif

#define CALLSTACKDEEP 32

typedef struct {
    const uintptr_t *fp;
    const uintptr_t lr;
} StackFrameFP_LR;

static thread_t _main_thread;

@implementation CallStack

+ (void)load {
    _main_thread = mach_thread_self();
}

+ (NSString *)callStackWith: (CallStackType)type {
    NSString *result;
    if (type == CallStackTypeAll) {
        thread_act_array_t threads;
        mach_msg_type_number_t thread_count = 0;
        if (KERN_SUCCESS != task_threads(mach_task_self(), &threads, &thread_count)) {
            return @"fail to get threads";
        }
        
        NSMutableString *resultStr = [NSMutableString string];
        [resultStr appendFormat:@"current thread count: %d", thread_count];
        for (int i = 0; i < thread_count; i++) {
            [resultStr appendFormat:@"%@", [self callStackWithThread:threads[i]]];
        }
        
        result = resultStr.copy;
    } else if (type == CallStackTypeMainThread) {
        if ([NSThread currentThread].isMainThread) {
            result = [self callStackWithCurrentThread];
        }
    } else {
        result = [self callStackWithThread:_main_thread];
    }
    
    return result.copy;
}

/*
 ARM64：从栈顶开始读取线程函数调用栈
 --------------------
 |-------------------<<---fp
 |-------------------<<---lr
 |--------func A-----
 |-------------------<<---fp
 |-------------------<<---lr
 |--------func B-----<<---pc
 x86-64：
 --------------------
 |-------------------<<---fp
 |-------------------<<---lr
 |--------func A-----
 |-------------------<<---return address(8(%rbp))
 |-------------------<<---save rbp(0(%rbp))
 |--------func B-----
 |-------------------
 |-------------------<<---outgoing arguments
 */
+ (NSString *)callStackWithThread:(thread_t)thread {
    _STRUCT_MCONTEXT machineContext;
    if (!getMachineContext(thread, &machineContext)) {
        return [NSString stringWithFormat:@"fail to get thread(%u) state", thread];
    }

    uintptr_t pc = machineContext.__ss.CS_INSTRUCTION_ADDRESS;
    uintptr_t fp = machineContext.__ss.CS_FRAME_POINTER;
#if defined(__x86_64__)
    // 调用callq指令时会将返回函数压栈（8b）
    uintptr_t lr = fp + 64;
#else
    uintptr_t lr = machineContext.__ss.CS_LINKER_POINTER;
#endif
    uintptr_t pcArr[CALLSTACKDEEP];
    int i = 0;
    pcArr[i++] = pc;
    StackFrameFP_LR frame = { (void *)fp, lr };
    vm_size_t len = sizeof(frame);
    while (frame.fp && i < CALLSTACKDEEP) {
        pcArr[i++] = frame.lr;
        bool flag = readFPMemory(frame.fp, &frame, len);
        if (!flag || frame.fp == 0 || frame.lr == 0) {
            break;
        }
    }
    
    return generateSymbol(pcArr, i, thread);
}

+ (NSString *)callStackWithCurrentThread {
    NSArray *arr = [NSThread callStackSymbols];
    
    return [arr componentsJoinedByString:@"\n"];
}

/**
 生成线程的调用栈符号表
 */
NSString *generateSymbol(uintptr_t *pcArr, int arrLen, thread_t thread) {
    CallStackInfo *csInfo = (CallStackInfo *)malloc(sizeof(CallStackInfo));
    if (csInfo == NULL) {
        return @"malloc fail";
    }
    csInfo->length = 0;
    csInfo->allocLength = arrLen;
    csInfo->stacks = (FuncInfo *)malloc(sizeof(FuncInfo) * csInfo->allocLength);
    if (csInfo->stacks == NULL) {
        return @"malloc fail";
    }
    
    callstack_symbol(pcArr, arrLen, csInfo);
    
    NSMutableString *strM = [NSMutableString stringWithFormat:@"\ncallStack of thread: %u\n", thread];
    for (int j = 0; j < csInfo->length; j++) {
        [strM appendFormat:@"%@", formatFuncInfo(csInfo->stacks[j])];
    }
    
    if (csInfo->stacks) {
        free(csInfo->stacks);
    }
    if (csInfo) {
        free(csInfo);
    }
    
    return strM.copy;
}

/**
 格式化函数栈信息
 */
NSString *formatFuncInfo(FuncInfo info) {
    if (info.symbol == NULL) {
        return @"";
    }
    char *lastPath = strrchr(info.machOName, '/');
    NSString *fname = @"";
    if (lastPath == NULL) {
        fname = [NSString stringWithFormat:@"%-30s", info.machOName];
    }
    else
    {
        fname = [NSString stringWithFormat:@"%-30s", lastPath+1];
    }
    return [NSString stringWithFormat:@"%@ 0x%08" PRIxPTR " %s  +  %llu\n", fname, (uintptr_t)info.addr, info.symbol, info.offset];
}

/**
 获取线程上下文信息
 */
bool getMachineContext(thread_t thread, _STRUCT_MCONTEXT *machineContext) {
    mach_msg_type_number_t state_count = CS_THREAD_STATE_COUNT;
    return KERN_SUCCESS == thread_get_state(thread,
                                            CS_THREAD_STATE,
                                            (thread_state_t)&machineContext->__ss,
                                            &state_count);
}

/*
 读取从fp开始的16字节，包含了fp和lr
 arm64: sub    sp, sp, #32
        stp    x29, x30, [sp, #16]
        高地址向低地址每个8byte：x29(fp), x30(lr)
 CISC:  pushq   %rbp        save return address
        movq    %rsp %rbp   save rbp
 */
bool readFPMemory(const void *fp, const void *dest, const vm_size_t len) {
    vm_size_t bytesCopied = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)fp, len, (vm_address_t)dest, &bytesCopied);
    return KERN_SUCCESS == kr;
}

@end
