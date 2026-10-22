#include "native_controls.h"
#include "touch_controls.h"
#include <utility>
#ifdef VF2_IOS
#import <UIKit/UIKit.h>
#define VF2NativeView UIView
#define VF2NativeFont UIFont
#define VF2NativeColor UIColor
#else
#import <Cocoa/Cocoa.h>
#define VF2NativeView NSView
#define VF2NativeFont NSFont
#define VF2NativeColor NSColor
#endif

namespace {
void paint_shape(CGContextRef ctx, CGRect rect, bool circle, bool active, bool base, CGFloat unit)
{
    CGPathRef path = circle ? CGPathCreateWithEllipseInRect(rect, nullptr)
        : CGPathCreateWithRoundedRect(rect, rect.size.height / 2, rect.size.height / 2, nullptr);
    const CGFloat fillAlpha = base ? 0.14 : active ? 0.34 : 0.22;
    const CGFloat rimAlpha = base ? 0.16 : active ? 0.55 : 0.28;
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, path);
    CGContextSetRGBFillColor(ctx, 0.04, 0.05, 0.07, fillAlpha);
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, rimAlpha);
    CGContextSetLineWidth(ctx, unit);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGContextRestoreGState(ctx);
    CGPathRelease(path);
}

const char *label_for(const touch_controls::box &button, bool menu)
{
    if (menu) {
        switch (button.action) {
        case touch_controls::GUARD: case touch_controls::MENU: return "Back";
        case touch_controls::PUNCH: return "OK";
        case touch_controls::KICK: return "Clear";
        case touch_controls::START: return "Resume";
        default: break;
        }
    }
    return button.label;
}
}

@interface VF2ControlsView : VF2NativeView {
@public
    touch_controls *_input;
    std::function<void(int)> _pressed;
@private
    touch_controls::box _buttons[touch_controls::ACTIONS];
    int _count;
    BOOL _menu;
    unsigned _revision;
    CGFloat _unit;
}
- (void)refreshLayout;
- (void)invalidate;
- (void)configureShown:(BOOL)shown menu:(BOOL)menu;
- (void)inputAt:(CGPoint)point identifier:(SDL_FingerID)identifier down:(BOOL)down beginning:(BOOL)beginning;
@end

@implementation VF2ControlsView
- (void)invalidate
{
#ifdef VF2_IOS
    [self setNeedsDisplay];
#else
    [self setNeedsDisplay:YES];
#endif
}
- (void)refreshLayout
{
    CGRect safe = self.bounds;
#ifdef VF2_IOS
    const UIEdgeInsets inset = self.safeAreaInsets;
#else
    const NSEdgeInsets inset = self.safeAreaInsets;
#endif
    safe.origin.x += inset.left;
    safe.origin.y += inset.top;
    safe.size.width -= inset.left + inset.right;
    safe.size.height -= inset.top + inset.bottom;
    _unit = safe.size.height / 384.0;
    _count = touch_controls::layout({float(safe.origin.x), float(safe.origin.y),
        float(safe.size.width), float(safe.size.height)}, _buttons, _menu);
}
- (void)configureShown:(BOOL)shown menu:(BOOL)menu
{
    const BOOL changed = self.hidden == shown || _menu != menu || _revision != _input->revision();
    self.hidden = !shown;
    _menu = menu;
    _revision = _input->revision();
    if (changed) { [self refreshLayout]; [self invalidate]; }
}
- (void)inputAt:(CGPoint)point identifier:(SDL_FingerID)identifier down:(BOOL)down beginning:(BOOL)beginning
{
    if (!_input) return;
    [self refreshLayout];
    const int action = _input->finger_at(identifier, point.x, point.y, down, beginning, _buttons, _count);
    if (action != touch_controls::NONE && _pressed) _pressed(action);
    [self invalidate];
}
#ifdef VF2_IOS
- (BOOL)isOpaque { return NO; }
- (void)layoutSubviews { [super layoutSubviews]; [self refreshLayout]; [self invalidate]; }
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self refreshLayout]; [self invalidate]; }
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event
{
    if (self.hidden || !self.userInteractionEnabled || ![self pointInside:point withEvent:event]) return nil;
    [self refreshLayout];
    return touch_controls::contains(_buttons, _count, point.x, point.y) ? self : nil;
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *touch in touches)
        [self inputAt:[touch locationInView:self] identifier:(SDL_FingerID)(uintptr_t)(__bridge void *)touch down:YES beginning:YES];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *touch in touches)
        [self inputAt:[touch locationInView:self] identifier:(SDL_FingerID)(uintptr_t)(__bridge void *)touch down:YES beginning:NO];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *touch in touches)
        [self inputAt:[touch locationInView:self] identifier:(SDL_FingerID)(uintptr_t)(__bridge void *)touch down:NO beginning:NO];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touchesEnded:touches withEvent:event];
}
#else
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)layout { [super layout]; [self refreshLayout]; [self invalidate]; }
- (NSView *)hitTest:(NSPoint)point
{
    if (self.hidden) return nil;
    const NSPoint local = [self convertPoint:point fromView:self.superview];
    [self refreshLayout];
    return touch_controls::contains(_buttons, _count, local.x, local.y) ? self : nil;
}
- (void)mouseDown:(NSEvent *)event
{
    [self inputAt:[self convertPoint:event.locationInWindow fromView:nil] identifier:0 down:YES beginning:YES];
}
- (void)mouseDragged:(NSEvent *)event
{
    [self inputAt:[self convertPoint:event.locationInWindow fromView:nil] identifier:0 down:YES beginning:NO];
}
- (void)mouseUp:(NSEvent *)event
{
    [self inputAt:[self convertPoint:event.locationInWindow fromView:nil] identifier:0 down:NO beginning:NO];
}
#endif
- (void)drawRect:(CGRect)dirtyRect
{
    if (!_input) return;
    [self refreshLayout];
#ifdef VF2_IOS
    CGContextRef ctx = UIGraphicsGetCurrentContext();
#else
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    CGContextClearRect(ctx, self.bounds);
#endif
    const SDL_FRect stick = touch_controls::stick_bounds(_buttons);
    const CGRect base = CGRectMake(stick.x, stick.y, stick.w, stick.h);
    paint_shape(ctx, base, true, false, true, _unit);
    const CGFloat radius = stick.w / 2, knob = radius * 0.32;
    const SDL_FPoint position = _input->stick_position();
    const CGFloat x = CGRectGetMidX(base) + position.x * (radius - knob);
    const CGFloat y = CGRectGetMidY(base) + position.y * (radius - knob);
    paint_shape(ctx, CGRectMake(x - knob, y - knob, knob * 2, knob * 2), true, _input->stick_active(), false, _unit);
    for (int i = 0; i < _count; ++i) {
        const auto &button = _buttons[i];
        if (button.action <= touch_controls::RIGHT) continue;
        const CGRect rect = CGRectMake(button.r.x, button.r.y, button.r.w, button.r.h);
        paint_shape(ctx, rect, button.circle, _input->held(button.action), false, _unit);
        const CGFloat size = (button.action >= touch_controls::GUARD_PUNCH ? 11 : button.circle ? 16 : 13) * _unit;
#ifdef VF2_IOS
        VF2NativeFont *font = [UIFont systemFontOfSize:size weight:UIFontWeightSemibold];
        VF2NativeColor *ink = [UIColor colorWithWhite:1 alpha:0.95];
#else
        VF2NativeFont *font = [NSFont systemFontOfSize:size weight:NSFontWeightSemibold];
        VF2NativeColor *ink = [NSColor colorWithWhite:1 alpha:0.95];
#endif
        NSDictionary *attributes = @{ NSFontAttributeName:font, NSForegroundColorAttributeName:ink };
        NSString *label = [NSString stringWithUTF8String:label_for(button, _menu)];
        const CGSize textSize = [label sizeWithAttributes:attributes];
        [label drawAtPoint:CGPointMake(CGRectGetMidX(rect) - textSize.width / 2, CGRectGetMidY(rect) - textSize.height / 2)
            withAttributes:attributes];
    }
}
@end

static VF2ControlsView *controls_view;

bool native_controls_attach(SDL_Window *window, touch_controls &input, std::function<void(int)> pressed)
{
    native_controls_detach();
#ifdef VF2_IOS
    UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIView *parent = native.rootViewController.view;
#else
    NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    NSView *parent = native.contentView;
#endif
    if (!parent) return false;
    controls_view = [[VF2ControlsView alloc] initWithFrame:parent.bounds];
    controls_view->_input = &input;
    controls_view->_pressed = std::move(pressed);
    controls_view.hidden = YES;
#ifdef VF2_IOS
    controls_view.backgroundColor = UIColor.clearColor;
    controls_view.multipleTouchEnabled = YES;
    controls_view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    controls_view.contentMode = UIViewContentModeRedraw;
#else
    controls_view.wantsLayer = YES;
    controls_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
#endif
    [parent addSubview:controls_view];
    return true;
}

void native_controls_update(bool shown, bool menu)
{
    [controls_view configureShown:shown menu:menu];
}

void native_controls_detach()
{
    [controls_view removeFromSuperview];
    if (controls_view) {
        controls_view->_pressed = {};
        controls_view->_input = nullptr;
        controls_view = nil;
    }
}
