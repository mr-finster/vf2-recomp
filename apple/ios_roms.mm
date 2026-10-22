#include "ios_roms.h"
#include <SDL3/SDL.h>
#include <memory>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static void remove_abandoned_imports(NSURL *documents)
{
    NSFileManager *files = NSFileManager.defaultManager;
    NSPredicate *ownedName = [NSPredicate predicateWithFormat:@"SELF MATCHES %@", @"\\.vf2-import-[0-9]+-[0-9]+"];
    NSArray<NSURL *> *items = [files contentsOfDirectoryAtURL:documents
        includingPropertiesForKeys:@[NSURLIsDirectoryKey, NSURLIsSymbolicLinkKey] options:0 error:nil];
    for (NSURL *item in items) {
        if (![ownedName evaluateWithObject:item.lastPathComponent]) continue;
        NSDictionary *attrs = [item resourceValuesForKeys:@[NSURLIsDirectoryKey, NSURLIsSymbolicLinkKey] error:nil];
        if (![attrs[NSURLIsDirectoryKey] boolValue] || [attrs[NSURLIsSymbolicLinkKey] boolValue]) continue;
        NSArray<NSString *> *contents = [files contentsOfDirectoryAtPath:item.path error:nil];
        // Only our staging shape; preserve anything else in Documents.
        if (contents && (!contents.count || (contents.count == 1 && [contents.firstObject isEqual:@"vf2.zip"])))
            [files removeItemAtURL:item error:nil];
    }
}

@interface VF2ROMController : UIViewController <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate> {
    romset::result _roms;
}
@property(nonatomic) BOOL ready;
@property(nonatomic) BOOL busy;
@property(nonatomic, strong) NSURL *documents;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UIButton *choose;
@property(nonatomic, strong) NSMutableDictionary<NSString *, NSArray *> *checked;
- (void)scanFiles;
- (void)refreshFiles;
- (void)chooseFile;
- (void)readURL:(NSURL *)url importing:(BOOL)importing;
- (romset::result)takeROMs;
@end

@implementation VF2ROMController
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.checked = [NSMutableDictionary dictionary];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refreshFiles)
        name:UIApplicationDidBecomeActiveNotification object:nil];
    self.documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    remove_abandoned_imports(self.documents); // before any import worker can start
    NSError *error = nil;
    [NSFileManager.defaultManager createDirectoryAtURL:[self.documents URLByAppendingPathComponent:@"roms" isDirectory:YES]
        withIntermediateDirectories:YES attributes:nil error:&error];

    UILabel *title = [UILabel new];
    title.text = @"Virtua Fighter 2";
    title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle1];
    title.accessibilityTraits = UIAccessibilityTraitHeader;
    UILabel *instructions = [UILabel new];
    instructions.text = @"Copy vf2.zip to Files → On My iPhone/iPad → VF2 Recomp → roms.\nKeep the ZIP compressed, then return to the app to start automatically. You can also import it from iCloud Drive or another location.";
    instructions.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    instructions.numberOfLines = 0;
    self.choose = [UIButton buttonWithType:UIButtonTypeSystem];
    self.choose.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [self.choose setTitle:@"Import vf2.zip from Files" forState:UIControlStateNormal];
    [self.choose addTarget:self action:@selector(chooseFile) forControlEvents:UIControlEventTouchUpInside];
    self.choose.accessibilityIdentifier = @"importROM";
    self.status = [UILabel new];
    self.status.text = error ? error.localizedDescription : @"Requires the complete MAME vf2 (Version 2.1) ROM set as a ZIP file.";
    self.status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    self.status.textColor = UIColor.secondaryLabelColor;
    self.status.numberOfLines = 0;
    self.status.accessibilityIdentifier = @"romStatus";
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[title, instructions, self.choose, self.status]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 14;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    UIScrollView *scroll = [UIScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];
    [scroll addSubview:stack];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:28],
        [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-28],
        [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16],
        [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor]
    ]];
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)refreshFiles
{
    // Providers may preserve the size and timestamp when replacing a document.
    [self.checked removeAllObjects];
    [self scanFiles];
}
- (void)chooseFile
{
    if (self.busy || self.ready) return;
    self.busy = YES;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[UTTypeZIP] asCopy:NO];
    picker.allowsMultipleSelection = NO;
    picker.delegate = self;
    picker.directoryURL = self.documents;
    [self presentViewController:picker animated:YES completion:nil];
    picker.presentationController.delegate = self;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    self.busy = NO;
}
- (void)presentationControllerDidDismiss:(UIPresentationController *)presentationController
{
    self.busy = NO;
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    if (!urls.count) { self.busy = NO; return; }
    [self readURL:urls.firstObject importing:YES];
}
- (void)readURL:(NSURL *)url importing:(BOOL)importing
{
    self.busy = YES;
    self.choose.enabled = NO;
    self.status.text = @"Checking vf2.zip…";
    NSURL *destination = [self.documents URLByAppendingPathComponent:@"vf2.zip"];
    // Coordination downloads provider files as needed and protects against a Files
    // write during the copy. Keep scoped access until all reads have finished.
    BOOL scoped = [url startAccessingSecurityScopedResource];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto result = std::make_shared<romset::result>();
        @autoreleasepool {
            NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            NSError *error = nil;
            [coordinator coordinateReadingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *readable) {
                *result = importing
                    ? romset::import_file(readable.fileSystemRepresentation, destination.fileSystemRepresentation)
                    : romset::load(readable.fileSystemRepresentation);
            }];
            if (error) { result->ok = false; result->error = error.localizedDescription.UTF8String; }
            if (scoped) [url stopAccessingSecurityScopedResource];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy = NO;
            self.choose.enabled = YES;
            if (result->ok) {
                self->_roms = std::move(*result);
                self.status.text = @"ROM verified. Starting the game…";
                self.ready = YES;
            } else {
                self.status.text = [@"Cannot load this file. Choose the complete MAME vf2 (Version 2.1) ZIP.\n"
                    stringByAppendingString:[NSString stringWithUTF8String:result->error.c_str()]];
            }
        });
    });
}
- (void)scanFiles
{
    if (self.busy || self.ready || UIApplication.sharedApplication.applicationState != UIApplicationStateActive) return;
    for (NSString *name in @[@"vf2.zip", @"roms/vf2.zip"]) {
        NSURL *url = [self.documents URLByAppendingPathComponent:name];
        NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:url.path error:nil];
        if (![attributes[NSFileType] isEqual:NSFileTypeRegular]) { [self.checked removeObjectForKey:name]; continue; }
        NSArray *fingerprint = @[attributes[NSFileSize], attributes[NSFileModificationDate]];
        if ([self.checked[name] isEqual:fingerprint]) continue;
        self.checked[name] = fingerprint;
        [self readURL:url importing:NO];
        break;
    }
}
- (romset::result)takeROMs { return std::move(_roms); }
@end

static UIWindow *loading_window;

static UIWindow *status_window(UIViewController *controller)
{
    UIWindowScene *scene = nil;
    for (UIScene *candidate in UIApplication.sharedApplication.connectedScenes) {
        if ([candidate isKindOfClass:UIWindowScene.class] &&
            candidate.activationState != UISceneActivationStateUnattached) {
            scene = (UIWindowScene *)candidate;
            break;
        }
    }
    UIWindow *window = scene ? [[UIWindow alloc] initWithWindowScene:scene]
                             : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    window.rootViewController = controller;
    [window makeKeyAndVisible];
    [controller loadViewIfNeeded];
    return window;
}

[[noreturn]] void ios_fatal_error(const std::string &message)
{
    @autoreleasepool {
        UIViewController *controller = [UIViewController new];
        controller.view.backgroundColor = UIColor.systemBackgroundColor;
        UILabel *label = [UILabel new];
        label.numberOfLines = 0;
        label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        NSString *detail = [NSString stringWithUTF8String:message.c_str()] ?: @"Unknown error";
        label.text = [@"The game cannot continue. Close the app and launch it again.\n\n"
            stringByAppendingString:detail];
        label.translatesAutoresizingMaskIntoConstraints = NO;
        [controller.view addSubview:label];
        UILayoutGuide *safe = controller.view.safeAreaLayoutGuide;
        [NSLayoutConstraint activateConstraints:@[
            [label.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:28],
            [label.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-28],
            [label.topAnchor constraintEqualToAnchor:safe.topAnchor constant:28]
        ]];
        UIWindow *previous = loading_window;
        loading_window = status_window(controller);
        previous.hidden = YES;
        previous.rootViewController = nil;
    }
    // SDL's UIKit main does not exit the app when SDL_main returns. Keep an
    // actionable UIKit screen responsive instead of leaving a dead SDL window.
    for (;;) {
        @autoreleasepool {
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        }
    }
}

void ios_pump_loading()
{
    @autoreleasepool {
        SDL_PumpEvents();
        while (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) {
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
    }
}

void ios_finish_loading()
{
    loading_window.hidden = YES;
    loading_window.rootViewController = nil;
    loading_window = nil;
}

bool ios_load_roms(romset::result &out)
{
    @autoreleasepool {
        if (!SDL_Init(SDL_INIT_VIDEO)) return false;
        SDL_PumpEvents();
        VF2ROMController *controller = [VF2ROMController new];
        // SDL 3 uses a UIWindowScene on modern iOS. A scene-less UIWindow can
        // load its views but never appear on that scene's display.
        UIWindow *window = status_window(controller);
        NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer *) {
            [controller scanFiles];
        }];
        [controller scanFiles];
        while (!controller.ready) {
            @autoreleasepool {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {}
                [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
            }
        }
        [timer invalidate];
        out = [controller takeROMs];
        loading_window = window; // keep the loading message visible through the arcade boot
        return out.ok;
    }
}
