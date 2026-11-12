package com.esp32.googlemaps.util

import android.app.Notification
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.RemoteViews
import android.widget.TextView
import java.lang.reflect.Field
import java.util.ArrayList

object RemoteViewsExtractor {

    data class ExtractedData(
        val texts: List<String>,
        val bitmaps: List<Bitmap>,
        val actionsDebug: List<String>
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

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            try {
                val recovered = Notification.Builder.recoverBuilder(appSourceContext, notification)
                candidateRemoteViews.add(recovered.createBigContentView())
                candidateRemoteViews.add(recovered.createContentView())
                candidateRemoteViews.add(recovered.createHeadsUpContentView())
            } catch (_: Throwable) {
                try {
                    val recoveredLocal = Notification.Builder.recoverBuilder(context, notification)
                    candidateRemoteViews.add(recoveredLocal.createBigContentView())
                    candidateRemoteViews.add(recoveredLocal.createContentView())
                } catch (_: Throwable) {}
            }
        }

        val seenRv = mutableSetOf<Int>()
        for (rv in candidateRemoteViews) {
            if (rv == null) continue
            val identity = System.identityHashCode(rv)
            if (seenRv.contains(identity)) continue
            seenRv.add(identity)

            // 1. Reflection on RemoteViews.mActions (works even if inflation fails)
            extractViaReflection(rv, context, appSourceContext, allTexts, allBitmaps, actionsDebug)

            // 2. Inflation and recursive view hierarchy traversal
            extractViaInflation(rv, appSourceContext, allTexts, allBitmaps)
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
        context: Context,
        appContext: Context,
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
                inspectAction(action, context, appContext, outTexts, outBitmaps, outDebug)
            }
        } catch (e: Throwable) {
            outDebug.add("extractViaReflection error: ${e.message}")
        }
    }

    private fun inspectAction(
        action: Any,
        context: Context,
        appContext: Context,
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
                            extractViaReflection(v, context, appContext, outTexts, outBitmaps, outDebug)
                        }
                    }

                    // Extract any CharSequence / String values
                    if (v is CharSequence && v.toString().isNotBlank()) {
                        val s = v.toString().trim()
                        if (s.isNotEmpty()) outTexts.add(s)
                    } else if (v is Bitmap) {
                        outBitmaps.add(v)
                    } else if (v is RemoteViews) {
                        extractViaReflection(v, context, appContext, outTexts, outBitmaps, outDebug)
                    } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && v is android.graphics.drawable.Icon) {
                        try {
                            val drawable = v.loadDrawable(appContext) ?: v.loadDrawable(context)
                            if (drawable != null) {
                                val bmp = drawableToBitmap(drawable)
                                if (bmp != null) outBitmaps.add(bmp)
                            }
                        } catch (_: Throwable) {}
                    }
                } catch (_: Throwable) {}
            }
            clazz = clazz.superclass
        }

        if (currentMethodName != null || foundValue != null) {
            outDebug.add("Action: method=$currentMethodName, viewId=0x${Integer.toHexString(viewId ?: 0)}, value=$foundValue")
        }
    }

    private fun extractViaInflation(
        rv: RemoteViews,
        appSourceContext: Context,
        outTexts: MutableList<String>,
        outBitmaps: MutableList<Bitmap>
    ) {
        try {
            val view = rv.apply(appSourceContext, null)
            traverseViewTree(view, outTexts, outBitmaps)
        } catch (_: Throwable) {
            try {
                val inflater = appSourceContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE) as? LayoutInflater
                val inflated = inflater?.inflate(rv.layoutId, null)
                if (inflated is ViewGroup) {
                    rv.reapply(appSourceContext, inflated)
                    traverseViewTree(inflated, outTexts, outBitmaps)
                }
            } catch (_: Throwable) {}
        }
    }

    private fun traverseViewTree(
        view: View?,
        outTexts: MutableList<String>,
        outBitmaps: MutableList<Bitmap>
    ) {
        if (view == null) return

        if (view is TextView) {
            val t = view.text?.toString()?.trim()
            if (!t.isNullOrEmpty()) {
                outTexts.add(t)
            }
        } else if (view is ImageView) {
            val d = view.drawable
            if (d != null) {
                val bmp = drawableToBitmap(d)
                if (bmp != null) outBitmaps.add(bmp)
            }
        }

        if (view is ViewGroup) {
            for (i in 0 until view.childCount) {
                traverseViewTree(view.getChildAt(i), outTexts, outBitmaps)
            }
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
        } catch (_: Throwable) {
            null
        }
    }
}
