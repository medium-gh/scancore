package com.mediumdev.scancore;

import org.qtproject.qt.android.bindings.QtService;

// SCANCORE without a window, for LibraryCheckJob: Qt runs main() here with
// --library-check (AndroidManifest.xml), in a process of its own (Qt cannot
// start a service in the process of its activity). It checks the library,
// posts the notifications and ends.
public class LibraryCheckService extends QtService {}
