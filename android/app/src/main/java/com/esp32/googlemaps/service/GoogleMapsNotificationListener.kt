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
import com.esp32.googlemaps.util.NotificationDumpHelper
import timber.log.Timber
import java.util.Locale

class GoogleMapsNotificationListener : NotificationListenerService() {

    companion object {
        const val GMAPS_PACKAGE = "com.google.android.apps.maps"
        const val GMAPS_LITE_PACKAGE = "com.google.android.apps.mapslite"
        const val GMAPS_GO_PACKAGE = "com.google.android.apps.navlite"
        const val ANDROID_AUTO_PACKAGE = "com.google.android.projection.gearhead"
        const val WAZE_PACKAGE = "com.waze"

        const val ACTION_NAV_UPDATE = "com.esp32.googlemaps.NAV_UPDATE"
        const val EXTRA_NAV_STATE = "nav_state"

        private var activeInstance: GoogleMapsNotificationListener? = null

        fun isConnected(): Boolean = activeInstance != null

        fun getActiveNotificationsList(): Array<StatusBarNotification>? {
            return try {
                activeInstance?.activeNotifications
            } catch (e: Exception) {
                DebugLogger.log("LISTENER", "getActiveNotificationsList error: ${e.message}", DebugLogger.Level.ERROR)
                null
            }
        }

        fun scanActiveNotifications() {
            activeInstance?.checkActiveNotifications()
        }

        fun dumpActiveNotificationsJson(context: Context): String {
            val notifs = getActiveNotificationsList()
            return NotificationDumpHelper.dumpAll(context, notifs)
        }

        fun isNavigationNotification(sbn: StatusBarNotification): Boolean {
            val pkg = sbn.packageName.lowercase(Locale.ROOT)
            if (pkg.contains("com.google.android.apps.maps") ||
                pkg == GMAPS_LITE_PACKAGE ||
                pkg == GMAPS_GO_PACKAGE ||
                pkg == ANDROID_AUTO_PACKAGE ||
                pkg == WAZE_PACKAGE ||
                pkg.contains("maps") ||
                pkg.contains("navigation")
            ) {
                return true
            }

            val category = sbn.notification?.category
            if (category == Notification.CATEGORY_NAVIGATION || category == Notification.CATEGORY_TRANSPORT) {
                return true
            }

            return false
        }

        fun broadcastNavigationState(context: Context, state: NavigationState) {
            val intent = Intent(ACTION_NAV_UPDATE).apply {
                putExtra(EXTRA_NAV_STATE, state)
            }
            LocalBroadcastManager.getInstance(context.applicationContext).sendBroadcast(intent)
            NavigationBroadcastService.updateNavigation(context.applicationContext, state)
        }

        // Comprehensive distance regex supporting optional language prefixes ("In 500 m", "Dalam 200 m", "En 1 km")
        private val DISTANCE_EXTRACTION_REGEX = Regex(
            """(?:\b(?:in|dalam|en|nach|dans|tra|over|a|à|em|ke)\s+)?(\d+([.,]\d+)?\s*(?:m|km|ft|feet|mi|miles?|yd|yards?|meter|meters|kilometer|kilometers|mil|kaki))\b""",
            RegexOption.IGNORE_CASE
        )

        private val CLOCK_TIME_REGEX = Regex(
            """\b(\d{1,2}[:.]\d{2}\s*(?:AM|PM|am|pm)?|\d{1,2}\s*h\s*\d{2})\b"""
        )

        private val DURATION_REGEX = Regex(
            """\b(\d+\s*(?:min|mins|mnt|menit|hr|hrs|jam|h|m))\b""",
            RegexOption.IGNORE_CASE
        )

        private val TOTAL_DIST_REGEX = Regex(
            """\b(\d+([.,]\d+)?\s*(?:km|mi|miles?))\b""",
            RegexOption.IGNORE_CASE
        )

        fun tryParseNotification(sbn: StatusBarNotification, context: Context): NavigationState? {
            val listener = activeInstance
            return if (listener != null) {
                listener.parseNotificationInternal(sbn)
            } else {
                parseNotificationStatic(sbn, context)
            }
        }

        private fun parseNotificationStatic(sbn: StatusBarNotification, context: Context): NavigationState? {
            // Standalone parser using context when listener instance is not yet bound
            return try {
                val notification = sbn.notification ?: return null
                val extras = notification.extras
                val appCtx = try {
                    context.createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
                } catch (_: Exception) {
                    context
                }

                val titleStr = extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim() ?: ""
                val textStr = extras?.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.trim() ?: ""
                val subTextStr = extras?.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()?.trim() ?: ""

                extractNavData(sbn, titleStr, textStr, subTextStr, "", null, appCtx)
            } catch (e: Exception) {
                Timber.e("parseNotificationStatic error: ${e.message}")
                null
            }
        }

        private fun extractNavData(
            sbn: StatusBarNotification,
            rawTitle: String,
            rawText: String,
            rawSubText: String,
            rawBigText: String,
            rawBitmap: Bitmap?,
            appSourceContext: Context
        ): NavigationState? {
            var distance = ""
            var nextRoad = ""
            var nextRoadSub = ""
            var eta = ""
            var ete = ""
            var totalDist = ""

            // Combine fields to search for distance
            val titleMatch = DISTANCE_EXTRACTION_REGEX.find(rawTitle)
            val textMatch = DISTANCE_EXTRACTION_REGEX.find(rawText)
            val bigTextMatch = if (rawBigText.isNotEmpty()) DISTANCE_EXTRACTION_REGEX.find(rawBigText) else null

            when {
                titleMatch != null -> {
                    distance = titleMatch.groupValues[1].trim()
                    // If title had extra text after removing distance, use it
                    val remainingTitle = rawTitle.replace(titleMatch.value, "").replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "").trim()
                    if (remainingTitle.isNotEmpty()) {
                        nextRoad = remainingTitle
                        if (rawText.isNotEmpty()) nextRoadSub = rawText
                    } else {
                        nextRoad = rawText
                    }
                }
                textMatch != null -> {
                    distance = textMatch.groupValues[1].trim()
                    val remainingText = rawText.replace(textMatch.value, "").replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "").trim()
                    nextRoad = if (remainingText.isNotEmpty()) remainingText else rawTitle
                    if (rawTitle.isNotEmpty() && nextRoad != rawTitle) {
                        nextRoadSub = rawTitle
                    }
                }
                bigTextMatch != null -> {
                    distance = bigTextMatch.groupValues[1].trim()
                    val remainingBig = rawBigText.replace(bigTextMatch.value, "").replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "").trim()
                    nextRoad = if (remainingBig.isNotEmpty()) remainingBig else rawTitle
                    if (rawText.isNotEmpty()) nextRoadSub = rawText
                }
                else -> {
                    // No distance found, treat as route / navigation status
                    if (rawTitle.isNotEmpty() && rawText.isNotEmpty()) {
                        nextRoad = rawTitle
                        nextRoadSub = rawText
                    } else if (rawTitle.isNotEmpty()) {
                        nextRoad = rawTitle
                    } else if (rawText.isNotEmpty()) {
                        nextRoad = rawText
                    }
                }
            }

            // Parse ETA / ETE / Remaining total distance from subText or text
            val candidateEtaSources = listOf(rawSubText, rawText, rawBigText).filter { it.isNotEmpty() }
            for (source in candidateEtaSources) {
                if (eta.isEmpty()) {
                    val timeMatch = CLOCK_TIME_REGEX.find(source)
                    if (timeMatch != null) eta = cleanEtaString(timeMatch.value)
                }
                if (ete.isEmpty()) {
                    val durMatch = DURATION_REGEX.find(source)
                    if (durMatch != null) ete = durMatch.value.trim()
                }
                if (totalDist.isEmpty()) {
                    val distMatch = TOTAL_DIST_REGEX.find(source)
                    if (distMatch != null && distMatch.value.trim() != distance) {
                        totalDist = distMatch.value.trim()
                    }
                }
            }

            // Subtext bullet splitter fallback (e.g. "15 min · 4.2 km · 17:30")
            if (rawSubText.isNotEmpty() && (eta.isEmpty() || ete.isEmpty())) {
                val parts = rawSubText.split(Regex("""[·•|\-]""")).map { it.trim() }.filter { it.isNotEmpty() }
                when (parts.size) {
                    3 -> {
                        if (ete.isEmpty()) ete = parts[0]
                        if (totalDist.isEmpty()) totalDist = parts[1]
                        if (eta.isEmpty()) eta = cleanEtaString(parts[2])
                    }
                    2 -> {
                        if (ete.isEmpty()) ete = parts[0]
                        if (eta.isEmpty()) eta = cleanEtaString(parts[1])
                    }
                    1 -> {
                        if (eta.isEmpty()) eta = cleanEtaString(parts[0])
                    }
                }
            }

            // Fallback for ongoing navigation notifications without clear distance
            if (nextRoad.isEmpty() && distance.isEmpty()) {
                if (sbn.isOngoing || sbn.id == 1) {
                    nextRoad = if (rawTitle.isNotEmpty()) rawTitle else "Navigating route"
                } else {
                    return null
                }
            }

            // Maneuver icon fallback: if no bitmap from notification, synthesize arrow from text
            var finalBitmap = rawBitmap
            if (finalBitmap == null) {
                val combinedText = "$rawTitle $rawText $rawBigText".lowercase(Locale.ROOT)
                finalBitmap = when {
                    combinedText.contains("roundabout") || combinedText.contains("bundaran") || combinedText.contains("rond-point") || combinedText.contains("circle") -> {
                        BitmapHelper.createRoundaboutBitmap()
                    }
                    combinedText.contains("left") || combinedText.contains("kiri") || combinedText.contains("gauche") || combinedText.contains("links") || combinedText.contains("izquierda") -> {
                        BitmapHelper.createTurnLeftBitmap()
                    }
                    combinedText.contains("right") || combinedText.contains("kanan") || combinedText.contains("droite") || combinedText.contains("rechts") || combinedText.contains("derecha") -> {
                        BitmapHelper.createTurnRightBitmap()
                    }
                    combinedText.contains("straight") || combinedText.contains("lurus") || combinedText.contains("terus") || combinedText.contains("ahead") || combinedText.contains("tout droit") -> {
                        BitmapHelper.createStraightBitmap()
                    }
                    combinedText.contains("u-turn") || combinedText.contains("putar balik") -> {
                        BitmapHelper.createTurnLeftBitmap()
                    }
                    else -> null
                }
            }

            val (iconBase64, monoBmp) = BitmapHelper.bitmapToBase64(finalBitmap)

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
                currentIconBitmap = monoBmp ?: finalBitmap
            }
        }

        private fun cleanEtaString(raw: String): String {
            return raw.replace(Regex("""(?i)\b(eta|tiba|llegada|ankunft)\b"""), "")
                .replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "")
                .trim()
        }
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
                DebugLogger.log("LISTENER", "Tray scan: 0 notifications in active list.", DebugLogger.Level.DEBUG)
                return
            }
            DebugLogger.log("LISTENER", "Scanning ${notifications.size} active notifications in tray...", DebugLogger.Level.DEBUG)
            var foundNav = false
            for (sbn in notifications) {
                if (isNavigationNotification(sbn)) {
                    foundNav = true
                    DebugLogger.log("LISTENER", "Found active navigation notif (pkg=${sbn.packageName}, id=${sbn.id}, ongoing=${sbn.isOngoing})", DebugLogger.Level.NAV)
                    handleMapsNotification(sbn)
                }
            }
            if (!foundNav) {
                DebugLogger.log("LISTENER", "Scan complete: No active navigation notifications found in tray.", DebugLogger.Level.INFO)
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

        if (isNavigationNotification(sbn)) {
            handleMapsNotification(sbn)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null || !isNavigationNotification(sbn)) return
        DebugLogger.log("MAPS_EVENT", "Navigation notification removed (pkg=${sbn.packageName}, id=${sbn.id}). Resetting HUD.", DebugLogger.Level.NAV)
        val emptyState = NavigationState(isNavigating = false)
        broadcastNavigationState(applicationContext, emptyState)
    }

    private fun handleMapsNotification(sbn: StatusBarNotification) {
        val extras = sbn.notification?.extras
        val rawTitle = extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim() ?: ""
        val rawText = extras?.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.trim() ?: ""
        val rawSubText = extras?.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()?.trim() ?: ""

        DebugLogger.log(
            "MAPS_RX",
            "Maps notif (pkg=${sbn.packageName}, id=${sbn.id}, ongoing=${sbn.isOngoing}): title='$rawTitle' text='$rawText' sub='$rawSubText'",
            DebugLogger.Level.NAV
        )

        try {
            val navState = parseNotificationInternal(sbn)
            if (navState != null) {
                DebugLogger.log(
                    "MAPS_PARSED",
                    "Parsed OK -> Dist='${navState.distanceToTurn}' | Road='${navState.nextRoad}' | Sub='${navState.nextRoadSub}' | ETA='${navState.eta}' | Icon=${navState.currentIconBitmap != null}",
                    DebugLogger.Level.NAV
                )
                broadcastNavigationState(applicationContext, navState)
            } else {
                DebugLogger.log(
                    "MAPS_SKIP",
                    "Skipped: Notification lacks navigation content (title='$rawTitle', text='$rawText')",
                    DebugLogger.Level.DEBUG
                )
            }
        } catch (e: Exception) {
            DebugLogger.log("MAPS_ERR", "Error parsing navigation notification: ${e.message}", DebugLogger.Level.ERROR)
            Timber.e("Error parsing Google Maps notification: ${e.message}")
        }
    }

    private fun parseNotificationInternal(sbn: StatusBarNotification): NavigationState? {
        val notification = sbn.notification ?: return null
        val extras = notification.extras
        val appSourceContext = try {
            createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
        } catch (_: Exception) {
            applicationContext
        }

        // 1. Extract Maneuver Icon
        var rawBitmap: Bitmap? = extractIcon(notification, appSourceContext)

        // 2. Extract standard and extended notification extras
        var titleStr = ""
        var textStr = ""
        var subTextStr = ""
        var bigTextStr = ""

        if (extras != null) {
            val titleSeq = extras.getCharSequence(Notification.EXTRA_TITLE)
                ?: extras.getCharSequence(Notification.EXTRA_TITLE_BIG)
                ?: extras.getCharSequence("android.title")
            titleStr = titleSeq?.toString()?.trim() ?: ""

            val textSeq = extras.getCharSequence(Notification.EXTRA_TEXT)
                ?: extras.getCharSequence("android.text")

            if (textSeq is Spanned) {
                val parsed = parseSpannedText(textSeq)
                textStr = if (parsed.second.isNotEmpty()) "${parsed.first} ${parsed.second}".trim() else parsed.first
            } else if (textSeq != null) {
                textStr = textSeq.toString().trim()
            }

            val bigTextSeq = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
            if (bigTextSeq != null) {
                bigTextStr = bigTextSeq.toString().trim()
            }

            // Fallback to textLines if text is empty
            if (textStr.isEmpty()) {
                val lines = extras.getCharSequenceArray("android.textLines")
                if (lines != null && lines.isNotEmpty()) {
                    textStr = lines.joinToString(" - ") { it.toString().trim() }
                }
            }

            val subSeq = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_INFO_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_SUMMARY_TEXT)
                ?: extras.getCharSequence("android.subText")
            subTextStr = subSeq?.toString()?.trim() ?: ""
        }

        // 3. Fallback: Deep RemoteViews inspection if standard fields are incomplete
        if (titleStr.isEmpty() || textStr.isEmpty() || rawBitmap == null) {
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

                        if (textStr.isEmpty() && directionView != null && !directionView.text.isNullOrEmpty()) {
                            textStr = directionView.text.toString().trim()
                        }
                        if (titleStr.isEmpty() && titleView != null && !titleView.text.isNullOrEmpty()) {
                            titleStr = titleView.text.toString().trim()
                        }
                        if (subTextStr.isEmpty() && headerView != null && !headerView.text.isNullOrEmpty()) {
                            subTextStr = headerView.text.toString().trim()
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

        return extractNavData(sbn, titleStr, textStr, subTextStr, bigTextStr, rawBitmap, appSourceContext)
    }

    private fun extractIcon(notification: Notification, appSourceContext: Context): Bitmap? {
        // 1. Try getLargeIcon() with fallback between local and source context
        val largeIcon = notification.getLargeIcon()
        if (largeIcon != null) {
            try {
                val drawable = largeIcon.loadDrawable(this) ?: largeIcon.loadDrawable(appSourceContext)
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

        // 3. Try Bitmap in extras
        val extras = notification.extras
        if (extras != null) {
            val bmpExtra = extras.getParcelable<Bitmap>(Notification.EXTRA_LARGE_ICON)
                ?: extras.getParcelable<Bitmap>("android.largeIcon.big")
                ?: extras.getParcelable<Bitmap>("android.picture")
            if (bmpExtra != null) return bmpExtra
        }

        // 4. Try getSmallIcon()
        val smallIcon = notification.getSmallIcon()
        if (smallIcon != null) {
            try {
                val drawable = smallIcon.loadDrawable(this) ?: smallIcon.loadDrawable(appSourceContext)
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
}
