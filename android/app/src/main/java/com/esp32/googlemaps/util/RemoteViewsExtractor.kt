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
    val IGNORED_FRAMEWORK_METHOD_NAMES = setOf(
        "setText", "setVisibility", "setBackgroundColor", "setImageBitmap",
        "setImageViewBitmap", "setImageResource", "setTextColor", "setCharSequence",
        "setInt", "setBoolean", "setLong", "setFloat", "setDouble", "setUri",
        "setImageURI", "setImageIcon", "setAdjustViewBounds", "setMaxHeight", "setMaxWidth"
    )

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

            extractViaReflection(rv, allTexts, allBitmaps, actionsDebug, seenRv, 0)
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
        outDebug: MutableList<String>,
        seenRv: MutableSet<Int>,
        depth: Int
    ) {
        if (depth > 3) return
        try {
            val rvClass = rv.javaClass
            val mActionsField = findField(rvClass, "mActions") ?: return
            mActionsField.isAccessible = true
            val actions = mActionsField.get(rv) as? ArrayList<*> ?: return

            outDebug.add("RemoteViews(layoutId=0x${Integer.toHexString(rv.layoutId)}) has ${actions.size} actions")

            for (action in actions) {
                if (action == null) continue
                inspectAction(action, outTexts, outBitmaps, outDebug, seenRv, depth)
            }
        } catch (e: Throwable) {
            outDebug.add("extractViaReflection error: ${e.message}")
        }
    }

    private fun inspectAction(
        action: Any,
        outTexts: MutableList<String>,
        outBitmaps: MutableList<Bitmap>,
        outDebug: MutableList<String>,
        seenRv: MutableSet<Int>,
        depth: Int
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

                    // Crucial: Skip synthetic inner-class outer references (this$0) and parent context
                    // to prevent infinite reflection loops between RemoteViews and Action instances!
                    if (name.startsWith("this$") || name == "mApplication" || name == "mActions" || name == "mContext") {
                        continue
                    }

                    val v = field.get(action) ?: continue

                    when {
                        name == "methodName" && v is String -> currentMethodName = v
                        name == "viewId" && v is Int -> viewId = v
                        name == "value" -> foundValue = v
                        name == "bitmap" && v is Bitmap -> outBitmaps.add(v)
                        (name == "nestedViews" || name == "mNestedViews") && v is RemoteViews -> {
                            if (depth < 3) {
                                val id = System.identityHashCode(v)
                                if (seenRv.add(id)) {
                                    extractViaReflection(v, outTexts, outBitmaps, outDebug, seenRv, depth + 1)
                                }
                            }
                        }
                    }

                    // Pure in-memory extraction (no IPC, no network, no disk)
                    // Only extract content text values, never reflection metadata or method names
                    if (name != "methodName" && name != "signature" && name != "type") {
                        if (v is CharSequence && v.toString().isNotBlank()) {
                            val s = v.toString().trim()
                            if (s.isNotEmpty() && !IGNORED_FRAMEWORK_METHOD_NAMES.contains(s)) {
                                outTexts.add(s)
                            }
                        }
                    }
                } catch (_: Throwable) {}
            }
            clazz = clazz.superclass
        }

        if (currentMethodName != null || foundValue != null) {
            val valDisplay = when (foundValue) {
                is Bitmap -> "[Bitmap ${foundValue.width}x${foundValue.height}]"
                is CharSequence -> foundValue.take(60).toString()
                else -> foundValue?.javaClass?.simpleName ?: "null"
            }
            outDebug.add("Action: method=$currentMethodName, viewId=0x${Integer.toHexString(viewId ?: 0)}, value=$valDisplay")
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
