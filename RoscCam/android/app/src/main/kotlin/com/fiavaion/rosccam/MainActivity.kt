// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

package com.fiavaion.rosccam

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ApplicationInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel
import kotlin.math.cos
import kotlin.math.sin

/**
 * What the app needs from Android itself: the device's rotation, its name, the Wi-Fi broadcasts the
 * editors announce themselves with, and in debug builds the test script's hooks.
 */
class MainActivity : FlutterActivity() {
    private var multicastLock: WifiManager.MulticastLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // An operator holds the device still for minutes at a time; the screen must not go dark.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger
        val rotation = RotationStream(getSystemService(Context.SENSOR_SERVICE) as SensorManager)
        EventChannel(messenger, "rosccam/rotation").setStreamHandler(rotation)
        MethodChannel(messenger, "rosccam/device").setMethodCallHandler { call, result ->
            when (call.method) {
                "name" -> result.success(deviceName())
                "broadcasts" -> {
                    hearBroadcasts(call.arguments == true)
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
        val debuggable = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        if (debuggable) {
            EventChannel(messenger, "rosccam/test").setStreamHandler(TestHooks(this, rotation))
        }
    }

    override fun onDestroy() {
        hearBroadcasts(false)
        super.onDestroy()
    }

    private fun deviceName(): String {
        val maker = Build.MANUFACTURER.replaceFirstChar { it.uppercase() }
        return if (Build.MODEL.startsWith(maker, ignoreCase = true)) Build.MODEL else "$maker ${Build.MODEL}"
    }

    private fun hearBroadcasts(on: Boolean) {
        if (on && multicastLock == null) {
            val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicastLock = wifi.createMulticastLock("RoscCam").apply {
                setReferenceCounted(false)
                acquire()
            }
        } else if (!on) {
            multicastLock?.release()
            multicastLock = null
        }
    }
}

/**
 * The device's rotation for the Dart side: the game rotation vector, which uses the gyroscope and
 * the accelerometer but not the compass, so steel and cables on a set do not swing it.
 */
class RotationStream(private val sensors: SensorManager) : EventChannel.StreamHandler, SensorEventListener {
    private var sink: EventChannel.EventSink? = null
    private val quaternion = FloatArray(4)

    /** A rotation the test script holds the device at, in place of the sensor's. */
    private var held: DoubleArray? = null

    override fun onListen(arguments: Any?, events: EventChannel.EventSink) {
        sink = events
        held?.let { events.success(it) }
        val sensor = sensors.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
        if (sensor == null) {
            if (held == null) {
                events.error("no-sensor", "This device has no rotation sensor, so it cannot operate a support.", null)
            }
            return
        }
        sensors.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME)
    }

    override fun onCancel(arguments: Any?) {
        sensors.unregisterListener(this)
        sink = null
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (held != null) {
            return
        }
        // w, x, y, z of the device's rotation in its world frame.
        SensorManager.getQuaternionFromVector(quaternion, event.values)
        sink?.success(doubleArrayOf(quaternion[0].toDouble(), quaternion[1].toDouble(), quaternion[2].toDouble(), quaternion[3].toDouble()))
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    fun hold(rotation: DoubleArray) {
        held = rotation
        sink?.success(rotation)
    }
}

/**
 * Debug builds only: the test script connects, pairs and turns the device by Android broadcast,
 * since the emulator's own rotation cannot be turned from a script (ADR-014, Spike #5).
 *
 *     adb shell am broadcast -p com.fiavaion.rosccam -a com.fiavaion.rosccam.TEST_CONNECT --es address 10.0.2.2
 *     adb shell am broadcast -p com.fiavaion.rosccam -a com.fiavaion.rosccam.TEST_PAIR --es code 1234
 *     adb shell am broadcast -p com.fiavaion.rosccam -a com.fiavaion.rosccam.TEST_ROTATION --ef right 30 --ef up 0 --ef roll 0
 *
 * TEST_ROTATION holds the device in landscape, its top to the left, turned right, tipped up and
 * leaned clockwise by those degrees from facing north.
 */
private class TestHooks(private val context: Context, private val rotation: RotationStream) : EventChannel.StreamHandler {
    private var receiver: BroadcastReceiver? = null

    override fun onListen(arguments: Any?, events: EventChannel.EventSink) {
        val hooks = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                when (intent.action) {
                    CONNECT -> intent.getStringExtra("address")?.let { events.success(mapOf("connect" to it)) }
                    PAIR -> intent.getStringExtra("code")?.let { events.success(mapOf("pair" to it)) }
                    ROTATION -> rotation.hold(
                        held(intent.getFloatExtra("right", 0f).toDouble(), intent.getFloatExtra("up", 0f).toDouble(), intent.getFloatExtra("roll", 0f).toDouble())
                    )
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(CONNECT)
            addAction(PAIR)
            addAction(ROTATION)
        }
        // The script's broadcasts come from the shell, another app as far as Android can tell.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(hooks, filter, Context.RECEIVER_EXPORTED)
        } else {
            context.registerReceiver(hooks, filter)
        }
        receiver = hooks
    }

    override fun onCancel(arguments: Any?) {
        receiver?.let { context.unregisterReceiver(it) }
        receiver = null
    }

    companion object {
        const val CONNECT = "com.fiavaion.rosccam.TEST_CONNECT"
        const val PAIR = "com.fiavaion.rosccam.TEST_PAIR"
        const val ROTATION = "com.fiavaion.rosccam.TEST_ROTATION"

        /** w, x, y, z of a turn of the given degrees about an axis. */
        private fun turn(x: Double, y: Double, z: Double, degrees: Double): DoubleArray {
            val half = Math.toRadians(degrees) / 2
            val s = sin(half)
            return doubleArrayOf(cos(half), x * s, y * s, z * s)
        }

        private fun times(a: DoubleArray, b: DoubleArray) = doubleArrayOf(
            a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
        )

        /** The device held in landscape, top to the left, facing north (+y), then turned so. */
        fun held(right: Double, up: Double, roll: Double): DoubleArray {
            // Its x up, its y west and its z south.
            val north = doubleArrayOf(0.5, 0.5, -0.5, 0.5)
            val leaned = times(turn(0.0, 1.0, 0.0, roll), north)
            val tipped = times(turn(1.0, 0.0, 0.0, up), leaned)
            return times(turn(0.0, 0.0, 1.0, -right), tipped)
        }
    }
}
