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
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import com.esp32.googlemaps.model.NavigationState
import com.esp32.googlemaps.util.BitmapHelper
import com.esp32.googlemaps.util.DebugLogger
import com.esp32.googlemaps.util.NotificationDumpHelper
import com.esp32.googlemaps.util.RemoteViewsExtractor
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
        @Volatile private var preferredRemoteViewsBitmapIndex: Int? = null

        fun setPreferredRemoteViewsBitmapIndex(index: Int?) {
            preferredRemoteViewsBitmapIndex = index?.coerceIn(1, 3)
        }

        fun preferredRemoteViewsBitmapIndex(): Int? = preferredRemoteViewsBitmapIndex

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

            // Critical: NEVER intercept our own companion app's notifications (prevents infinite feedback loop)
            if (pkg.startsWith("com.esp32.") || pkg == "com.esp32.googlemaps") {
                return false
            }

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

        // Distance extraction regex supporting languages and optional prefixes ("In 500 m", "Dalam 200 m", "En 1 km")
        val DISTANCE_EXTRACTION_REGEX = Regex(
            """(?:\b(?:in|dalam|en|nach|dans|tra|over|a|à|em|ke)\s+)?(\d+([.,]\d+)?\s*(?:m|km|ft|feet|mi|miles?|yd|yards?|meter|meters|kilometer|kilometers|mil|kaki))\b""",
            RegexOption.IGNORE_CASE
        )

        val CLOCK_TIME_REGEX = Regex(
            """\b(\d{1,2}[:.]\d{2}\s*(?:AM|PM|am|pm)?|\d{1,2}\s*h\s*\d{2})\b"""
        )

        val DURATION_REGEX = Regex(
            """\b(\d+\s*(?:hr|hrs|jam|h)(?:\s*\d+(?:\s*(?:min|mins|mnt|menit|m))?)?|\d+\s*(?:min|mins|mnt|menit|m))\b""",
            RegexOption.IGNORE_CASE
        )

        val TOTAL_DIST_REGEX = Regex(
            """\b(\d+([.,]\d+)?\s*(?:km|mi|miles?))\b""",
            RegexOption.IGNORE_CASE
        )

        private val TOWARD_PREFIX_REGEX = Regex(
            """^(?:toward|towards|ke arah|menuju|vers|en direction de|richtung|in richtung|hacia|verso)\s+(.+)$""",
            RegexOption.IGNORE_CASE
        )

        private val MANEUVER_SPLIT_REGEX = Regex(
            """^(turn left|turn right|slight left|slight right|sharp left|sharp right|keep left|keep right|make a u-turn|u-turn|take exit \S+|belok kiri|belok kanan|putar balik)\s+(?:on|onto|to|toward|towards|ke|ke arah|di)\s+(.+)$""",
            RegexOption.IGNORE_CASE
        )

        private val ACTION_BUTTON_REGEX = Regex(
            """^(?:exit navigation|exit|keluar navigasi|keluar|keluar dari navigasi|resume|lanjutkan|re-center|recenter|pusatkan kembali|mute|unmute|senyapkan|batal senyap|suara|stop|berhenti|share trip|bagikan perjalanan|feedback|masukan|overview|tinjauan|leave|quitter|reprendre|recentrer)$""",
            RegexOption.IGNORE_CASE
        )

        private val COORDINATE_DECORATION_REGEX = Regex(
            """(?i)\s*[-–—]?\s*\d{1,2}[:.]\d{2}\s*(?:ETA|Tiba)?\s*$"""
        )

        private val COORDINATE_REGEX = Regex(
            """\b\d{1,2}°\d{1,2}'[\d.]+"?[NSEW]\b""",
            RegexOption.IGNORE_CASE
        )

        fun isSummaryLine(trimmed: String): Boolean {
            if (trimmed.isEmpty()) return false
            val hasBullet = trimmed.contains("·") || trimmed.contains("•") || trimmed.contains("|")
            val hasDuration = DURATION_REGEX.containsMatchIn(trimmed)
            val hasClockTime = CLOCK_TIME_REGEX.containsMatchIn(trimmed)
            val hasTotalDist = TOTAL_DIST_REGEX.containsMatchIn(trimmed)
            val hasEtaWord = trimmed.contains(Regex("""(?i)\b(eta|tiba|arrival|ankunft|llegada)\b"""))

            if (hasBullet && (hasDuration || hasClockTime || hasTotalDist || hasEtaWord)) {
                return true
            }
            if (hasDuration && (hasTotalDist || hasClockTime || hasEtaWord)) {
                return true
            }
            if ((hasClockTime || hasDuration || hasTotalDist) && hasEtaWord) {
                return true
            }
            return false
        }

        fun tryParseNotification(sbn: StatusBarNotification, context: Context): NavigationState? {
            val listener = activeInstance
            return if (listener != null) {
                listener.parseNotificationInternal(sbn)
            } else {
                parseNotificationStatic(sbn, context)
            }
        }

        private fun parseNotificationStatic(sbn: StatusBarNotification, context: Context): NavigationState? {
            return try {
                val notification = sbn.notification ?: return null
                val appCtx = try {
                    context.createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
                } catch (_: Exception) {
                    context
                }

                val rvData = RemoteViewsExtractor.extract(notification, context, appCtx)
                val standardTexts = extractStandardExtrasTexts(notification)
                val allCandidateTexts = (standardTexts + rvData.texts).filter { it.isNotBlank() }.distinct()

                extractNavData(
                    sbn,
                    allCandidateTexts,
                    rvData.bitmaps,
                    notification,
                    context,
                    appCtx,
                    preferredRemoteViewsBitmapIndex
                )
            } catch (e: Exception) {
                Timber.e("parseNotificationStatic error: ${e.message}")
                null
            }
        }

        fun extractStandardExtrasTexts(notification: Notification): List<String> {
            val list = mutableListOf<String>()
            val extras = notification.extras ?: return list

            val titleSeq = extras.getCharSequence(Notification.EXTRA_TITLE)
                ?: extras.getCharSequence(Notification.EXTRA_TITLE_BIG)
                ?: extras.getCharSequence("android.title")
            if (!titleSeq.isNullOrBlank()) list.add(titleSeq.toString().trim())

            val textSeq = extras.getCharSequence(Notification.EXTRA_TEXT)
                ?: extras.getCharSequence("android.text")
            if (!textSeq.isNullOrBlank()) list.add(textSeq.toString().trim())

            val bigTextSeq = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
            if (!bigTextSeq.isNullOrBlank()) list.add(bigTextSeq.toString().trim())

            val subSeq = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_INFO_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_SUMMARY_TEXT)
                ?: extras.getCharSequence("android.subText")
            if (!subSeq.isNullOrBlank()) list.add(subSeq.toString().trim())

            val lines = extras.getCharSequenceArray("android.textLines")
            if (lines != null) {
                for (line in lines) {
                    if (!line.isNullOrBlank()) list.add(line.toString().trim())
                }
            }

            return list
        }

        fun extractNavData(
            sbn: StatusBarNotification,
            candidateTexts: List<String>,
            candidateBitmaps: List<Bitmap>,
            notification: Notification,
            context: Context,
            appSourceContext: Context,
            selectedBitmapIndex: Int? = null
        ): NavigationState? {
            var distance = ""
            var nextRoad = ""
            var nextRoadSub = ""
            var eta = ""
            var ete = ""
            var totalDist = ""

            // 1. Identify trip summary lines and extract ETA, ETE, and Total Distance
            val nonSummaryTexts = mutableListOf<String>()
            for (text in candidateTexts) {
                val trimmed = text.trim()
                if (trimmed.isEmpty()) continue

                // Ignore action buttons (e.g. "Exit navigation", "Resume")
                if (ACTION_BUTTON_REGEX.matches(trimmed)) continue

                // Check if coordinate string (e.g. "7°33'54.8\"S 112°32'10.4\"E - 17:32 ETA")
                if (COORDINATE_REGEX.containsMatchIn(trimmed) || (trimmed.contains("°") && trimmed.contains("'"))) {
                    if (eta.isEmpty()) {
                        val timeMatch = CLOCK_TIME_REGEX.find(trimmed)
                        if (timeMatch != null) eta = cleanEtaString(timeMatch.value)
                    }
                    val withoutCoordinates = COORDINATE_REGEX.replace(trimmed, " ")
                    val nonCoordinatePart = COORDINATE_DECORATION_REGEX.replace(withoutCoordinates, "")
                        .replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "")
                        .trim()
                    if (nonCoordinatePart.isNotEmpty()) nonSummaryTexts.add(nonCoordinatePart)
                    continue
                }

                if (isSummaryLine(trimmed)) {
                    // Check for duration (ETE)
                    if (ete.isEmpty()) {
                        val durMatch = DURATION_REGEX.find(trimmed)
                        if (durMatch != null) {
                            ete = durMatch.value.trim()
                        }
                    }

                    // Check for clock time (ETA)
                    if (eta.isEmpty()) {
                        val timeMatch = CLOCK_TIME_REGEX.find(trimmed)
                        if (timeMatch != null) {
                            eta = cleanEtaString(timeMatch.value)
                        }
                    }

                    // Check for total distance
                    if (totalDist.isEmpty()) {
                        val tdMatch = TOTAL_DIST_REGEX.find(trimmed)
                        if (tdMatch != null) {
                            totalDist = tdMatch.value.trim()
                        }
                    }

                    // Handle bullet-separated trip metrics (e.g. "1 hr 43 min · 44 km · 17:32 ETA")
                    val parts = trimmed.split(Regex("""[·•|\-]""")).map { it.trim() }.filter { it.isNotEmpty() }
                    for (part in parts) {
                        if (ete.isEmpty() && DURATION_REGEX.containsMatchIn(part)) {
                            val m = DURATION_REGEX.find(part)
                            if (m != null) ete = m.value.trim()
                        }
                        if (eta.isEmpty() && CLOCK_TIME_REGEX.containsMatchIn(part)) {
                            val m = CLOCK_TIME_REGEX.find(part)
                            if (m != null) eta = cleanEtaString(m.value)
                        }
                        if (totalDist.isEmpty() && TOTAL_DIST_REGEX.containsMatchIn(part)) {
                            val m = TOTAL_DIST_REGEX.find(part)
                            if (m != null) totalDist = m.value.trim()
                        }
                    }
                    continue
                }

                nonSummaryTexts.add(trimmed)
            }

            // 2. Scan non-summary texts for turn maneuver distance
            val remainingTexts = mutableListOf<String>()
            for (text in nonSummaryTexts) {
                val distMatch = DISTANCE_EXTRACTION_REGEX.find(text)
                if (distMatch != null && distance.isEmpty()) {
                    distance = distMatch.groupValues[1].trim()
                    val leftover = text.replace(distMatch.value, "")
                        .replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "")
                        .trim()
                    if (leftover.isNotEmpty() && leftover.length > 2) {
                        remainingTexts.add(leftover)
                    }
                } else {
                    remainingTexts.add(text)
                }
            }

            // 3. Filter road candidates
            val roadCandidates = remainingTexts.filter { text ->
                val isOnlyTime = CLOCK_TIME_REGEX.matches(text)
                val isOnlyDur = DURATION_REGEX.matches(text)
                val isOnlyDist = DISTANCE_EXTRACTION_REGEX.matches(text) || TOTAL_DIST_REGEX.matches(text)
                val isEtaString = text.startsWith("ETA", ignoreCase = true) || text.startsWith("Tiba", ignoreCase = true)
                val isFrameworkMethod = RemoteViewsExtractor.IGNORED_FRAMEWORK_METHOD_NAMES.contains(text.trim())
                !isOnlyTime && !isOnlyDur && !isOnlyDist && !isEtaString && !isFrameworkMethod && text != distance
            }

            // 4. Resolve Road and Sub-Road
            if (roadCandidates.isNotEmpty()) {
                val primary = roadCandidates[0]
                val maneuverMatch = MANEUVER_SPLIT_REGEX.find(primary)
                val towardMatch = TOWARD_PREFIX_REGEX.find(primary)

                when {
                    maneuverMatch != null -> {
                        val maneuver = maneuverMatch.groupValues[1].trim()
                            .replaceFirstChar { if (it.isLowerCase()) it.titlecase(Locale.ROOT) else it.toString() }
                        nextRoad = maneuverMatch.groupValues[2].trim()
                        nextRoadSub = maneuver
                    }
                    towardMatch != null -> {
                        nextRoad = towardMatch.groupValues[1].trim()
                        if (roadCandidates.size > 1) {
                            nextRoadSub = roadCandidates[1]
                        } else {
                            val prefix = towardMatch.value.substringBefore(nextRoad).trim()
                                .replaceFirstChar { if (it.isLowerCase()) it.titlecase(Locale.ROOT) else it.toString() }
                            nextRoadSub = prefix.ifEmpty { "Toward" }
                        }
                    }
                    else -> {
                        nextRoad = primary
                        if (roadCandidates.size > 1) {
                            nextRoadSub = roadCandidates[1]
                        }
                    }
                }
            }

            // 5. Fallback for ongoing navigation notification without explicit road text
            if (nextRoad.isEmpty()) {
                if (distance.isNotEmpty()) {
                    nextRoad = "Navigating route"
                } else if (sbn.isOngoing || sbn.id == 1) {
                    nextRoad = "Navigating route"
                } else {
                    return null
                }
            }

            // 6. Maneuver Icon Resolution. An explicit debug selection (1-based) wins,
            // otherwise choose a bitmap RemoteViews action before generic notification icons.
            var finalBitmap: Bitmap? = selectedBitmapIndex
                ?.takeIf { it in 1..candidateBitmaps.size }
                ?.let { candidateBitmaps[it - 1] }
                ?: candidateBitmaps.firstOrNull()
                ?: extractIcon(notification, context, appSourceContext)

            // If still null, synthesize arrow based on keywords in all candidate texts
            if (finalBitmap == null) {
                val combinedText = (candidateTexts + listOf(nextRoad, nextRoadSub)).joinToString(" ").lowercase(Locale.ROOT)
                finalBitmap = when {
                    combinedText.contains("roundabout") || combinedText.contains("bundaran") || combinedText.contains("circle") -> {
                        BitmapHelper.createRoundaboutBitmap()
                    }
                    combinedText.contains("left") || combinedText.contains("kiri") || combinedText.contains("gauche") || combinedText.contains("links") -> {
                        BitmapHelper.createTurnLeftBitmap()
                    }
                    combinedText.contains("right") || combinedText.contains("kanan") || combinedText.contains("droite") || combinedText.contains("rechts") -> {
                        BitmapHelper.createTurnRightBitmap()
                    }
                    combinedText.contains("straight") || combinedText.contains("lurus") || combinedText.contains("terus") || combinedText.contains("ahead") || combinedText.contains("toward") || combinedText.contains("ke arah") -> {
                        BitmapHelper.createStraightBitmap()
                    }
                    else -> BitmapHelper.createStraightBitmap()
                }
            }

            val selectedBitmap = selectedBitmapIndex
                ?.takeIf { it in 1..candidateBitmaps.size }
                ?.let { candidateBitmaps[it - 1] }
            val (iconBase64, monoBmp) = BitmapHelper.bitmapToBase64(selectedBitmap ?: finalBitmap)

            return NavigationState(
                nextRoad = nextRoad.replace(Regex("\\p{Z}+"), " ").trim(),
                nextRoadSub = nextRoadSub.replace(Regex("\\p{Z}+"), " ").trim(),
                distanceToTurn = distance.replace(Regex("\\p{Z}+"), "").trim(),
                eta = eta,
                ete = shortenEte(ete),
                totalDistance = totalDist.replace(Regex("\\p{Z}+"), "").trim(),
                iconBase64 = iconBase64,
                isNavigating = true,
                lastUpdatedMs = System.currentTimeMillis()
            ).apply {
                currentIconBitmap = monoBmp ?: finalBitmap
            }
        }

        private fun cleanEtaString(raw: String): String {
            return raw.replace(Regex("""(?i)\b(eta|tiba|llegada|ankunft|arrival)\b"""), "")
                .replace(Regex("""^[\s\-–•·,:]+|[\s\-–•·,:]+$"""), "")
                .trim()
        }

        /**
         * Shorten ETE duration to single-letter units for compact OLED display.
         * "1 hr 43 min" → "1h 43m"
         * "43 min"       → "43m"
         * "2 hrs"        → "2h"
         */
        private fun shortenEte(raw: String): String {
            return raw.trim()
                .replace(Regex("""(?i)\s*hrs?\b"""), "h")
                .replace(Regex("""(?i)\s*mins?\b"""), "m")
                .replace(Regex("""(?i)\s*mnt\b"""), "m")
                .replace(Regex("""(?i)\s*menit\b"""), "m")
                .replace(Regex("""(?i)\s*jam\b"""), "h")  // Indonesian "jam" = hour
                .replace(Regex("""  +"""), " ")
                .trim()
        }

        fun extractIcon(notification: Notification, context: Context, appSourceContext: Context): Bitmap? {
            val largeIcon = notification.getLargeIcon()
            if (largeIcon != null) {
                try {
                    val drawable = largeIcon.loadDrawable(context) ?: largeIcon.loadDrawable(appSourceContext)
                    if (drawable != null) {
                        val bmp = drawableToBitmap(drawable)
                        if (bmp != null) return bmp
                    }
                } catch (_: Exception) {}
            }

            @Suppress("DEPRECATION")
            if (notification.largeIcon != null) {
                return notification.largeIcon
            }

            val extras = notification.extras
            if (extras != null) {
                val bmpExtra = extras.getParcelable<Bitmap>(Notification.EXTRA_LARGE_ICON)
                    ?: extras.getParcelable<Bitmap>("android.largeIcon.big")
                    ?: extras.getParcelable<Bitmap>("android.picture")
                if (bmpExtra != null) return bmpExtra
            }

            val smallIcon = notification.getSmallIcon()
            if (smallIcon != null) {
                try {
                    val drawable = smallIcon.loadDrawable(context) ?: smallIcon.loadDrawable(appSourceContext)
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
                    DebugLogger.log("LISTENER", "Found navigation notification (pkg=${sbn.packageName}, id=${sbn.id}, ongoing=${sbn.isOngoing})", DebugLogger.Level.NAV)
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
        try {
            val navState = parseNotificationInternal(sbn)
            if (navState != null) {
                DebugLogger.log(
                    "MAPS_PARSED",
                    "Parsed -> Dist='${navState.distanceToTurn}' | Road='${navState.nextRoad}' | Sub='${navState.nextRoadSub}' | ETA='${navState.eta}' | ETE='${navState.ete}' | Total='${navState.totalDistance}'",
                    DebugLogger.Level.NAV
                )
                broadcastNavigationState(applicationContext, navState)
            } else {
                DebugLogger.log(
                    "MAPS_SKIP",
                    "Skipped: Notification lacks navigation content (pkg=${sbn.packageName}, id=${sbn.id})",
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
        val appSourceContext = try {
            createPackageContext(sbn.packageName, Context.CONTEXT_IGNORE_SECURITY)
        } catch (_: Exception) {
            applicationContext
        }

        // 1. Extract from standard notification extras
        val standardTexts = extractStandardExtrasTexts(notification)

        // 2. Extract from RemoteViews (Reflection on mActions + View hierarchy traversal)
        val rvData = RemoteViewsExtractor.extract(notification, this, appSourceContext)

        val candidateTexts = (standardTexts + rvData.texts).filter { it.isNotBlank() }.distinct()

        DebugLogger.log(
            "MAPS_EXTRACT",
            "Extracted ${candidateTexts.size} texts: $candidateTexts",
            DebugLogger.Level.DEBUG
        )

        return extractNavData(
            sbn,
            candidateTexts,
            rvData.bitmaps,
            notification,
            this,
            appSourceContext,
            preferredRemoteViewsBitmapIndex
        )
    }
}
