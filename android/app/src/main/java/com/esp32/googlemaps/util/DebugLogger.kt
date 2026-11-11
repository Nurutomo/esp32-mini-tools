package com.esp32.googlemaps.util

import android.os.Handler
import android.os.Looper
import timber.log.Timber
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArrayList

object DebugLogger {

    enum class Level(val label: String) {
        DEBUG("DBG"),
        INFO("INF"),
        WARN("WRN"),
        ERROR("ERR"),
        NAV("NAV"),
        BT("BT")
    }

    data class LogEntry(
        val timestamp: String,
        val tag: String,
        val message: String,
        val level: Level
    ) {
        override fun toString(): String = "[$timestamp] [$tag] [${level.label}] $message"
    }

    private const val MAX_LOGS = 400
    private val logsList = mutableListOf<LogEntry>()
    private val lock = Any()
    private val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val mainHandler = Handler(Looper.getMainLooper())

    var isListenerConnected: Boolean = false
        set(value) {
            field = value
            notifyListeners()
        }

    var logAllNotifications: Boolean = false

    interface LogListener {
        fun onLogAdded(entry: LogEntry)
        fun onLogsCleared()
        fun onStatusChanged()
    }

    private val listeners = CopyOnWriteArrayList<LogListener>()

    fun addListener(listener: LogListener) {
        listeners.add(listener)
    }

    fun removeListener(listener: LogListener) {
        listeners.remove(listener)
    }

    fun log(tag: String, message: String, level: Level = Level.INFO) {
        val time = timeFormat.format(Date())
        val entry = LogEntry(time, tag, message, level)

        synchronized(lock) {
            if (logsList.size >= MAX_LOGS) {
                logsList.removeAt(0)
            }
            logsList.add(entry)
        }

        mainHandler.post {
            for (listener in listeners) {
                listener.onLogAdded(entry)
            }
        }
    }

    fun getLogs(): List<LogEntry> {
        synchronized(lock) {
            return ArrayList(logsList)
        }
    }

    fun getAllFormattedLogs(): String {
        synchronized(lock) {
            return logsList.joinToString("\n") { it.toString() }
        }
    }

    fun clear() {
        synchronized(lock) {
            logsList.clear()
        }
        mainHandler.post {
            for (listener in listeners) {
                listener.onLogsCleared()
            }
        }
    }

    private fun notifyListeners() {
        mainHandler.post {
            for (listener in listeners) {
                listener.onStatusChanged()
            }
        }
    }

    class InAppTree : Timber.Tree() {
        override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
            val level = when (priority) {
                android.util.Log.VERBOSE, android.util.Log.DEBUG -> Level.DEBUG
                android.util.Log.INFO -> Level.INFO
                android.util.Log.WARN -> Level.WARN
                android.util.Log.ERROR, android.util.Log.ASSERT -> Level.ERROR
                else -> Level.INFO
            }
            val logTag = tag ?: "App"
            val fullMsg = if (t != null) "$message\n${t.stackTraceToString()}" else message
            log(logTag, fullMsg, level)
        }
    }
}
