#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "Host/Vst3Host.h"

struct Vst3UiWindow {
    NSWindow* window;
    NSView* contentView;
};

@interface Vst3DefaultParamTarget : NSObject
@property(nonatomic, assign) Vst3DefaultEditorSetParamFn setParam;
@property(nonatomic, assign) void* userData;
@end

@implementation Vst3DefaultParamTarget
- (void)sliderChanged:(NSSlider*)sender {
    if (!self.setParam) return;
    self.setParam(self.userData, static_cast<int32_t>(sender.tag), sender.doubleValue);
}
@end

static void Vst3UI_EnsureApp() {
    NSApplication* app = [NSApplication sharedApplication];
    if (app && [app activationPolicy] == NSApplicationActivationPolicyProhibited) {
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
}

Vst3UiWindow* Vst3UI_CreateWindow(const char* title, int width, int height) {
    Vst3UI_EnsureApp();
    NSString* nsTitle = title ? [NSString stringWithUTF8String:title] : @"VST3";
    NSRect rect = NSMakeRect(0, 0, width > 0 ? width : 640, height > 0 ? height : 480);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:nsTitle];
    [window center];
    NSView* content = [[NSView alloc] initWithFrame:rect];
    [window setContentView:content];
    auto* handle = new Vst3UiWindow{window, content};
    return handle;
}

Vst3UiWindow* Vst3UI_CreateDefaultEditorWindow(const char* title,
                                               const Vst3DefaultEditorParam* params,
                                               int paramCount,
                                               Vst3DefaultEditorSetParamFn setParam,
                                               void* userData) {
    Vst3UI_EnsureApp();
    const int clampedCount = paramCount > 0 ? paramCount : 0;
    const int width = 520;
    const int rowHeight = 34;
    const int topPad = 48;
    const int bottomPad = 18;
    const int contentHeight = MAX(220, topPad + bottomPad + clampedCount * rowHeight);
    const int windowHeight = MIN(640, contentHeight);

    NSString* nsTitle = title ? [NSString stringWithUTF8String:title] : @"VST3 Parameters";
    NSRect rect = NSMakeRect(0, 0, width, windowHeight);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:[nsTitle stringByAppendingString:@" Parameters"]];
    [window center];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:rect];
    [scroll setHasVerticalScroller:YES];
    [scroll setAutohidesScrollers:YES];
    [scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    NSView* document = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, contentHeight)];
    Vst3DefaultParamTarget* target = [[Vst3DefaultParamTarget alloc] init];
    target.setParam = setParam;
    target.userData = userData;
    objc_setAssociatedObject(document, "Vst3DefaultParamTarget", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSTextField* header = [NSTextField labelWithString:@"Generic parameter editor"];
    [header setFrame:NSMakeRect(16, contentHeight - 32, width - 32, 20)];
    [header setFont:[NSFont boldSystemFontOfSize:13.0]];
    [document addSubview:header];

    for (int i = 0; i < clampedCount; ++i) {
        const Vst3DefaultEditorParam& param = params[i];
        const int y = contentHeight - topPad - (i + 1) * rowHeight;
        NSString* paramTitle = param.title[0] ? [NSString stringWithUTF8String:param.title] : @"Parameter";
        if (param.units[0]) {
            paramTitle = [paramTitle stringByAppendingFormat:@" (%s)", param.units];
        }

        NSTextField* label = [NSTextField labelWithString:paramTitle];
        [label setFrame:NSMakeRect(16, y + 8, 190, 18)];
        [label setLineBreakMode:NSLineBreakByTruncatingTail];
        [document addSubview:label];

        NSSlider* slider = [NSSlider sliderWithValue:param.value
                                            minValue:0.0
                                            maxValue:1.0
                                              target:target
                                              action:@selector(sliderChanged:)];
        [slider setTag:param.id];
        [slider setFrame:NSMakeRect(214, y + 5, width - 236, 24)];
        [slider setContinuous:YES];
        [document addSubview:slider];
    }

    [scroll setDocumentView:document];
    [window setContentView:scroll];
    auto* handle = new Vst3UiWindow{window, scroll};
    return handle;
}

void* Vst3UI_GetContentView(Vst3UiWindow* window) {
    if (!window || !window->contentView) return nullptr;
    return (__bridge void*)window->contentView;
}

void Vst3UI_ShowWindow(Vst3UiWindow* window) {
    if (!window || !window->window) return;
    [window->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void Vst3UI_HideWindow(Vst3UiWindow* window) {
    if (!window || !window->window) return;
    [window->window orderOut:nil];
}

void Vst3UI_SetWindowSize(Vst3UiWindow* window, int width, int height) {
    if (!window || !window->window) return;
    NSSize size = NSMakeSize(width > 0 ? width : 640, height > 0 ? height : 480);
    [window->window setContentSize:size];
}

void Vst3UI_CloseWindow(Vst3UiWindow* window) {
    if (!window) return;
    if (window->window) {
        [window->window orderOut:nil];
        [window->window close];
    }
    delete window;
}
