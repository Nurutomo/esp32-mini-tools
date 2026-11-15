package com.esp32.googlemaps.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat
import com.esp32.googlemaps.service.NavigationBroadcastService
import com.esp32.googlemaps.util.PermissionHelper
import timber.log.Timber

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        when (intent?.action) {
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_MY_PACKAGE_REPLACED -> try {
                if (!PermissionHelper.hasBluetoothPermissions(context)) return
                ContextCompat.startForegroundService(
                    context,
                    Intent(context, NavigationBroadcastService::class.java).apply {
                        action = NavigationBroadcastService.ACTION_BACKGROUND_START
                    }
                )
            } catch (e: Exception) {
                // Android 12+ may refuse a background FGS start; the user reopening
                // the app path handles it instead.
                Timber.w("Deferred service start: ${e.message}")
            }
        }
    }
}
