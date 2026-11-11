package com.esp32.googlemaps.service

import android.app.Notification
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Typeface
import android.graphics.drawable.BitmapDrawable
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
import timber.log.Timber

class GoogleMapsNotificationListener : NotificationListenerService() {

    companion object {
        const val GMAPS_PACKAGE = "com.google.android.apps.maps"
        const val ACTION_NAV_UPDATE = "com.esp32.googlemaps.NAV_UPDATE"
        const val EXTRA_NAV_STATE = "nav_state"
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null || !isGoogleMapsNavigation(sbn)) return
        try {
            val navState = parseNotification(sbn)
            if (navState != null) {
                broadcastNavigation(navState)
            }
        } catch (e: Exception) {
            Timber.e("Error parsing Google Maps notification: ${e.message}")
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null || sbn.packageName != GMAPS_PACKAGE) return
        val emptyState = NavigationState(isNavigating = false)
        broadcastNavigation(emptyState)
    }

    private fun isGoogleMapsNavigation(sbn: StatusBarNotification): Boolean {
        if (sbn.packageName != GMAPS_PACKAGE) return false
        if (!sbn.isOngoing) return false

        val extras = sbn.notification.extras ?: return false
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""

        // Filter out non-navigation notifications (like downloads, reviews, traffic summary)
        return title.isNotEmpty() || text.isNotEmpty()
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

        // 1. Try extracting large icon directly
        val largeIcon = notification.getLargeIcon()
        if (largeIcon != null) {
            try {
                val drawable = largeIcon.loadDrawable(this)
                if (drawable is BitmapDrawable) {
                    rawBitmap = drawable.bitmap
                }
            } catch (_: Exception) {}
        }
        if (rawBitmap == null) {
            @Suppress("DEPRECATION")
            rawBitmap = notification.largeIcon
        }

        // 2. Try extracting from standard notification extras
        if (extras != null) {
            val titleSeq = extras.getCharSequence(Notification.EXTRA_TITLE)
            if (titleSeq != null) {
                distance = titleSeq.toString().trim()
            }

            val textSeq = extras.getCharSequence(Notification.EXTRA_TEXT)
            if (textSeq is Spanned) {
                val parsed = parseSpannedText(textSeq)
                nextRoad = parsed.first
                nextRoadSub = parsed.second
            } else if (textSeq != null) {
                nextRoad = textSeq.toString().trim()
            }

            val subTextSeq = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)
            if (subTextSeq != null) {
                val parts = subTextSeq.toString().split("·")
                if (parts.size >= 3) {
                    ete = parts[0].trim()
                    totalDist = parts[1].trim()
                    eta = parts[2].replace("ETA", "").trim()
                } else if (parts.size == 2) {
                    ete = parts[0].trim()
                    eta = parts[1].trim()
                } else {
                    eta = subTextSeq.toString().trim()
                }
            }
        }

        // 3. Fallback / deep search inside RemoteViews hierarchy for maximum fidelity
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

                    if (directionView != null) {
                        val dText = directionView.text
                        if (dText is Spanned) {
                            val parsed = parseSpannedText(dText)
                            if (parsed.first.isNotEmpty()) nextRoad = parsed.first
                            if (parsed.second.isNotEmpty()) nextRoadSub = parsed.second
                        } else if (!dText.isNullOrEmpty()) {
                            nextRoad = dText.toString()
                        }
                    }

                    if (titleView != null && !titleView.text.isNullOrEmpty()) {
                        distance = titleView.text.toString().trim()
                    }

                    if (headerView != null && !headerView.text.isNullOrEmpty()) {
                        val parts = headerView.text.split("·")
                        if (parts.size >= 3) {
                            ete = parts[0].trim()
                            totalDist = parts[1].trim()
                            eta = parts[2].replace("ETA", "").trim()
                        }
                    }

                    if (rawBitmap == null && rightIconView?.drawable is BitmapDrawable) {
                        rawBitmap = (rightIconView.drawable as BitmapDrawable).bitmap
                    }
                }
            }
        } catch (e: Exception) {
            Timber.v("RemoteViews inspection fallback: ${e.message}")
        }

        if (nextRoad.isEmpty() && distance.isEmpty()) {
            return null
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

        // Also pass directly to foreground service if running
        NavigationBroadcastService.updateNavigation(applicationContext, state)
    }
}
