package com.esp32.googlemaps.util

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.util.Base64

object BitmapHelper {

    const val ICON_WIDTH = 32
    const val ICON_HEIGHT = 32

    /**
     * Converts any incoming Bitmap into a 32x32 monochrome 1-bit bitmap array,
     * then Base64-encodes it in standard Adafruit_GFX drawBitmap format:
     * - 32 pixels wide x 32 pixels high
     * - (32 / 8) = 4 bytes per row * 32 rows = 128 bytes
     * - Bit 7 of byte 0 = pixel (0,0); Bit 0 of byte 0 = pixel (7,0)
     * - 1 = WHITE (foreground maneuver icon), 0 = BLACK (background)
     */
    fun bitmapToBase64(bitmap: Bitmap?): Pair<String, Bitmap?> {
        if (bitmap == null) return Pair("", null)

        val scaled = Bitmap.createScaledBitmap(bitmap, ICON_WIDTH, ICON_HEIGHT, true)
        val monoBitmap = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
        val buffer = ByteArray((ICON_WIDTH / 8) * ICON_HEIGHT) { 0 }

        // Analyze palette to decide threshold
        var lightCount = 0
        var darkCount = 0
        for (y in 0 until ICON_HEIGHT) {
            for (x in 0 until ICON_WIDTH) {
                val pixel = scaled.getPixel(x, y)
                val alpha = Color.alpha(pixel)
                if (alpha > 64) {
                    val lum = (Color.red(pixel) * 0.299 + Color.green(pixel) * 0.587 + Color.blue(pixel) * 0.114).toInt()
                    if (lum > 140) lightCount++ else darkCount++
                }
            }
        }

        // Most Google Maps icons have white arrows (lightCount > 0)
        val detectLightAsForeground = lightCount >= (darkCount / 2)

        for (y in 0 until ICON_HEIGHT) {
            for (x in 0 until ICON_WIDTH) {
                val pixel = scaled.getPixel(x, y)
                val alpha = Color.alpha(pixel)
                var isForeground = false

                if (alpha > 70) {
                    val lum = (Color.red(pixel) * 0.299 + Color.green(pixel) * 0.587 + Color.blue(pixel) * 0.114).toInt()
                    isForeground = if (detectLightAsForeground) lum > 130 else lum < 120
                }

                if (isForeground) {
                    val byteIndex = y * (ICON_WIDTH / 8) + (x / 8)
                    val bitPos = 7 - (x % 8)
                    buffer[byteIndex] = (buffer[byteIndex].toInt() or (1 shl bitPos)).toByte()
                    monoBitmap.setPixel(x, y, Color.WHITE)
                } else {
                    monoBitmap.setPixel(x, y, Color.BLACK)
                }
            }
        }

        val base64 = Base64.encodeToString(buffer, Base64.NO_WRAP)
        return Pair(base64, monoBitmap)
    }

    /**
     * Decodes a 32x32 Base64 1-bit bitmap back into an ARGB_8888 preview Bitmap.
     */
    fun base64ToBitmap(base64: String): Bitmap? {
        if (base64.isEmpty()) return null
        return try {
            val bytes = Base64.decode(base64, Base64.NO_WRAP)
            val bitmap = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
            for (y in 0 until ICON_HEIGHT) {
                for (x in 0 until ICON_WIDTH) {
                    val byteIndex = y * (ICON_WIDTH / 8) + (x / 8)
                    val bitPos = 7 - (x % 8)
                    val isSet = if (byteIndex < bytes.size) {
                        (bytes[byteIndex].toInt() and (1 shl bitPos)) != 0
                    } else false
                    bitmap.setPixel(x, y, if (isSet) Color.WHITE else Color.BLACK)
                }
            }
            bitmap
        } catch (_: Exception) {
            null
        }
    }

    fun createTurnLeftBitmap(): Bitmap {
        val bmp = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val paint = Paint().apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeWidth = 4f
            strokeCap = Paint.Cap.ROUND
            strokeJoin = Paint.Join.ROUND
            isAntiAlias = false
        }
        canvas.drawColor(Color.BLACK)

        // Draw curved turn left arrow
        val path = Path().apply {
            moveTo(24f, 28f)
            lineTo(24f, 18f)
            cubicTo(24f, 12f, 20f, 10f, 12f, 10f)
            lineTo(8f, 10f)
        }
        canvas.drawPath(path, paint)

        // Arrow head
        paint.style = Paint.Style.FILL
        val arrow = Path().apply {
            moveTo(4f, 10f)
            lineTo(12f, 4f)
            lineTo(12f, 16f)
            close()
        }
        canvas.drawPath(arrow, paint)
        return bmp
    }

    fun createTurnRightBitmap(): Bitmap {
        val bmp = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val paint = Paint().apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeWidth = 4f
            strokeCap = Paint.Cap.ROUND
            strokeJoin = Paint.Join.ROUND
            isAntiAlias = false
        }
        canvas.drawColor(Color.BLACK)

        // Draw curved turn right arrow
        val path = Path().apply {
            moveTo(8f, 28f)
            lineTo(8f, 18f)
            cubicTo(8f, 12f, 12f, 10f, 20f, 10f)
            lineTo(24f, 10f)
        }
        canvas.drawPath(path, paint)

        // Arrow head
        paint.style = Paint.Style.FILL
        val arrow = Path().apply {
            moveTo(28f, 10f)
            lineTo(20f, 4f)
            lineTo(20f, 16f)
            close()
        }
        canvas.drawPath(arrow, paint)
        return bmp
    }

    fun createStraightBitmap(): Bitmap {
        val bmp = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val paint = Paint().apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeWidth = 4f
            strokeCap = Paint.Cap.ROUND
            isAntiAlias = false
        }
        canvas.drawColor(Color.BLACK)

        // Vertical line
        canvas.drawLine(16f, 28f, 16f, 12f, paint)

        // Arrow head
        paint.style = Paint.Style.FILL
        val arrow = Path().apply {
            moveTo(16f, 4f)
            lineTo(8f, 13f)
            lineTo(24f, 13f)
            close()
        }
        canvas.drawPath(arrow, paint)
        return bmp
    }

    fun createRoundaboutBitmap(): Bitmap {
        val bmp = Bitmap.createBitmap(ICON_WIDTH, ICON_HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val paint = Paint().apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeWidth = 3f
            isAntiAlias = false
        }
        canvas.drawColor(Color.BLACK)

        // Draw circle
        canvas.drawCircle(16f, 16f, 8f, paint)

        // Approach line
        paint.strokeWidth = 4f
        canvas.drawLine(16f, 28f, 16f, 24f, paint)

        // Exit arrow at top
        paint.style = Paint.Style.FILL
        val arrow = Path().apply {
            moveTo(16f, 2f)
            lineTo(11f, 8f)
            lineTo(21f, 8f)
            close()
        }
        canvas.drawPath(arrow, paint)
        return bmp
    }
}
