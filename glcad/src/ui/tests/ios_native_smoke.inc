// Included inside CanvasController only in Debug builds. The opt-in launch
// environment keeps these checks isolated from ordinary documents and input.
- (void)runNativeSmokeChecks {
    NSMutableArray* checks = [NSMutableArray array];
    void (^check)(NSString*, BOOL) = ^(NSString* name, BOOL passed) {
        [checks addObject:@{@"name": name, @"passed": @(passed)}];
        NSLog(@"native smoke: %@ %@", passed ? @"PASS" : @"FAIL", name);
    };
    if (!g_shell) { check(@"shell is available", NO); return; }
    g_shell->set_project_type("Mechanical");
    g_shell->build_frame();
    gui::Context::Accessible command;
    for (const auto& field : g_shell->accessible_ui())
        if (field.role == gui::Context::Role::Field && field.label == "Command") command = field;
    check(@"command field is accessible", command.id != 0);
    if (command.id) {
        [self beginEditing:command];
        check(@"native keyboard editor is first responder", self.editor.isFirstResponder);
        self.editor.text = @"LAYER café 零";
        [self textViewDidChange:self.editor];
        check(@"Unicode native replacement reaches shell", g_shell->bar_input() == "LAYER café 零");
        const std::string before = g_shell->bar_input();
        [self.editor setMarkedText:@"未確定" selectedRange:NSMakeRange(0, 3)];
        [self textViewDidChange:self.editor];
        check(@"IME composition does not preview partial text", g_shell->bar_input() == before);
        [self.editor unmarkText];
        [self textViewDidChange:self.editor];
        check(@"completed IME text reaches shell", g_shell->bar_input() == (self.editor.text.UTF8String ?: ""));
        self.editor.text = @"LINE";
        [self textViewDidChange:self.editor];
        [self commitEditor:nil];
        check(@"Done executes command once", g_shell->canvas().tool() == Tool::Line && g_shell->bar_input().empty());
        check(@"Done closes native editor", !self.editor.isFirstResponder && self.editorPanel.hidden);
        g_shell->key_special(1);
        g_shell->build_frame();
    }

    UIView* probe = [[UIView alloc] initWithFrame:CGRectMake(11, 17, 240, 160)];
    [self.view addSubview:probe];
    gui::Id activated = 0;
    void* bridge = create_voice_over((__bridge void*)probe, [&](gui::Id id, int) { activated = id; return true; });
    gui::AccessSnapshot snapshot;
    gui::AccessNode first;
    first.id = 20; first.label = "First"; first.role = gui::Context::Role::Button;
    first.bounds = {4, 8, 40, 24};
    gui::AccessNode second = first; second.id = 10; second.label = "Second";
    snapshot.nodes = {first, second};
    update_voice_over(bridge, snapshot, 2, 160);
    NSArray* elements = probe.accessibilityElements;
    check(@"VoiceOver uses drawing order", elements.count == 2 &&
        [[elements[0] accessibilityLabel] isEqualToString:@"First"] &&
        [[elements[1] accessibilityLabel] isEqualToString:@"Second"]);
    if (elements.count == 2) {
        check(@"VoiceOver activation routes stable identity", [elements[0] accessibilityActivate] && activated == 20);
        CGRect expected = UIAccessibilityConvertFrameToScreenCoordinates(CGRectMake(2, 4, 20, 12), probe);
        check(@"VoiceOver bounds use screen coordinates", CGRectEqualToRect([elements[0] accessibilityFrame], expected));
        snapshot.nodes[0].value = "Changed";
        update_voice_over(bridge, snapshot, 2, 160);
        check(@"VoiceOver retains element identity after update", probe.accessibilityElements[0] == elements[0]);
    }
    destroy_voice_over(bridge);
    [probe removeFromSuperview];

    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* source = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:@"native-smoke-source.txt"]];
    [@"durable source" writeToURL:source atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSURL* copied = [self copyIncomingFile:source];
    [fm removeItemAtURL:source error:nil];
    NSString* recovered = copied ? [NSString stringWithContentsOfURL:copied encoding:NSUTF8StringEncoding error:nil] : nil;
    check(@"Files copy survives source deletion", [recovered isEqualToString:@"durable source"]);
    auto cancelled = std::make_shared<bool>(false);
    [self askFile:Shell::FileAsk::Import completion:[cancelled](std::string path) { *cancelled = path.empty(); }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        check(@"Files picker is presented", [self.presentedViewController isKindOfClass:UIDocumentPickerViewController.class]);
        [self documentPickerWasCancelled:(UIDocumentPickerViewController*)self.presentedViewController];
        [self dismissViewControllerAnimated:NO completion:nil];
        check(@"Files cancellation completes without a path", *cancelled);
        check(@"Files cancellation preserves local copy", copied && [fm fileExistsAtPath:copied.path]);
        if (copied) [fm removeItemAtURL:copied.URLByDeletingLastPathComponent error:nil];
        // Exercise the real simulator renderer with a live boolean, then its
        // exact CPU commit and native-file recovery. The native text/Files
        // checks above and this CAD workflow share the same app instance.
        g_shell->command("NEWTAB");
        ScenarioConfig scenario;
        scenario.seconds=0.25; scenario.fps=12; scenario.paced=false;
        scenario.distance=-1.0;
        std::size_t frames=0, previews=0;
        scenario.render=[&] {
            if (!g_shell->modeller().gpu_preview().tool.empty()) ++previews;
            want_frames(); [self tick:nil]; ++frames;
        };
        const bool modeled=run_box_circle_extrude(*g_shell,scenario);
        check(@"circle cut previews and commits a valid CPU solid",modeled);
        check(@"real renderer draws live boolean preview frames",frames>0 && previews>0);
        const std::string recovery=g_shell->autosave_path();
        check(@"committed solid is recoverable on suspension",g_shell->prepare_suspend());
        Shell reopened;
        const bool opened=reopened.open_file(recovery);
        check(@"saved simulator model reopens as one closed solid",opened &&
            reopened.modeller().bodies().size()==1 && reopened.modeller().bodies()[0].analyse().closed);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (self.presentedViewController) [self dismissViewControllerAnimated:NO completion:nil];
            g_shell->build_frame();
            gui::Context::Accessible exportField;
            for (const auto& field : g_shell->accessible_ui())
                if (field.role == gui::Context::Role::Field && field.label == "Command") exportField = field;
            if (exportField.id) {
                [self beginEditing:exportField];
                self.editor.text = @"EXPORT";
                [self textViewDidChange:self.editor];
                [self commitEditor:nil];
            }
            check(@"typed EXPORT leaves the command line empty", g_shell->bar_input().empty());
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                check(@"typed EXPORT presents the format sheet once", exportField.id && [self.presentedViewController isKindOfClass:UIAlertController.class] && self->_fileCompletion);
                NSUInteger failed = 0;
                for (NSDictionary* result in checks) if (![result[@"passed"] boolValue]) ++failed;
                NSDictionary* report = @{@"checks": checks, @"failures": @(failed)};
                NSURL* documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
                NSURL* path = [documents URLByAppendingPathComponent:@"ios-native-smoke.json"];
                NSData* data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
                [data writeToURL:path atomically:YES];
                NSLog(@"native smoke finished: %lu checks, %lu failures; %@", (unsigned long)checks.count, (unsigned long)failed, path.path);
                want_frames();
            });
        });
    });
}
