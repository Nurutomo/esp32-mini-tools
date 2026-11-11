package com.esp32.googlemaps.service

import android.app.Notification
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Typeface
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.text.Spanned
import android.text.style.StyleSpan
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.RemoteViews
import android.widget.TextView
import androidx.core.view.children
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.util.BitmapHelper
import com.esp32.googlemaps.util.DebugLogger
import timber.log.Timber

class GoogleMapsNotificationListener : NotificationListenerService() {

    companion object {
        const val GMAPS_PACKAGE = "com.google.android.apps.maps"
        const val GMAPS_LITE_PACKAGE = "com.google.android.apps.mapslite"
        const val ACTION_NAV_UPDATE = "com.esp32.googlemaps.NAV_UPDATE"
        const val EXTRA_NAV_STATE = "nav_state"

        private var activeInstance: GoogleMapsNotificationListener? = null

        fun isConnected(): Boolean = activeInstance != null

        fun scanActiveNotifications() {
            activeInstance?.checkActiveNotifications()
        }

        private val DISTANCE_REGEX = Regex(
            """^(\d+([.,]\d+)?)\s*(m|km|ft|mi|yd|meter|meters|kilometer|kilometers|feet|miles|mil|kaki)\b""",
            RegexOption.IGNORE_CASE
        )
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        activeInstance = this
        DebugLogger.isListenerConnected = true
        DebugLogger.log("LISTENER", "Connected to Android Notification Manager!", DebugLogger.Level.INFO)
        checkActiveNotifications()
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        activeInstance = null
        DebugLogger.isListenerConnected = false
        DebugLogger.log("LISTENER", "Disconnected from Android Notification Manager!", DebugLogger.Level.WARN)
    }

    fun checkActiveNotifications() {
        try {
            val notifications = activeNotifications
            if (notifications == null || notifications.isEmpty()) {
                DebugLogger.log("LISTENER", "No active notifications in tray.", DebugLogger.Level.DEBUG)
                return
            }
            DebugLogger.log("LISTENER", "Scanning ${notifications.size} active notifications in tray...", DebugLogger.Level.DEBUG)
            for (sbn in notifications) {
                if (isGoogleMapsPackage(sbn.packageName)) {
                    DebugLogger.log("LISTENER", "Found existing Maps notification (id=${sbn.id}, ongoing=${sbn.isOngoing})", DebugLogger.Level.NAV)
                    handleMapsNotification(sbn)
                }
            }
        } catch (e: Exception) {
            DebugLogger.log("LISTENER", "checkActiveNotifications error: ${e.message}", DebugLogger.Level.ERROR)
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null) return

        if (DebugLogger.logAllNotifications) {
            val title = sbn.notification?.extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
            val text = sbn.notification?.extras?.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
            DebugLogger.log("NOTIF_TRAY", "pkg=${sbn.packageName} id=${sbn.id} ongoing=${sbn.isOngoing} title='$title' text='$text'", DebugLogger.Level.DEBUG)
        }

        if (isGoogleMapsPackage(sbn.packageName)) {
            handleMapsNotification(sbn)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null || !isGoogleMapsPackage(sbn.packageName)) return
        DebugLogger.log("MAPS_EVENT", "Google Maps notification removed (id=${sbn.id}). Resetting HUD preview.", DebugLogger.Level.NAV)
        val emptyState = NavigationState(isNavigating = false)
        broadcastNavigation(emptyState)
    }

    private fun isGoogleMapsPackage(pkg: String): Boolean {
        return pkg == GMAPS_PACKAGE || pkg == GMAPS_LITE_PACKAGE || pkg.contains("google.android.apps.maps")
    }

    private fun handleMapsNotification(sbn: StatusBarNotification) {
        val extras = sbn.notification.extras
        val rawTitle = extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim() ?: ""
        val rawText = extras?.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.trim() ?: ""
        val rawSubText = extras?.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()?.trim() ?: ""

        DebugLogger.log(
            "MAPS_RX",
            "Maps notification posted: id=${sbn.id} ongoing=${sbn.isOngoing} title='$rawTitle' text='$rawText' sub='$rawSubText'",
            DebugLogger.Level.NAV
        )

        try {
            val navState = parseNotification(sbn)
            if (navState != null) {
                DebugLogger.log(
                    "MAPS_PARSED",
                    "Success -> Dist='${navState.distanceToTurn}' | Road='${navState.nextRoad}' | Sub='${navState.nextRoadSub}' | ETA='${navState.eta}' | Icon=${navState.currentIconBitmap != null}",
                    DebugLogger.Level.NAV
                )
                broadcastNavigation(navState)
            } else {
                DebugLogger.log(
                    "MAPS_SKIP",
                    "Skipped: Notification lacks navigation data (title='$rawTitle', text='$rawText')",
                    DebugLogger.Level.DEBUG
                )
            }
        } catch (e: Exception) {
            DebugLogger.log("MAPS_ERR", "Error parsing Maps notification: ${e.message}", DebugLogger.Level.ERROR)
            Timber.e("Error parsing Google Maps notification: ${e.message}")
        }
    }

    private fun parseNotification(sbn: StatusBarNotification): NavigationState? {
        val notification = sbn.notification
        val extras = notification.extras
        val appSourceContext = try {
            createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
        } catch (_: Exception) {
            applicationContext
        }

        var nextRoad = ""
        var nextRoadSub = ""
        var distance = ""
        var eta = ""
        var ete = ""
        var totalDist = ""
        var rawBitmap: Bitmap? = null

        // 1. Extract Maneuver Icon (supports Bitmap, Vector, Adaptive, or Layered drawables)
        rawBitmap = extractIcon(notification)

        // 2. Parse from standard Notification Extras
        if (extras != null) {
            val titleSeq = extras.getCharSequence(Notification.EXTRA_TITLE)
                ?: extras.getCharSequence("android.title")
            val titleStr = titleSeq?.toString()?.trim() ?: ""

            val textSeq = extras.getCharSequence(Notification.EXTRA_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
                ?: extras.getCharSequence("android.text")

            val subTextSeq = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_INFO_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_SUMMARY_TEXT)
                ?: extras.getCharSequence("android.subText")

            var parsedRoadFromText = ""
            var parsedSubFromText = ""
            var textStr = ""

            if (textSeq is Spanned) {
                val parsed = parseSpannedText(textSeq)
                parsedRoadFromText = parsed.first
                parsedSubFromText = parsed.second
                textStr = textSeq.toString().trim()
            } else if (textSeq != null) {
                textStr = textSeq.toString().trim()
                parsedRoadFromText = textStr
            }

            // Smart distance detection: Check whether title or text contains the distance to turn
            val titleIsDistance = DISTANCE_REGEX.containsMatchIn(titleStr)
            val textIsDistance = DISTANCE_REGEX.containsMatchIn(textStr)

            if (titleIsDistance && !textIsDistance) {
                distance = titleStr
                nextRoad = parsedRoadFromText
                nextRoadSub = parsedSubFromText
            } else if (textIsDistance && !titleIsDistance) {
                distance = textStr
                nextRoad = titleStr
            } else {
                // Default: title is distance, text is road
                if (titleStr.isNotEmpty()) distance = titleStr
                if (parsedRoadFromText.isNotEmpty()) nextRoad = parsedRoadFromText
                if (parsedSubFromText.isNotEmpty()) nextRoadSub = parsedSubFromText
            }

            // Parse ETA, ETE, and Total Remaining Distance from subText
            if (subTextSeq != null) {
                val subParts = subTextSeq.toString().split(Regex("""[·•|\-]""")).map { it.trim() }.filter { it.isNotEmpty() }
                when (subParts.size) {
                    3 -> {
                        ete = subParts[0]
                        totalDist = subParts[1]
                        eta = cleanEtaString(subParts[2])
                    }
                    2 -> {
                        ete = subParts[0]
                        eta = cleanEtaString(subParts[1])
                    }
                    1 -> {
                        eta = cleanEtaString(subParts[0])
                    }
                }
            }
        }

        // 3. Fallback: Deep RemoteViews inspection if standard fields are incomplete
        if (nextRoad.isEmpty() || distance.isEmpty() || rawBitmap == null) {
            try {
                val remoteViews = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                    try {
                        Notification.Builder.recoverBuilder(this, notification).createBigContentView()
                            ?: Notification.Builder.recoverBuilder(this, notification).createContentView()
                    } catch (_: Exception) {
                        @Suppress("DEPRECATION")
                        notification.bigContentView ?: notification.contentView
                    }
                } else {
                    @Suppress("DEPRECATION")
                    notification.bigContentView ?: notification.contentView
                }

                if (remoteViews != null) {
                    val layoutInflater = appSourceContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE) as LayoutInflater
                    val viewGroup = layoutInflater.inflate(remoteViews.layoutId, null) as? ViewGroup
                    if (viewGroup != null) {
                        remoteViews.reapply(appSourceContext, viewGroup)

                        val directionView = findChildByEntryName(viewGroup, appSourceContext, "text") as? TextView
                        val titleView = findChildByEntryName(viewGroup, appSourceContext, "title") as? TextView
                        val headerView = findChildByEntryName(viewGroup, appSourceContext, "header_text") as? TextView
                        val rightIconView = findChildByEntryName(viewGroup, appSourceContext, "right_icon") as? ImageView

                        if (nextRoad.isEmpty() && directionView != null) {
                            val dText = directionView.text
                            if (dText is Spanned) {
                                val parsed = parseSpannedText(dText)
                                if (parsed.first.isNotEmpty()) nextRoad = parsed.first
                                if (parsed.second.isNotEmpty()) nextRoadSub = parsed.second
                            } else if (!dText.isNullOrEmpty()) {
                                nextRoad = dText.toString().trim()
                            }
                        }

                        if (distance.isEmpty() && titleView != null && !titleView.text.isNullOrEmpty()) {
                            distance = titleView.text.toString().trim()
                        }

                        if (eta.isEmpty() && headerView != null && !headerView.text.isNullOrEmpty()) {
                            val parts = headerView.text.split(Regex("""[·•|\-]""")).map { it.trim() }
                            if (parts.size >= 3) {
                                ete = parts[0]
                                totalDist = parts[1]
                                eta = cleanEtaString(parts[2])
                            }
                        }

                        if (rawBitmap == null && rightIconView?.drawable != null) {
                            rawBitmap = drawableToBitmap(rightIconView.drawable)
                        }
                    }
                }
            } catch (e: Exception) {
                DebugLogger.log("MAPS_REMOTE", "RemoteViews fallback note: ${e.message}", DebugLogger.Level.DEBUG)
            }
        }

        // Accept notification if it has either road or distance or if it is the ongoing navigation notification (id=1)
        if (nextRoad.isEmpty() && distance.isEmpty() && sbn.id != 1) {
            return null
        }

        if (nextRoad.isEmpty() && distance.isNotEmpty()) {
            nextRoad = "Navigating route"
        }

        val (iconBase64, monoBmp) = BitmapHelper.bitmapToBase64(rawBitmap)

        return NavigationState(
            nextRoad = nextRoad,
            nextRoadSub = nextRoadSub,
            distanceToTurn = distance,
            eta = eta,
            ete = ete,
            totalDistance = totalDist,
            iconBase64 = iconBase64,
            isNavigating = true,
            lastUpdatedMs = System.currentTimeMillis()
        ).apply {
            currentIconBitmap = monoBmp ?: rawBitmap
        }
    }

    private fun cleanEtaString(raw: String): String {
        return raw.replace("ETA", "", ignoreCase = true)
            .replace("Tiba", "", ignoreCase = true)
            .replace("Llegada", "", ignoreCase = true)
            .replace("Ankunft", "", ignoreCase = true)
            .trim()
    }

    private fun extractIcon(notification: Notification): Bitmap? {
        // 1. Try getLargeIcon()
        val largeIcon = notification.getLargeIcon()
        if (largeIcon != null) {
            try {
                val drawable = largeIcon.loadDrawable(this)
                if (drawable != null) {
                    val bmp = drawableToBitmap(drawable)
                    if (bmp != null) return bmp
                }
            } catch (_: Exception) {}
        }

        // 2. Try legacy notification.largeIcon
        @Suppress("DEPRECATION")
        if (notification.largeIcon != null) {
            return notification.largeIcon
        }

        // 3. Try getSmallIcon()
        val smallIcon = notification.getSmallIcon()
        if (smallIcon != null) {
            try {
                val drawable = smallIcon.loadDrawable(this)
                if (drawable != null) {
                    val bmp = drawableToBitmap(drawable)
                    if (bmp != null) return bmp
                }
            } catch (_: Exception) {}
        }

        return null
    }

    private fun drawableToBitmap(drawable: Drawable): Bitmap? {
        if (drawable is BitmapDrawable && drawable.bitmap != null) {
            return drawable.bitmap
        }
        return try {
            val width = if (drawable.intrinsicWidth > 0) drawable.intrinsicWidth else 64
            val height = if (drawable.intrinsicHeight > 0) drawable.intrinsicHeight else 64
            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            val canvas = Canvas(bitmap)
            drawable.setBounds(0, 0, canvas.width, canvas.height)
            drawable.draw(canvas)
            bitmap
        } catch (_: Exception) {
            null
        }
    }

    private fun parseSpannedText(spanned: Spanned): Pair<String, String> {
        val spans = spanned.getSpans(0, spanned.length, StyleSpan::class.java)
        var road = ""
        var sub = ""

        if (spans.isNotEmpty()) {
            for (span in spans) {
                val start = spanned.getSpanStart(span)
                val end = spanned.getSpanEnd(span)
                val segment = spanned.substring(start, end).trim()
                if (span.style == Typeface.BOLD) {
                    road = if (road.isEmpty()) segment else "$road $segment"
                } else {
                    sub = if (sub.isEmpty()) segment else "$sub $segment"
                }
            }
        }

        if (road.isEmpty()) {
            road = spanned.toString().trim()
        }

        return Pair(road, sub)
    }

    private fun findChildByEntryName(group: ViewGroup, appCtx: Context, entryName: String): View? {
        for (child in group.children) {
            val name = try {
                if (child.id > 0) appCtx.resources.getResourceEntryName(child.id) else ""
            } catch (_: Exception) { "" }

            if (name == entryName) return child
            if (child is ViewGroup) {
                val found = findChildByEntryName(child, appCtx, entryName)
                if (found != null) return found
            }
        }
        return null
    }

    private fun broadcastNavigation(state: NavigationState) {
        val intent = Intent(ACTION_NAV_UPDATE).apply {
            putExtra(EXTRA_NAV_STATE, state)
        }
        LocalBroadcastManager.getInstance(applicationContext).sendBroadcast(intent)
        NavigationBroadcastService.updateNavigation(applicationContext, state)
    }
}
