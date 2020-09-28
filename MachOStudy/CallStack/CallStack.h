//
//  CallStack.h
//  SwiftDemo
//
//  Created by linwenhu on 2020/9/27.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger) {
    CallStackTypeAll,
    CallStackTypeMainThread,
    CallStackTypeCurrentThread
} CallStackType;

@interface CallStack : NSObject

+ (NSString *)callStackWith: (CallStackType)type;

@end

NS_ASSUME_NONNULL_END
