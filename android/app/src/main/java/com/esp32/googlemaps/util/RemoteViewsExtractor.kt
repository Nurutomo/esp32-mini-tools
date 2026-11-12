package com.esp32.googlemaps.util

import android.app.Notification
import android.content.Context
import android.graphics.Bitmap
import android.widget.RemoteViews
import java.lang.reflect.Field
import java.util.ArrayList

object RemoteViewsExtractor {

    data class ExtractedData(
        val texts: List<String>,
        val bitmaps: List<Bitmap>,
        val actionsDebug: List<String>
    )

    /**
     * Pure in-memory reflection extractor.
     * Operates in sub-millisecond time without view inflation, IPC, or recoverBuilder calls.
     */
    fun extract(notification: Notification, context: Context, appSourceContext: Context): ExtractedData {
        val allTexts = mutableListOf<String>()
        val allBitmaps = mutableListOf<Bitmap>()
        val actionsDebug = mutableListOf<String>()

        val candidateRemoteViews = mutableListOf<RemoteViews?>()

        @Suppress("DEPRECATION")
        candidateRemoteViews.add(notification.bigContentView)
        @Suppress("DEPRECATION")
        candidateRemoteViews.add(notification.contentView)
        @Suppress("DEPRECATION")
        candidateRemoteViews.add(notification.headsUpContentView)
        @Suppress("DEPRECATION")
        candidateRemoteViews.add(notification.tickerView)

        val seenRv = mutableSetOf<Int>()
        for (rv in candidateRemoteViews) {
            if (rv == null) continue
            val identity = System.identityHashCode(rv)
            if (seenRv.contains(identity)) continue
            seenRv.add(identity)

            extractViaReflection(rv, allTexts, allBitmaps, actionsDebug)
        }

        // Deduplicate texts while preserving original insertion order
        val distinctTexts = mutableListOf<String>()
        for (t in allTexts) {
            val trimmed = t.trim()
            if (trimmed.isNotEmpty() && !distinctTexts.contains(trimmed)) {
                distinctTexts.add(trimmed)
            }
        }

        return ExtractedData(distinctTexts, allBitmaps, actionsDebug)
    }

    private fun extractViaReflection(
        rv: RemoteViews,
        outTexts: MutableList<String>,
        outBitmaps: MutableList<Bitmap>,
        outDebug: MutableList<String>
    ) {
        try {
            val rvClass = rv.javaClass
            val mActionsField = findField(rvClass, "mActions") ?: return
            mActionsField.isAccessible = true
            val actions = mActionsField.get(rv) as? ArrayList<*> ?: return

            outDebug.add("RemoteViews(layoutId=0x${Integer.toHexString(rv.layoutId)}) has ${actions.size} actions")

            for (action in actions) {
                if (action == null) continue
                inspectAction(action, outTexts, outBitmaps, outDebug)
            }
        } catch (e: Throwable) {
            outDebug.add("extractViaReflection error: ${e.message}")
        }
    }

    private fun inspectAction(
        action: Any,
        outTexts: MutableList<String>,
        outBitmaps: MutableList<Bitmap>,
        outDebug: MutableList<String>
    ) {
        val actionClass = action.javaClass
        var currentMethodName: String? = null
        var foundValue: Any? = null
        var viewId: Int? = null

        var clazz: Class<*>? = actionClass
        while (clazz != null && clazz != Any::class.java) {
            for (field in clazz.declaredFields) {
                try {
                    field.isAccessible = true
                    val name = field.name
                    val v = field.get(action) ?: continue

                    when {
                        name == "methodName" && v is String -> currentMethodName = v
                        name == "viewId" && v is Int -> viewId = v
                        name == "value" -> foundValue = v
                        name == "bitmap" && v is Bitmap -> outBitmaps.add(v)
                        name == "nestedViews" && v is RemoteViews -> {
                            extractViaReflection(v, outTexts, outBitmaps, outDebug)
                        }
                    }

                    // Pure in-memory extraction (no IPC, no network, no disk)
                    if (v is CharSequence && v.toString().isNotBlank()) {
                        val s = v.toString().trim()
                        if (s.isNotEmpty()) outTexts.add(s)
                    } else if (v is Bitmap) {
                        outBitmaps.add(v)
                    } else if (v is RemoteViews) {
                        extractViaReflection(v, outTexts, outBitmaps, outDebug)
                    }
                } catch (_: Throwable) {}
            }
            clazz = clazz.superclass
        }

        if (currentMethodName != null || foundValue != null) {
            outDebug.add("Action: method=$currentMethodName, viewId=0x${Integer.toHexString(viewId ?: 0)}, value=$foundValue")
        }
    }

    private fun findField(clazz: Class<*>, fieldName: String): Field? {
        var c: Class<*>? = clazz
        while (c != null && c != Any::class.java) {
            try {
                return c.getDeclaredField(fieldName)
            } catch (_: NoSuchFieldException) {
                c = c.superclass
            }
        }
        return null
    }
}
