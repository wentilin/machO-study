//
//  symbolhook.h
//  SwiftDemo
//
//  Created by linwenhu on 2020/9/25.
//

#ifndef symbolhook_h
#define symbolhook_h

#include <stdio.h>

struct rebinding {
    const char *name;
    void *replacement;
    void **replaced;
};

void rebind_symbol(struct rebinding rebind);

#endif /* symbolhook_h */
