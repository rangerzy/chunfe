#import <Foundation/Foundation.h>

#include <cstdio>

namespace {
bool check(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}

NSDictionary* readStrings(NSString* bundlePath, NSString* language)
{
    NSString* relative = [NSString stringWithFormat:@"Contents/Resources/%@.lproj/InfoPlist.strings", language];
    NSData* data = [NSData dataWithContentsOfFile:[bundlePath stringByAppendingPathComponent:relative]];
    if (!data) return nil;
    id value = [NSPropertyListSerialization propertyListWithData:data options:0 format:nullptr error:nil];
    return [value isKindOfClass:[NSDictionary class]] ? value : nil;
}
}

int main(int argc, char* argv[])
{
    @autoreleasepool {
        if (!check(argc == 2, "Expected a built application bundle path")) return 1;
        NSString* path = [NSString stringWithUTF8String:argv[1]];
        NSBundle* bundle = [NSBundle bundleWithPath:path];
        NSDictionary* info = [bundle infoDictionary];
        NSArray* languages = [bundle localizations];
        NSArray* chinese = [NSBundle preferredLocalizationsFromArray:languages forPreferences:@[@"zh-Hans-CN"]];
        NSArray* english = [NSBundle preferredLocalizationsFromArray:languages forPreferences:@[@"en-US"]];
        NSDictionary* zh = readStrings(path, @"zh-Hans");
        NSDictionary* en = readStrings(path, @"en");
        if (!check([[info objectForKey:@"CFBundleDevelopmentRegion"] isEqualToString:@"zh-Hans"], "Bundle development language must be zh-Hans") ||
            !check([languages containsObject:@"zh-Hans"] && [languages containsObject:@"en"], "Chinese and English localizations must both exist") ||
            !check([[chinese firstObject] isEqualToString:@"zh-Hans"], "Chinese preferences must select zh-Hans") ||
            !check([[english firstObject] isEqualToString:@"en"], "English preferences must select en") ||
            !check([[zh objectForKey:@"CFBundleDisplayName"] isEqualToString:@"春风"] &&
                   [[zh objectForKey:@"CFBundleName"] isEqualToString:@"春风"], "Chinese bundle name must be localized") ||
            !check([[en objectForKey:@"CFBundleDisplayName"] isEqualToString:@"Chunfe"] &&
                   [[en objectForKey:@"CFBundleName"] isEqualToString:@"Chunfe"], "English bundle name must be localized") ||
            !check([[zh objectForKey:@"NSMicrophoneUsageDescription"] length] > 0 &&
                   [[zh objectForKey:@"NSAudioCaptureUsageDescription"] length] > 0 &&
                   [[en objectForKey:@"NSMicrophoneUsageDescription"] length] > 0 &&
                   [[en objectForKey:@"NSAudioCaptureUsageDescription"] length] > 0, "Both languages must describe microphone and system-audio usage"))
            return 1;
        return 0;
    }
}
