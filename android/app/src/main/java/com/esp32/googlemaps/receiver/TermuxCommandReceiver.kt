package com.esp32.googlemaps.receiver

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Environment
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.service.GoogleMapsNotificationListener
import com.esp32.googlemaps.util.BitmapHelper
import com.esp32.googlemaps.util.DebugLogger
import com.esp32.googlemaps.util.PermissionHelper
import timber.log.Timber
import java.io.File
import java.util.Locale

/**
 * BroadcastReceiver callable directly from ADB / Termux via:
 *
 * 1. Dump all active notifications in JSON format:
 *    am broadcast -a com.esp32.googlemaps.DUMP_NOTIFICATIONS
 *    am broadcast -a com.esp32.googlemaps.DUMP_NOTIFICATIONS --es file /sdcard/Download/my_dump.json
 *
 * 2. Force rebind NotificationListenerService:
 *    am broadcast -a com.esp32.googlemaps.REBIND_LISTENER
 *
 * 3. Scan tray for active navigation:
 *    am broadcast -a com.esp32.googlemaps.SCAN_TRAY
 *
 * 4. Send synthetic turn-by-turn navigation data to ESP32 OLED:
 *    am broadcast -a com.esp32.googlemaps.SIMULATE_NAV --es turn left --es dist "250 m" --es road "Main St"
 */
class TermuxCommandReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent?) {
        val action = intent?.action ?: return
        DebugLogger.log("TERMUX_CMD", "Received action: $action", DebugLogger.Level.INFO)

        when (action) {
            "com.esp32.googlemaps.DUMP_NOTIFICATIONS" -> {
                handleDumpNotifications(context, intent)
            }
            "com.esp32.googlemaps.REBIND_LISTENER" -> {
                PermissionHelper.rebindNotificationListener(context)
                val msg = "NotificationListener rebind requested and component state cycled."
                DebugLogger.log("TERMUX_CMD", msg, DebugLogger.Level.INFO)
                resultCode = Activity.RESULT_OK
                resultData = msg
            }
            "com.esp32.googlemaps.SCAN_TRAY" -> {
                GoogleMapsNotificationListener.scanActiveNotifications()
                val msg = "Scanned active notification tray."
                DebugLogger.log("TERMUX_CMD", msg, DebugLogger.Level.INFO)
                resultCode = Activity.RESULT_OK
                resultData = msg
            }
            "com.esp32.googlemaps.SIMULATE_NAV" -> {
                handleSimulateNav(context, intent)
            }
        }
    }

    private fun handleDumpNotifications(context: Context, intent: Intent) {
        try {
            val json = GoogleMapsNotificationListener.dumpActiveNotificationsJson(context)

            // Determine destination file
            val customPath = intent.getStringExtra("file")
            val targetFile = if (!customPath.isNullOrBlank()) {
                File(customPath)
            } else {
                val downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                if (!downloadDir.exists()) downloadDir.mkdirs()
                File(downloadDir, "notifications_dump.json")
            }

            var savedPath = ""
            try {
                targetFile.writeText(json)
                savedPath = targetFile.absolutePath
            } catch (_: Exception) {
                // Fallback to app-specific external files dir
                val fallbackFile = File(context.getExternalFilesDir(null), "notifications_dump.json")
                fallbackFile.writeText(json)
                savedPath = fallbackFile.absolutePath
            }

            Timber.tag("ESP32_NOTIF_DUMP").i(json)
            DebugLogger.log("TERMUX_CMD", "Dumped notifications to: $savedPath", DebugLogger.Level.NAV)

            resultCode = Activity.RESULT_OK
            // Return file path and JSON so Termux captures output
            resultData = "Notifications JSON ($savedPath):\n$json"
        } catch (e: Exception) {
            val err = "Error dumping notifications: ${e.message}"
            DebugLogger.log("TERMUX_CMD", err, DebugLogger.Level.ERROR)
            resultCode = Activity.RESULT_CANCELED
            resultData = err
        }
    }

    private fun handleSimulateNav(context: Context, intent: Intent) {
        val road = intent.getStringExtra("road") ?: "Main Street"
        val roadSub = intent.getStringExtra("road_sub") ?: intent.getStringExtra("sub") ?: "Toward Downtown"
        val dist = intent.getStringExtra("dist") ?: "200 m"
        val turn = (intent.getStringExtra("turn") ?: "left").lowercase(Locale.ROOT)
        val eta = intent.getStringExtra("eta") ?: "18:00"
        val ete = intent.getStringExtra("ete") ?: "10 min"
        val totalDist = intent.getStringExtra("total_dist") ?: "4.5 km"

        val bmp = when {
            turn.contains("right") -> BitmapHelper.createTurnRightBitmap()
            turn.contains("straight") -> BitmapHelper.createStraightBitmap()
            turn.contains("roundabout") -> BitmapHelper.createRoundaboutBitmap()
            else -> BitmapHelper.createTurnLeftBitmap()
        }

        val (iconBase64, monoBmp) = BitmapHelper.bitmapToBase64(bmp)

        val state = NavigationState(
            nextRoad = road,
            nextRoadSub = roadSub,
            distanceToTurn = dist,
            eta = eta,
            ete = ete,
            totalDistance = totalDist,
            iconBase64 = iconBase64,
            isNavigating = true,
            lastUpdatedMs = System.currentTimeMillis()
        ).apply {
            currentIconBitmap = monoBmp ?: bmp
        }

        GoogleMapsNotificationListener.broadcastNavigationState(context, state)

        val msg = "Simulated navigation sent: [$turn] $dist to $road ($eta)"
        DebugLogger.log("TERMUX_CMD", msg, DebugLogger.Level.NAV)
        resultCode = Activity.RESULT_OK
        resultData = msg
    }
}
