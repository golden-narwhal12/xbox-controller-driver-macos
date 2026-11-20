/*******************************************************************************
 * menubar.m - Menu bar application (Objective-C implementation)
 ******************************************************************************/

#import <Cocoa/Cocoa.h>
#include "../../include/menubar.h"
#include "../../include/platform.h"

/* Ends [NSApp run] so menubar_run() returns and main() can clean up.
   [NSApp terminate:] would exit the process on the spot, skipping the code
   that releases held keys. Main thread only. */
static void stop_run_loop(void) {
    [NSApp stop:nil];
    // -stop: only takes effect after the next event; post one to wake the loop
    NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    [NSApp postEvent:wake atStart:YES];
}

/*******************************************************************************
 * AppDelegate
 ******************************************************************************/
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSStatusItem *statusItem;
@property (nonatomic, strong) NSMenu *statusMenu;
@property (nonatomic, strong) NSMenuItem *statusMenuItem;
@property (nonatomic, assign) void *context;
@property (nonatomic, assign) MenuBarReloadCallback reloadCallback;
@property (nonatomic, assign) MenuBarQuitCallback quitCallback;
@property (nonatomic, assign) BOOL isConnected;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    [self setupStatusBar];

    // Refresh the cursor-confinement target as soon as the frontmost app changes
    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserver:self
           selector:@selector(activeAppChanged:)
               name:NSWorkspaceDidActivateApplicationNotification
             object:nil];
}

- (void)activeAppChanged:(NSNotification *)notification {
    (void)notification;
    window_tracker_poke();
}

/* Logout, shutdown or anything else that terminates the app: leave through
   the normal path instead, so held keys are released. */
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    if (self.quitCallback) {
        self.quitCallback(self.context);
    }
    stop_run_loop();
    return NSTerminateCancel;
}

- (void)setupStatusBar {
    // Create status bar item
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];

    // Set initial icon
    [self updateIcon];

    // Create menu
    self.statusMenu = [[NSMenu alloc] init];

    // Status item (disabled, just shows info)
    self.statusMenuItem = [[NSMenuItem alloc] initWithTitle:@"Status: Initializing..."
                                                     action:nil
                                              keyEquivalent:@""];
    [self.statusMenuItem setEnabled:NO];
    [self.statusMenu addItem:self.statusMenuItem];

    [self.statusMenu addItem:[NSMenuItem separatorItem]];

    // Reload Config
    NSMenuItem *reloadItem = [[NSMenuItem alloc] initWithTitle:@"Reload Configuration"
                                                        action:@selector(reloadConfig:)
                                                 keyEquivalent:@"r"];
    [reloadItem setTarget:self];
    [self.statusMenu addItem:reloadItem];

    [self.statusMenu addItem:[NSMenuItem separatorItem]];

    // About
    NSMenuItem *aboutItem = [[NSMenuItem alloc] initWithTitle:@"About Xbox Controller Driver"
                                                       action:@selector(showAbout:)
                                                keyEquivalent:@""];
    [aboutItem setTarget:self];
    [self.statusMenu addItem:aboutItem];

    [self.statusMenu addItem:[NSMenuItem separatorItem]];

    // Quit
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                      action:@selector(quitApp:)
                                               keyEquivalent:@"q"];
    [quitItem setTarget:self];
    [self.statusMenu addItem:quitItem];

    self.statusItem.menu = self.statusMenu;
}

- (void)updateIcon {
    NSImage *icon;

    if (self.isConnected) {
        // Green controller icon when connected
        icon = [NSImage imageWithSystemSymbolName:@"gamecontroller.fill"
                         accessibilityDescription:@"Connected"];
        if (@available(macOS 12.0, *)) {
            NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration configurationWithPaletteColors:@[[NSColor systemGreenColor]]];
            icon = [icon imageWithSymbolConfiguration:config];
        }
    } else {
        // Gray controller icon when disconnected
        icon = [NSImage imageWithSystemSymbolName:@"gamecontroller"
                         accessibilityDescription:@"Disconnected"];
        if (@available(macOS 12.0, *)) {
            NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration configurationWithPaletteColors:@[[NSColor systemGrayColor]]];
            icon = [icon imageWithSymbolConfiguration:config];
        }
    }

    // Fallback if symbol not available
    if (!icon) {
        icon = [[NSImage alloc] initWithSize:NSMakeSize(18, 18)];
        [icon lockFocus];
        [[NSColor labelColor] set];
        NSBezierPath *path = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(2, 2, 14, 14)];
        if (self.isConnected) {
            [[NSColor systemGreenColor] setFill];
        } else {
            [[NSColor systemGrayColor] setFill];
        }
        [path fill];
        [icon unlockFocus];
    }

    [icon setTemplate:NO];
    self.statusItem.button.image = icon;
}

- (void)reloadConfig:(id)sender {
    (void)sender;
    if (self.reloadCallback) {
        self.reloadCallback(self.context);
    }
}

- (void)showAbout:(id)sender {
    (void)sender;

    // Bring app to front before showing modal dialog
    [NSApp activateIgnoringOtherApps:YES];

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Xbox Controller Driver"];
    [alert setInformativeText:@"A macOS driver for Xbox One controllers.\n\nTranslates controller input to keyboard and mouse events.\n\nVersion 2.0"];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];   // menu actions already run on the main thread
}

- (void)quitApp:(id)sender {
    (void)sender;
    if (self.quitCallback) {
        self.quitCallback(self.context);
    }
    stop_run_loop();
}

- (void)setStatusText:(NSString *)status {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.statusMenuItem setTitle:[NSString stringWithFormat:@"Status: %@", status]];
    });
}

- (void)setConnectedState:(BOOL)connected {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.isConnected = connected;
        [self updateIcon];
    });
}

@end

/*******************************************************************************
 * Global State
 ******************************************************************************/
static AppDelegate *g_appDelegate = nil;

/*******************************************************************************
 * C Interface Implementation
 ******************************************************************************/
void menubar_init(void *context, MenuBarReloadCallback reload_cb, MenuBarQuitCallback quit_cb) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        g_appDelegate = [[AppDelegate alloc] init];
        g_appDelegate.context = context;
        g_appDelegate.reloadCallback = reload_cb;
        g_appDelegate.quitCallback = quit_cb;
        g_appDelegate.isConnected = NO;

        [NSApp setDelegate:g_appDelegate];
    }
}

void menubar_set_status(const char *status) {
    if (g_appDelegate && status) {
        @autoreleasepool {
            NSString *statusStr = [NSString stringWithUTF8String:status];
            [g_appDelegate setStatusText:statusStr];
        }
    }
}

void menubar_set_connected(bool connected) {
    if (g_appDelegate) {
        [g_appDelegate setConnectedState:connected ? YES : NO];
    }
}

void menubar_run(void) {
    @autoreleasepool {
        [NSApp run];
    }
}

void menubar_quit(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        stop_run_loop();
    });
}
