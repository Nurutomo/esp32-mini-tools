package com.esp32.googlemaps.util

import android.util.Base64
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Compact binary replacement for the navigation JSON payload.
 *
 * Frame: [0xAA 0x55][type][payload length LE16][payload]
 * type 0x01: flags u8, speed i16 LE, speedLimit u8, epoch u32 LE, tzOffset i32 LE,
 *            then six length-prefixed UTF-8 strings (nextRoad, nextRoadSub,
 *            turnDistance, eta, ete, totalDistance), then the raw 128-byte icon
 *            bitmap when flags.bit1 is set. flags.bit3 marks the bitmap as
 *            row-differenced (see rowDiff).
 * type 0x02: [uncompressed length LE16][canonical Huffman bitstream] of the
 *            above type 0x01 payload. Sent only when it is actually shorter.
 */
object NavigationBinaryProtocol {

    private const val MAGIC_0 = 0xAA
    private const val MAGIC_1 = 0x55
    private const val TYPE_NAV = 0x01
    private const val TYPE_HUFFMAN = 0x02

    private const val FLAG_ACTIVE = 0x01
    private const val FLAG_ICON = 0x02
    private const val FLAG_BACKLIGHT = 0x04
    private const val FLAG_ICON_ROW_DIFF = 0x08

    private const val ICON_BYTES = 128

    // Canonical Huffman codes for all 256 byte values, packed as (code shl 4) or
    // code length. Derived from real navigation frames and shared bit-for-bit
    // with kHuffLengths in src/navigation_state.cpp.
    private val CODES = longArrayOf(
        0x00002L, 0x7f52fL, 0x7f53fL, 0x7f54fL, 0x00ec8L, 0x00286L, 0x00667L, 0x00677L,
        0x01f09L, 0x00687L, 0x7f55fL, 0x7f56fL, 0x00ed8L, 0x7f57fL, 0x7f58fL, 0x01f19L,
        0x01f29L, 0x7f59fL, 0x7f5afL, 0x7f5bfL, 0x7f5cfL, 0x7f5dfL, 0x7f5efL, 0x7f5ffL,
        0x00697L, 0x7f60fL, 0x7f61fL, 0x7f62fL, 0x7f63fL, 0x7f64fL, 0x7f65fL, 0x7f66fL,
        0x00084L, 0x7f67fL, 0x7f68fL, 0x7f69fL, 0x7f6afL, 0x7f6bfL, 0x7f6cfL, 0x7f6dfL,
        0x7f6efL, 0x7f6ffL, 0x006a7L, 0x7f70fL, 0x7f71fL, 0x7f72fL, 0x006b7L, 0x7f73fL,
        0x00296L, 0x002a6L, 0x002b6L, 0x002c6L, 0x00ee8L, 0x002d6L, 0x7f74fL, 0x00ef8L,
        0x00f08L, 0x03f8aL, 0x006c7L, 0x7f75fL, 0x006d7L, 0x7f76fL, 0x7f77fL, 0x7f78fL,
        0x7f79fL, 0x01f39L, 0x7f7afL, 0x7f7bfL, 0x7f7cfL, 0x7f7dfL, 0x7f7efL, 0x01f49L,
        0x7f7ffL, 0x00f18L, 0x00f28L, 0x7f80fL, 0x006e7L, 0x01f59L, 0x7f81fL, 0x7f82fL,
        0x01f69L, 0x7f83fL, 0x00f38L, 0x00f48L, 0x01f79L, 0x7f84fL, 0x7f85fL, 0x7f86fL,
        0x7f87fL, 0x01f89L, 0x7f88fL, 0x7f89fL, 0x7f8afL, 0x7f8bfL, 0x7f8cfL, 0x7f8dfL,
        0x7f8efL, 0x002e6L, 0x006f7L, 0x7f8ffL, 0x00707L, 0x002f6L, 0x00f58L, 0x00f68L,
        0x00717L, 0x00306L, 0x7f90fL, 0x00727L, 0x00737L, 0x00125L, 0x00135L, 0x00316L,
        0x00747L, 0x7f91fL, 0x00757L, 0x01f99L, 0x00326L, 0x00f78L, 0x7f92fL, 0x01fa9L,
        0x01fb9L, 0x7f93fL, 0x7f94fL, 0x7f95fL, 0x7f96fL, 0x7f97fL, 0x7f98fL, 0x7f99fL,
        0x7f9afL, 0x7f9bfL, 0x7f9cfL, 0x7f9dfL, 0x7f9efL, 0x7f9ffL, 0x7fa0fL, 0x7fa1fL,
        0x7fa2fL, 0x7fa3fL, 0x7fa4fL, 0x7fa5fL, 0x7fa6fL, 0x7fa7fL, 0x7fa8fL, 0x7fa9fL,
        0x7faafL, 0x7fabfL, 0x7facfL, 0x7fadfL, 0x7faefL, 0x7faffL, 0x7fb0fL, 0x7fb1fL,
        0x7fb2fL, 0x7fb3fL, 0x7fb4fL, 0x7fb5fL, 0x7fb6fL, 0x7fb7fL, 0x7fb8fL, 0x7fb9fL,
        0x7fbafL, 0x7fbbfL, 0x7fbcfL, 0x7fbdfL, 0x7fbefL, 0x7fbffL, 0x7fc0fL, 0x7fc1fL,
        0x7fc2fL, 0x7fc3fL, 0x7fc4fL, 0x7fc5fL, 0x7fc6fL, 0x7fc7fL, 0x7fc8fL, 0x7fc9fL,
        0x7fcafL, 0x7fcbfL, 0x7fccfL, 0x7fcdfL, 0x7fcefL, 0x7fcffL, 0x7fd0fL, 0x7fd1fL,
        0x7fd2fL, 0x7fd3fL, 0x7fd4fL, 0x7fd5fL, 0x7fd6fL, 0x7fd7fL, 0x7fd8fL, 0x7fd9fL,
        0x7fdafL, 0x7fdbfL, 0x7fdcfL, 0x7fddfL, 0x7fdefL, 0x7fdffL, 0x7fe0fL, 0x7fe1fL,
        0x7fe2fL, 0x7fe3fL, 0x7fe4fL, 0x7fe5fL, 0x7fe6fL, 0x7fe7fL, 0x7fe8fL, 0x7fe9fL,
        0x7feafL, 0x7febfL, 0x7fecfL, 0x7fedfL, 0x7feefL, 0x7feffL, 0x7ff0fL, 0x7ff1fL,
        0x7ff2fL, 0x7ff3fL, 0x7ff4fL, 0x7ff5fL, 0x7ff6fL, 0x7ff7fL, 0x7ff8fL, 0x7ff9fL,
        0x7ffafL, 0x7ffbfL, 0x7ffcfL, 0x7ffdfL, 0x7ffefL, 0x7ffffL, 0x3f90eL, 0x3f91eL,
        0x3f92eL, 0x3f93eL, 0x3f94eL, 0x3f95eL, 0x3f96eL, 0x3f97eL, 0x3f98eL, 0x3f99eL,
        0x3f9aeL, 0x3f9beL, 0x3f9ceL, 0x3f9deL, 0x3f9eeL, 0x3f9feL, 0x3fa0eL, 0x3fa1eL,
        0x3fa2eL, 0x3fa3eL, 0x3fa4eL, 0x3fa5eL, 0x3fa6eL, 0x3fa7eL, 0x3fa8eL, 0x00012L,
    )

    private fun huffmanEncode(payload: ByteArray): ByteArray {
        val out = ByteArrayOutputStream(payload.size / 2 + 2)
        var acc = 0L
        var bits = 0
        for (byte in payload) {
            val entry = CODES[byte.toInt() and 0xFF]
            val length = (entry and 0xFL).toInt()
            acc = (acc shl length) or (entry ushr 4)
            bits += length
            while (bits >= 8) {
                bits -= 8
                out.write(((acc ushr bits) and 0xFFL).toInt())
            }
            acc = acc and ((1L shl bits) - 1)
        }
        if (bits > 0) out.write(((acc shl (8 - bits)) and 0xFFL).toInt())
        return out.toByteArray()
    }

    /**
     * XOR every row of the 1-bit bitmap with the row above it. Monochrome
     * maneuver icons are mostly blank rows, so this replaces long runs of
     * identical row bytes with zeros, which the shared codebook codes in 2 bits.
     */
    private fun rowDiff(icon: ByteArray): ByteArray {
        val out = icon.copyOf()
        for (row in 1 until 32) {
            for (col in 0 until 4) {
                val i = row * 4 + col
                out[i] = (icon[i].toInt() xor icon[i - 4].toInt()).toByte()
            }
        }
        return out
    }

    private fun frame(type: Int, payload: ByteArray): ByteArray {
        val len = payload.size
        return ByteArray(5 + len).also { out ->
            out[0] = MAGIC_0.toByte()
            out[1] = MAGIC_1.toByte()
            out[2] = type.toByte()
            out[3] = (len and 0xFF).toByte()
            out[4] = ((len shr 8) and 0xFF).toByte()
            payload.copyInto(out, 5)
        }
    }

    private fun field(out: ByteArrayOutputStream, value: String?, maxBytes: Int) {
        val text = value.orEmpty()
        var end = 0
        var byteCount = 0
        while (end < text.length) {
            val codePoint = text.codePointAt(end)
            val encodedBytes = when {
                codePoint <= 0x7F -> 1
                codePoint <= 0x7FF -> 2
                codePoint <= 0xFFFF -> 3
                else -> 4
            }
            if (byteCount + encodedBytes > maxBytes) break
            byteCount += encodedBytes
            end += Character.charCount(codePoint)
        }
        val bytes = text.substring(0, end).toByteArray(Charsets.UTF_8)
        out.write(bytes.size)
        out.write(bytes, 0, bytes.size)
    }

    fun encode(
        navigating: Boolean,
        nextRoad: String?,
        nextRoadSub: String?,
        turnDistance: String?,
        eta: String?,
        ete: String?,
        totalDistance: String?,
        iconBase64: String?,
        speed: Int,
        speedLimit: Int,
        displayBacklight: Boolean,
        epochSeconds: Long,
        tzOffsetSeconds: Int
    ): ByteArray {
        val icon = decodeIcon(iconBase64)
        val payload = ByteArrayOutputStream(64 + ICON_BYTES).apply {
            val flags = (if (navigating) FLAG_ACTIVE else 0) or
                (if (icon != null) FLAG_ICON or FLAG_ICON_ROW_DIFF else 0) or
                (if (displayBacklight) FLAG_BACKLIGHT else 0)
            write(flags)
            write(ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN)
                .putShort(speed.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort())
                .array())
            write(speedLimit.coerceIn(0, 255))
            write(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(epochSeconds.toInt()).array())
            write(ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(tzOffsetSeconds).array())
            field(this, nextRoad, 63)
            field(this, nextRoadSub, 63)
            field(this, turnDistance, 31)
            field(this, eta, 31)
            field(this, ete, 31)
            field(this, totalDistance, 31)
            if (icon != null) write(rowDiff(icon))
        }.toByteArray()

        // The decoder needs the exact decoded length, so only take the Huffman
        // frame when the extra two length bytes still pay for themselves.
        val encoded = huffmanEncode(payload)
        if (encoded.size + 2 < payload.size) {
            val compressed = ByteArray(2 + encoded.size)
            compressed[0] = (payload.size and 0xFF).toByte()
            compressed[1] = ((payload.size shr 8) and 0xFF).toByte()
            encoded.copyInto(compressed, 2)
            return frame(TYPE_HUFFMAN, compressed)
        }
        return frame(TYPE_NAV, payload)
    }

    private fun decodeIcon(iconBase64: String?): ByteArray? {
        if (iconBase64.isNullOrEmpty()) return null
        val raw = try {
            Base64.decode(iconBase64, Base64.NO_WRAP)
        } catch (_: Exception) {
            return null
        }
        if (raw.size != ICON_BYTES) return null
        return raw
    }
}
