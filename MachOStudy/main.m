//
//  main.m
//  MachOStudy
//
//  Created by linwenhu on 2020/9/25.
//

#import <Foundation/Foundation.h>
#import "symbolhook.h"
#include <DynamicB/b.h>

extern int global_var;
static void (*origin_func)(int a);

void func(int a) {
    printf("here is a: %d\n", a);
}

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        func(global_var);
        func(global_var);
        
        struct rebinding *rebind = (struct rebinding *)malloc(sizeof(struct rebinding));
        rebind->name = "func";
        rebind->replacement = &func;
        rebind->replaced = (void *)&origin_func;
        rebind_symbol(*rebind);
    }
    return 0;
}
