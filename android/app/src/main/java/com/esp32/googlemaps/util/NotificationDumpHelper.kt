package com.esp32.googlemaps.util

import android.app.Notification
import android.content.Context
import android.graphics.Bitmap
import android.os.Build
import android.os.Bundle
import android.service.notification.StatusBarNotification
import com.esp32.googlemaps.service.GoogleMapsNotificationListener
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object NotificationDumpHelper {

    fun dumpAll(context: Context, notifications: Array<StatusBarNotification>?): String {
        val root = JSONObject()
        val timeFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

        root.put("timestamp", System.currentTimeMillis())
        root.put("timeFormatted", timeFormat.format(Date()))
        root.put("isListenerConnected", GoogleMapsNotificationListener.isConnected())
        root.put("isNotificationAccessGranted", PermissionHelper.isNotificationServiceEnabled(context))

        if (notifications == null) {
            root.put("warning", "No active notifications returned. If notifications are present in the status bar, ensure Notification Access permission is enabled for this app in Android Settings.")
        }

        val array = JSONArray()
        if (notifications != null) {
            for (sbn in notifications) {
                try {
                    array.put(dumpNotification(sbn, context))
                } catch (t: Throwable) {
                    val errObj = JSONObject()
                    errObj.put("error", "Failed to dump notification pkg=${sbn.packageName} id=${sbn.id}: ${t.message}")
                    array.put(errObj)
                }
            }
        }

        root.put("notificationCount", notifications?.size ?: 0)
        root.put("notifications", array)

        return root.toString(2)
    }

    fun dumpNotification(sbn: StatusBarNotification, context: Context): JSONObject {
        val obj = JSONObject()
        obj.put("id", sbn.id)
        obj.put("tag", sbn.tag ?: JSONObject.NULL)
        obj.put("key", sbn.key)
        obj.put("packageName", sbn.packageName)
        obj.put("postTime", sbn.postTime)
        obj.put("isOngoing", sbn.isOngoing)
        obj.put("isClearable", sbn.isClearable)

        val n = sbn.notification
        if (n == null) {
            obj.put("notification", JSONObject.NULL)
            return obj
        }

        obj.put("category", n.category ?: JSONObject.NULL)
        obj.put("flags", n.flags)
        obj.put("flagsDescription", decodeFlags(n.flags))

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            obj.put("channelId", n.channelId ?: JSONObject.NULL)
        }

        if (n.tickerText != null) {
            obj.put("tickerText", n.tickerText.toString())
        }

        val isNavApp = GoogleMapsNotificationListener.isNavigationNotification(sbn)
        obj.put("isNavigationApp", isNavApp)

        // Extras serialization
        val extrasObj = JSONObject()
        val extras = n.extras
        if (extras != null) {
            for (key in extras.keySet()) {
                try {
                    val value = extras.get(key)
                    extrasObj.put(key, formatExtraValue(value, 0))
                } catch (e: Throwable) {
                    extrasObj.put(key, "[Error reading extra: ${e.message}]")
                }
            }
        }
        obj.put("extras", extrasObj)

        // Actions
        val actionsArray = JSONArray()
        n.actions?.forEach { action ->
            val actObj = JSONObject()
            actObj.put("title", action.title?.toString() ?: "")
            actionsArray.put(actObj)
        }
        obj.put("actions", actionsArray)

        // RemoteViews extraction inspection
        try {
            val appSourceContext = try {
                context.createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
            } catch (_: Exception) {
                context
            }
            val rvData = RemoteViewsExtractor.extract(n, context, appSourceContext)
            val rvObj = JSONObject()
            val rvTextsArray = JSONArray()
            rvData.texts.forEach { rvTextsArray.put(it) }
            rvObj.put("extractedTexts", rvTextsArray)
            rvObj.put("extractedBitmapsCount", rvData.bitmaps.size)
            val bitmapCandidates = JSONArray()
            rvData.bitmaps.forEachIndexed { index, bitmap ->
                bitmapCandidates.put(JSONObject().apply {
                    put("index", index + 1)
                    put("source", rvData.bitmapSources.getOrElse(index) { "RemoteViews bitmap" })
                    put("width", bitmap.width)
                    put("height", bitmap.height)
                })
            }
            rvObj.put("bitmapCandidates", bitmapCandidates)

            val debugActionsArray = JSONArray()
            rvData.actionsDebug.take(35).forEach { debugActionsArray.put(it) }
            if (rvData.actionsDebug.size > 35) {
                debugActionsArray.put("... (${rvData.actionsDebug.size - 35} more actions)")
            }
            rvObj.put("actionsDebug", debugActionsArray)
            obj.put("remoteViews", rvObj)
        } catch (t: Throwable) {
            obj.put("remoteViewsError", t.message ?: "Unknown extraction error")
        }

        // Navigation parse analysis
        if (isNavApp) {
            val navAnalysis = JSONObject()
            try {
                val parsed = GoogleMapsNotificationListener.tryParseNotification(sbn, context)
                if (parsed != null) {
                    navAnalysis.put("parseSuccess", true)
                    navAnalysis.put("distanceToTurn", parsed.distanceToTurn)
                    navAnalysis.put("nextRoad", parsed.nextRoad)
                    navAnalysis.put("nextRoadSub", parsed.nextRoadSub)
                    navAnalysis.put("eta", parsed.eta)
                    navAnalysis.put("ete", parsed.ete)
                    navAnalysis.put("totalDistance", parsed.totalDistance)
                    navAnalysis.put("hasIcon", parsed.currentIconBitmap != null || parsed.iconBase64.isNotEmpty())
                    navAnalysis.put("iconBase64Length", parsed.iconBase64.length)
                } else {
                    navAnalysis.put("parseSuccess", false)
                    navAnalysis.put("reason", "Parser returned null (no distance or road text found in notification)")
                }
            } catch (e: Throwable) {
                navAnalysis.put("parseSuccess", false)
                navAnalysis.put("error", e.message ?: "Unknown parse error")
            }
            obj.put("navigationAnalysis", navAnalysis)
        }

        return obj
    }

    private fun decodeFlags(flags: Int): String {
        val list = mutableListOf<String>()
        if (flags and Notification.FLAG_ONGOING_EVENT != 0) list.add("FLAG_ONGOING_EVENT")
        if (flags and Notification.FLAG_NO_CLEAR != 0) list.add("FLAG_NO_CLEAR")
        if (flags and Notification.FLAG_FOREGROUND_SERVICE != 0) list.add("FLAG_FOREGROUND_SERVICE")
        if (flags and Notification.FLAG_LOCAL_ONLY != 0) list.add("FLAG_LOCAL_ONLY")
        if (flags and Notification.FLAG_AUTO_CANCEL != 0) list.add("FLAG_AUTO_CANCEL")
        return list.joinToString("|")
    }

    private fun formatExtraValue(value: Any?, depth: Int = 0): Any {
        if (value == null) return JSONObject.NULL
        if (depth > 4) return "[Max Depth Reached]"
        return try {
            when (value) {
                is String -> value
                is Number -> value
                is Boolean -> value
                is CharSequence -> value.toString()
                is Array<*> -> {
                    val arr = JSONArray()
                    value.take(15).forEach { item -> arr.put(formatExtraValue(item, depth + 1)) }
                    if (value.size > 15) arr.put("... (${value.size - 15} more items)")
                    arr
                }
                is Collection<*> -> {
                    val arr = JSONArray()
                    value.take(15).forEach { item -> arr.put(formatExtraValue(item, depth + 1)) }
                    if (value.size > 15) arr.put("... (${value.size - 15} more items)")
                    arr
                }
                is Bundle -> {
                    val bObj = JSONObject()
                    for (k in value.keySet()) {
                        bObj.put(k, formatExtraValue(value.get(k), depth + 1))
                    }
                    bObj
                }
                is Bitmap -> "[Bitmap: ${value.width}x${value.height}]"
                is android.graphics.drawable.Icon -> "[Icon: $value]"
                else -> value.toString().take(200)
            }
        } catch (t: Throwable) {
            "[Error reading value: ${t.message}]"
        }
    }
}
