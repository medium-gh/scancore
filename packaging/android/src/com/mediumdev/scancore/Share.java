package com.mediumdev.scancore;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import androidx.core.content.FileProvider;

import java.io.File;

// The system's share sheet for a text (a crash report to send) or a file
// SCANCORE made (an exported EPUB, a comic page), and the clipboard for a
// file, through the app's FileProvider.
public final class Share {
    private Share() {}

    public static boolean text(Activity activity, String text, String subject) {
        try {
            final Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_SUBJECT, subject);
            intent.putExtra(Intent.EXTRA_TEXT, text);
            final Intent chooser = Intent.createChooser(intent, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // Onto the clipboard as a content:// address other apps can paste (a
    // picture into a chat).
    public static boolean copy(Activity activity, String path, String mime) {
        try {
            final Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".qtprovider", new File(path));
            final ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
            final ClipData clip = new ClipData(new File(path).getName(), new String[] {mime}, new ClipData.Item(uri));
            clipboard.setPrimaryClip(clip);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static boolean file(Activity activity, String path, String mime, String subject) {
        try {
            final Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".qtprovider", new File(path));
            final Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType(mime);
            intent.putExtra(Intent.EXTRA_STREAM, uri);
            intent.putExtra(Intent.EXTRA_SUBJECT, subject);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            final Intent chooser = Intent.createChooser(intent, subject);
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            activity.startActivity(chooser);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
