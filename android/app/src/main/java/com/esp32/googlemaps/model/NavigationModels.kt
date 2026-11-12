package com.esp32.googlemaps.model

import android.graphics.Bitmap
import android.os.Parcelable
import kotlinx.parcelize.IgnoredOnParcel
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Parcelize
@Serializable
data class NavigationPayload(
    @SerialName("next_road")
    val nextRoad: String? = null,

    @SerialName("next_road_sub")
    val nextRoadSub: String? = null,

    @SerialName("next_road_distance")
    val nextRoadDistance: String? = null,

    @SerialName("eta")
    val eta: String? = null,

    @SerialName("ete")
    val ete: String? = null,

    @SerialName("distance")
    val distance: String? = null,

    @SerialName("icon")
    val iconBase64: String? = null
) : Parcelable

@Serializable
data class Esp32Message(
    @SerialName("navigation")
    val navigation: NavigationPayload? = null,

    @SerialName("speed")
    val speed: Int? = null,

    @SerialName("preferences")
    val preferences: DevicePreferences? = null,

    @SerialName("ts")
    val epochSeconds: Long? = null,

    /** UTC offset in seconds for the phone's current timezone, e.g. +25200 for UTC+7. */
    @SerialName("tz_offset")
    val tzOffsetSeconds: Int? = null
)

@Serializable
data class DevicePreferences(
    @SerialName("display_backlight")
    val displayBacklight: Boolean = false,

    @SerialName("speed_limit")
    val speedLimit: Int = 60
)

@Parcelize
data class NavigationState(
    var nextRoad: String = "",
    var nextRoadSub: String = "",
    var distanceToTurn: String = "",
    var eta: String = "",
    var ete: String = "",
    var totalDistance: String = "",
    var iconBase64: String = "",
    var speed: Int = 0,
    var isNavigating: Boolean = false,
    var lastUpdatedMs: Long = 0
) : Parcelable {
    @IgnoredOnParcel
    var currentIconBitmap: Bitmap? = null
}

data class BluetoothDeviceInfo(
    val name: String,
    val address: String,
    val isBle: Boolean,
    val isBonded: Boolean
)
