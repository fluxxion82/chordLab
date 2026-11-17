// The editor side of vst3host: see editor.h. Objective-C++ under ARC.

#import <Cocoa/Cocoa.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include "editor.h"

#include "ChordLab-Shared.h"

#include <set>

namespace editor {

const int kPreview = static_cast<int>(kMsgTagPreview);
const int kPanic = static_cast<int>(kMsgTagPanic);
const int kError = static_cast<int>(kMsgTagError);

namespace {

std::vector<std::string> gErrors;
std::set<Class> gRecorded;   // the controller classes whose onMessage::: records

/// The editor's view controller in `parent`: OpenWindow adds the controller's view to it, and an NSViewController
/// is its view's next responder.
NSViewController* controllerIn(void* parent) {
    NSView* view = (__bridge NSView*) parent;
    for (NSView* sub in view.subviews) {
        NSResponder* next = sub.nextResponder;
        if ([next isKindOfClass:[NSViewController class]] &&
            [next respondsToSelector:NSSelectorFromString(@"sendArbitraryMsgFromUI:::")])
            return (NSViewController*) next;
    }
    return nil;
}

/// Makes the class of `controller` record every kError message the plug-in sends it (iPlug2's delegate calls
/// onMessage::: on it), then run its own onMessage::: as before.
void recordErrors(NSViewController* controller) {
    Class cls = object_getClass(controller);
    if (gRecorded.count(cls)) return;
    const SEL selector = NSSelectorFromString(@"onMessage:::");
    Method method = class_getInstanceMethod(cls, selector);
    if (!method) return;
    using OnMessage = BOOL (*)(id, SEL, NSInteger, NSInteger, NSData*);
    const OnMessage original = (OnMessage) method_getImplementation(method);
    IMP recording = imp_implementationWithBlock(^BOOL(id self, NSInteger msgTag, NSInteger ctrlTag, NSData* msg) {
        if (msgTag == kMsgTagError)
            gErrors.emplace_back(msg ? std::string((const char*) msg.bytes, msg.length) : std::string());
        return original(self, selector, msgTag, ctrlTag, msg);
    });
    // On the class itself, so that the original stays the one its superclass chain would have run.
    if (!class_addMethod(cls, selector, recording, method_getTypeEncoding(method)))
        method_setImplementation(method, recording);
    gRecorded.insert(cls);
}

}  // namespace

void withPool(const std::function<void()>& body) {
    @autoreleasepool {
        body();
    }
}

void startApplication() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
}

void* makeParent(int width, int height) {
    @autoreleasepool {
        NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
        return (__bridge_retained void*) view;
    }
}

void releaseParent(void* parent) {
    @autoreleasepool {
        CFRelease(parent);   // the retain makeParent's __bridge_retained handed over
    }
}

bool hasController(void* parent) {
    @autoreleasepool {
        NSViewController* controller = controllerIn(parent);
        if (controller) recordErrors(controller);
        return controller != nil;
    }
}

bool send(void* parent, int tag, const std::string& json) {
    @autoreleasepool {
        NSViewController* controller = controllerIn(parent);
        if (!controller) return false;
        recordErrors(controller);
        NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
        using Send = void (*)(id, SEL, NSInteger, NSInteger, NSData*);
        ((Send) objc_msgSend)(controller, NSSelectorFromString(@"sendArbitraryMsgFromUI:::"), tag, kNoCtrlTag, data);
        return true;
    }
}

std::vector<std::string> takeErrors() {
    std::vector<std::string> errors;
    errors.swap(gErrors);
    return errors;
}

}  // namespace editor
