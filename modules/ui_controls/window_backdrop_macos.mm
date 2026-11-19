#include "window_backdrop.h"

#include <QQuickWindow>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

@interface ChunfeWindowEffectView : NSVisualEffectView
@end

@implementation ChunfeWindowEffectView
- (NSView *)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
@end

struct WindowBackdrop::NativeBackdrop
{
    NSVisualEffectView *view = nil;
};

bool WindowBackdrop::updateNativeBackdrop(QQuickWindow *window, const QRectF &rect)
{
    NSView *contentView = reinterpret_cast<NSView *>(window->winId());
    NSView *container = contentView.superview;
    if (!container || !contentView.window)
        return false;

    if (!m_nativeBackdrop) {
        auto *effect = [[ChunfeWindowEffectView alloc] initWithFrame:NSZeroRect];
        effect.material = NSVisualEffectMaterialHUDWindow;
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateActive;
        effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark];
        effect.wantsLayer = YES;
        effect.layer.masksToBounds = YES;
        m_nativeBackdrop = new NativeBackdrop{effect};
    }

    NSVisualEffectView *effect = m_nativeBackdrop->view;
    if (effect.superview != container) {
        [effect removeFromSuperview];
        // Keep the effect below the Qt render view, so it never covers QML text.
        [container addSubview:effect positioned:NSWindowBelow relativeTo:contentView];
    }

    const NSRect bounds = contentView.bounds;
    const CGFloat y = contentView.isFlipped ? NSMinY(bounds) + rect.y()
        : NSMaxY(bounds) - rect.y() - rect.height();
    const NSRect localRect = NSMakeRect(NSMinX(bounds) + rect.x(), y, rect.width(), rect.height());
    effect.frame = [contentView convertRect:localRect toView:container];
    effect.layer.cornerRadius = m_cornerRadius;
    return true;
}

void WindowBackdrop::clearNativeBackdrop()
{
    if (!m_nativeBackdrop)
        return;
    [m_nativeBackdrop->view removeFromSuperview];
    [m_nativeBackdrop->view release];
    delete m_nativeBackdrop;
    m_nativeBackdrop = nullptr;
}
