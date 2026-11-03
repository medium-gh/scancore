package com.mediumdev.scancore;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

// System notifications for SCANCORE's notification area
// (src/app/Notifications.cpp): new chapters, announcements and updates.
// Tapping one opens the app, whose own panel lists them all.
public final class Notify {
    private static final String CHANNEL = "updates";
    private static final String PERMISSION = "android.permission.POST_NOTIFICATIONS";

    private Notify() {}

    // Whether they show: the permission (Android 13 and later), and neither
    // the app's notifications nor their channel switched off in Settings.
    public static boolean allowed(Context context) {
        if (Build.VERSION.SDK_INT >= 33 && context.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED)
            return false;
        NotificationManager manager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) return true;
        if (!manager.areNotificationsEnabled()) return false;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = manager.getNotificationChannel(CHANNEL);
            if (channel != null && channel.getImportance() == NotificationManager.IMPORTANCE_NONE) return false;
        }
        return true;
    }

    // Asks for the permission while Android still shows its prompt (the
    // first time, and once more after a refusal); after that, or when they
    // were switched off in Settings, opens SCANCORE's notification settings.
    public static void ask(Context context) {
        if (allowed(context)) return;
        if (Build.VERSION.SDK_INT >= 33 && context instanceof Activity
                && context.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED) {
            Activity activity = (Activity) context;
            SharedPreferences prefs = context.getSharedPreferences("scancore_notify", Context.MODE_PRIVATE);
            if (!prefs.getBoolean("asked", false) || activity.shouldShowRequestPermissionRationale(PERMISSION)) {
                prefs.edit().putBoolean("asked", true).apply();
                activity.requestPermissions(new String[] {PERMISSION}, 4711);
                return;
            }
        }
        Intent settings;
        if (Build.VERSION.SDK_INT >= 26) {
            settings = new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS);
            settings.putExtra(Settings.EXTRA_APP_PACKAGE, context.getPackageName());
        } else {
            settings = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + context.getPackageName()));
        }
        if (!(context instanceof Activity)) settings.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(settings);
        } catch (Exception e) {
            // No settings screen for it on this device.
        }
    }

    public static void show(Context context, String title, String body, String tag, String channelName) {
        if (!allowed(context)) return;
        NotificationManager manager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) return;
        if (Build.VERSION.SDK_INT >= 26 && manager.getNotificationChannel(CHANNEL) == null)
            manager.createNotificationChannel(new NotificationChannel(CHANNEL, channelName, NotificationManager.IMPORTANCE_DEFAULT));
        Intent open = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
        PendingIntent tap = null;
        if (open != null) {
            open.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            tap = PendingIntent.getActivity(context, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        }
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(context, CHANNEL) : new Notification.Builder(context);
        builder.setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(new Notification.BigTextStyle().bigText(body))
            .setAutoCancel(true);
        if (tap != null) builder.setContentIntent(tap);
        manager.notify(tag, 0, builder.build());
    }
}
