#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#import <CoreGraphics/CoreGraphics.h>

extern "C" {

const char* getMacOSGPUInfo() {
    static NSString* cachedGPUInfo = nil;
    
    if (cachedGPUInfo == nil) {
        @autoreleasepool {
            // Try to get GPU information using Metal
            id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
            if (metalDevice) {
                cachedGPUInfo = [metalDevice.name copy];
            } else {
                // Fallback to IOKit
                io_iterator_t iterator;
                if (IOServiceGetMatchingServices(kIOMasterPortDefault, 
                                               IOServiceMatching("IOPCIDevice"), 
                                               &iterator) == KERN_SUCCESS) {
                    
                    io_service_t service;
                    while ((service = IOIteratorNext(iterator)) != 0) {
                        CFMutableDictionaryRef properties = NULL;
                        if (IORegistryEntryCreateCFProperties(service, &properties, 
                                                            kCFAllocatorDefault, 
                                                            kNilOptions) == KERN_SUCCESS) {
                            
                            CFStringRef className = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("IOName"));
                            if (className) {
                                NSString* classNameStr = (__bridge NSString*)className;
                                if ([classNameStr containsString:@"GPU"] || 
                                    [classNameStr containsString:@"Display"] ||
                                    [classNameStr containsString:@"Radeon"] ||
                                    [classNameStr containsString:@"GeForce"]) {
                                    
                                    CFStringRef model = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("model"));
                                    if (model) {
                                        cachedGPUInfo = [(__bridge NSString*)model copy];
                                        CFRelease(properties);
                                        IOObjectRelease(service);
                                        break;
                                    }
                                }
                            }
                            CFRelease(properties);
                        }
                        IOObjectRelease(service);
                    }
                    IOObjectRelease(iterator);
                }
                
                if (cachedGPUInfo == nil) {
                    cachedGPUInfo = @"Unknown GPU";
                }
            }
        }
    }
    
    return [cachedGPUInfo UTF8String];
}

bool getMacOSDiscreteGPUStatus() {
    @autoreleasepool {
        NSString* gpuInfo = [NSString stringWithUTF8String:getMacOSGPUInfo()];
        
        // Check for discrete GPU indicators
        NSArray* discreteIndicators = @[@"AMD", @"NVIDIA", @"Radeon", @"GeForce", @"RTX", @"GTX"];
        
        for (NSString* indicator in discreteIndicators) {
            if ([gpuInfo rangeOfString:indicator options:NSCaseInsensitiveSearch].location != NSNotFound) {
                return true;
            }
        }
        
        // Additional check for Apple Silicon discrete GPUs
        if ([gpuInfo rangeOfString:@"Pro" options:NSCaseInsensitiveSearch].location != NSNotFound ||
            [gpuInfo rangeOfString:@"Max" options:NSCaseInsensitiveSearch].location != NSNotFound) {
            return true;
        }
        
        return false;
    }
}

int getMacOSVRAMSize() {
    @autoreleasepool {
        id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
        if (metalDevice) {
            // For Apple Silicon and modern GPUs, we can get recommended working set size
            if (@available(macOS 10.15, *)) {
                return (int)(metalDevice.recommendedMaxWorkingSetSize / (1024 * 1024)); // Convert to MiB
            }
        }
        
        // Fallback to IOKit registry
        io_iterator_t iterator;
        if (IOServiceGetMatchingServices(kIOMasterPortDefault, 
                                       IOServiceMatching("IOPCIDevice"), 
                                       &iterator) == KERN_SUCCESS) {
            
            io_service_t service;
            while ((service = IOIteratorNext(iterator)) != 0) {
                CFMutableDictionaryRef properties = NULL;
                if (IORegistryEntryCreateCFProperties(service, &properties, 
                                                    kCFAllocatorDefault, 
                                                    kNilOptions) == KERN_SUCCESS) {
                    
                    CFNumberRef vramSize = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("VRAM,totalMB"));
                    if (vramSize) {
                        int vramMB;
                        CFNumberGetValue(vramSize, kCFNumberIntType, &vramMB);
                        CFRelease(properties);
                        IOObjectRelease(service);
                        IOObjectRelease(iterator);
                        return vramMB;
                    }
                    
                    CFRelease(properties);
                }
                IOObjectRelease(service);
            }
            IOObjectRelease(iterator);
        }
        
        return 0; // Unknown
    }
}

bool getMacOSMetalSupport() {
    @autoreleasepool {
        id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
        return metalDevice != nil;
    }
}

const char* getMacOSMetalDeviceInfo() {
    static NSString* cachedMetalInfo = nil;
    
    if (cachedMetalInfo == nil) {
        @autoreleasepool {
            id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
            if (metalDevice) {
                NSMutableString* info = [NSMutableString stringWithFormat:@"%@", metalDevice.name];
                
                if (@available(macOS 10.15, *)) {
                    [info appendFormat:@" (Max Working Set: %llu MB)", 
                     metalDevice.recommendedMaxWorkingSetSize / (1024 * 1024)];
                }
                
                if (@available(macOS 10.15, *)) {
                    [info appendFormat:@" (Max Buffer Length: %lu MB)", 
                     static_cast<unsigned long>(metalDevice.maxBufferLength / (1024 * 1024))];
                }
                
                if (@available(macOS 10.13, *)) {
                    [info appendFormat:@" (Registry ID: %llu)", metalDevice.registryID];
                }
                
                cachedMetalInfo = [info copy];
            } else {
                cachedMetalInfo = @"Metal not supported";
            }
        }
    }
    
    return [cachedMetalInfo UTF8String];
}

bool getMacOSVideoToolboxSupport() {
    // VideoToolbox is available from macOS 10.8+
    // We can test by trying to create a compression session
    VTCompressionSessionRef compressionSession = NULL;
    
    CFDictionaryRef encoderSpecification = NULL;
    CFDictionaryRef sourceImageBufferAttributes = NULL;
    
    OSStatus status = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        1920, 1080,  // Test dimensions
        kCMVideoCodecType_H264,
        encoderSpecification,
        sourceImageBufferAttributes,
        kCFAllocatorDefault,
        NULL, NULL,  // No callback
        &compressionSession
    );
    
    if (compressionSession) {
        VTCompressionSessionInvalidate(compressionSession);
        CFRelease(compressionSession);
    }
    
    return status == noErr;
}

const char* getMacOSVideoToolboxCodecs() {
    static NSString* cachedCodecs = nil;
    
    if (cachedCodecs == nil) {
        @autoreleasepool {
            NSMutableArray* supportedCodecs = [NSMutableArray array];
            
            // Test common codecs
            NSArray* testCodecs = @[
                @{@"name": @"H.264", @"type": @(kCMVideoCodecType_H264)},
                @{@"name": @"H.265/HEVC", @"type": @(kCMVideoCodecType_HEVC)},
                @{@"name": @"ProRes", @"type": @(kCMVideoCodecType_AppleProRes422)}
            ];
            
            for (NSDictionary* codec in testCodecs) {
                VTCompressionSessionRef compressionSession = NULL;
                CMVideoCodecType codecType = (CMVideoCodecType)[codec[@"type"] intValue];
                
                OSStatus status = VTCompressionSessionCreate(
                    kCFAllocatorDefault,
                    1920, 1080,
                    codecType,
                    NULL, NULL,
                    kCFAllocatorDefault,
                    NULL, NULL,
                    &compressionSession
                );
                
                if (status == noErr && compressionSession) {
                    [supportedCodecs addObject:codec[@"name"]];
                    VTCompressionSessionInvalidate(compressionSession);
                    CFRelease(compressionSession);
                }
            }
            
            cachedCodecs = [[supportedCodecs componentsJoinedByString:@", "] copy];
            if (cachedCodecs.length == 0) {
                cachedCodecs = @"None detected";
            }
        }
    }
    
    return [cachedCodecs UTF8String];
}

bool getMacOSLowPowerModeEnabled() {
    @autoreleasepool {
        if (@available(macOS 12.0, *)) {
            NSProcessInfo* processInfo = [NSProcessInfo processInfo];
            return processInfo.isLowPowerModeEnabled;
        }
        return false;
    }
}

void setMacOSGPUPreference(bool preferIntegrated) {
    @autoreleasepool {
        // Set GPU preference using NSApplication's graphics policy
        NSApplication *app = [NSApplication sharedApplication];
        if (app) {
            if (preferIntegrated) {
                // Request integrated GPU for better battery life
                if ([app respondsToSelector:@selector(setGraphicsPolicy:)]) {
                    // Use runtime check since this is a private API
                    [(id)app setGraphicsPolicy:1]; // Integrated GPU policy
                    NSLog(@"GPU preference set to: Integrated (for better battery life)");
                } else {
                    NSLog(@"GPU preference: Integrated requested but API not available");
                }
            } else {
                // Request discrete GPU for better performance
                if ([app respondsToSelector:@selector(setGraphicsPolicy:)]) {
                    [(id)app setGraphicsPolicy:2]; // Discrete GPU policy
                    NSLog(@"GPU preference set to: Discrete (for better performance)");
                } else {
                    NSLog(@"GPU preference: Discrete requested but API not available");
                }
            }
        } else {
            // Fallback: Set environment variable that some frameworks respect
            if (preferIntegrated) {
                setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1); // Prefer integrated
            } else {
                setenv("METAL_DEVICE_WRAPPER_TYPE", "0", 1); // Prefer discrete
            }
            NSLog(@"GPU preference set via environment variable: %@", 
                  preferIntegrated ? @"Integrated" : @"Discrete");
        }
        
        // Also set a hint for OpenGL contexts
        if (preferIntegrated) {
            setenv("GPU_PREFERENCE", "integrated", 1);
        } else {
            setenv("GPU_PREFERENCE", "discrete", 1);
        }
    }
}

} // extern "C"