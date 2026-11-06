package com.mediumdev.scancore;

import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.IBinder;
import android.os.PersistableBundle;

// Library update checks while SCANCORE is not on screen (src/app/App.cpp,
// scheduleLibraryCheck). Android ends an app soon after it leaves the screen,
// and with it the app's own timer, so new chapters and episodes were only
// found while it was open. The system runs this job every few hours, even
// after SCANCORE was closed or the phone restarted: a SCANCORE still running
// checks by itself (nativeCheck); otherwise LibraryCheckService starts one
// without a window in a process of its own, which checks, notifies and ends.
public final class LibraryCheckJob extends JobService {
    private static final int JOB_ID = 4720;
    private static LibraryCheckJob running;
    private static JobParameters runningParams;
    private ServiceConnection connection;

    // Answered by the running app (src/app/Platform.cpp): false when it is
    // closing; missing (UnsatisfiedLinkError) when it is not running here.
    static native boolean nativeCheck();

    // Every `hours` hours, with a network (unmetered or while charging when
    // asked); 0 or less cancels it. Rescheduled only when that changes, as
    // rescheduling a periodic job restarts its wait.
    public static void schedule(Context context, int hours, boolean unmetered, boolean charging) {
        JobScheduler scheduler = (JobScheduler) context.getSystemService(Context.JOB_SCHEDULER_SERVICE);
        if (scheduler == null) return;
        if (hours <= 0) {
            scheduler.cancel(JOB_ID);
            return;
        }
        long interval = hours * 3600_000L;
        JobInfo pending = Build.VERSION.SDK_INT >= 24 ? scheduler.getPendingJob(JOB_ID) : null;
        if (pending != null && pending.getIntervalMillis() == interval && pending.isRequireCharging() == charging
                && pending.getNetworkType() == (unmetered ? JobInfo.NETWORK_TYPE_UNMETERED : JobInfo.NETWORK_TYPE_ANY))
            return;
        JobInfo.Builder job = new JobInfo.Builder(JOB_ID, new ComponentName(context, LibraryCheckJob.class))
            .setRequiredNetworkType(unmetered ? JobInfo.NETWORK_TYPE_UNMETERED : JobInfo.NETWORK_TYPE_ANY)
            .setRequiresCharging(charging)
            .setPersisted(true);
        // A check may come up to an hour early, when the phone is awake anyway.
        if (Build.VERSION.SDK_INT >= 24) job.setPeriodic(interval, Math.min(interval / 2, 3600_000L));
        else job.setPeriodic(interval);
        try {
            scheduler.schedule(job.build());
        } catch (RuntimeException e) {
            // Too many jobs, or the permission missing: the app's own timer remains.
        }
    }

    // The app's check is over (Platform.cpp).
    static void finished() {
        if (running != null) running.jobFinished(runningParams, false);
        running = null;
        runningParams = null;
    }

    @Override
    public boolean onStartJob(JobParameters params) {
        running = this;
        runningParams = params;
        try {
            if (nativeCheck()) return true;
        } catch (UnsatisfiedLinkError e) {
            // SCANCORE is not running in this process.
        }
        running = null;
        runningParams = null;
        final JobParameters job = params;
        connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {}

            // The checking process ended (it does when done).
            @Override
            public void onServiceDisconnected(ComponentName name) { done(job); }

            @Override
            public void onBindingDied(ComponentName name) { done(job); }

            @Override
            public void onNullBinding(ComponentName name) {}
        };
        try {
            if (bindService(new Intent(this, LibraryCheckService.class), connection, Context.BIND_AUTO_CREATE)) return true;
        } catch (SecurityException e) {
            // Not allowed to start it now; the next period tries again.
        }
        connection = null;
        return false;
    }

    private void done(JobParameters params) {
        if (connection == null) return;
        // Unbound first, so Android does not start the finished check again.
        try {
            unbindService(connection);
        } catch (IllegalArgumentException e) {
            // Already unbound.
        }
        connection = null;
        jobFinished(params, false);
    }

    // Out of time (about ten minutes) or its network gone: the process
    // checking stops with the binding, the next period checks again.
    @Override
    public boolean onStopJob(JobParameters params) {
        if (connection != null) {
            try {
                unbindService(connection);
            } catch (IllegalArgumentException e) {
                // Already unbound.
            }
            connection = null;
        }
        running = null;
        runningParams = null;
        return false;
    }
}
