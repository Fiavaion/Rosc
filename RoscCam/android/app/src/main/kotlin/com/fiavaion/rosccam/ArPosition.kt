// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

package com.fiavaion.rosccam

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.Looper
import com.google.ar.core.ArCoreApk
import com.google.ar.core.Config
import com.google.ar.core.Session
import com.google.ar.core.TrackingState
import io.flutter.plugin.common.EventChannel
import kotlin.concurrent.thread
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin

/**
 * Where the device is carried, from ARCore, for Steadicam, handheld and drone. Sent as
 * [x, y, z, tracked] in metres, in the world frame of the rotation (right-handed, z up), so that
 * walking where the device looks moves the camera where it looks. A device without ARCore, or
 * without the camera allowed, sends [0, 0, 0, 0]: the support keeps its place.
 *
 * ARCore needs a camera texture to update, so it runs on a thread of its own with an off-screen
 * OpenGL context; nothing of the camera's picture is shown or kept.
 */
class ArPosition(private val activity: Activity, private val rotation: RotationStream) : EventChannel.StreamHandler {
    private val main = Handler(Looper.getMainLooper())
    private var sink: EventChannel.EventSink? = null
    @Volatile private var running = false
    private var worker: Thread? = null

    override fun onListen(arguments: Any?, events: EventChannel.EventSink) {
        sink = events
        events.success(UNTRACKED)
        val availability = ArCoreApk.getInstance().checkAvailability(activity)
        if (availability != ArCoreApk.Availability.SUPPORTED_INSTALLED) {
            return
        }
        if (activity.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            start()
        } else {
            activity.requestPermissions(arrayOf(Manifest.permission.CAMERA), PERMISSION_REQUEST)
        }
    }

    override fun onCancel(arguments: Any?) {
        running = false
        worker?.join(2000)
        worker = null
        sink = null
    }

    /** The activity's answer to the camera permission. */
    fun onPermission(requestCode: Int, granted: Boolean) {
        if (requestCode == PERMISSION_REQUEST && granted && sink != null) {
            start()
        }
    }

    private fun send(value: DoubleArray) {
        main.post { sink?.success(value) }
    }

    private fun start() {
        if (running) {
            return
        }
        running = true
        worker = thread(name = "RoscCam AR") { track() }
    }

    private fun track() {
        val gl = OffscreenGl()
        var session: Session? = null
        try {
            val texture = IntArray(1)
            GLES20.glGenTextures(1, texture, 0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture[0])
            session = Session(activity).apply {
                configure(Config(this).apply {
                    updateMode = Config.UpdateMode.BLOCKING
                    planeFindingMode = Config.PlaneFindingMode.DISABLED
                    lightEstimationMode = Config.LightEstimationMode.DISABLED
                })
                setCameraTextureName(texture[0])
                resume()
            }
            var wasTracked = false
            while (running) {
                val frame = session.update()
                val tracked = frame.camera.trackingState == TrackingState.TRACKING
                if (tracked) {
                    val pose = frame.androidSensorPose
                    send(toRotationWorld(pose.tx().toDouble(), pose.ty().toDouble(), pose.tz().toDouble(),
                        doubleArrayOf(pose.qw().toDouble(), pose.qx().toDouble(), pose.qy().toDouble(), pose.qz().toDouble())))
                } else if (wasTracked) {
                    send(UNTRACKED)
                }
                wasTracked = tracked
            }
        } catch (error: Exception) {
            // The camera taken by another app, ARCore out of date: no position, and the rotation still works.
            send(UNTRACKED)
        } finally {
            session?.pause()
            session?.close()
            gl.release()
            running = false
        }
    }

    /**
     * ARCore's world is y up; the rotation's is z up, with a heading of its own. Both are level, so
     * they differ by a quarter turn about x and a turn about the vertical, which is measured on
     * every frame from the two rotations of the device.
     */
    private fun toRotationWorld(x: Double, y: Double, z: Double, arRotation: DoubleArray): DoubleArray {
        val up = doubleArrayOf(cos(Math.PI / 4), sin(Math.PI / 4), 0.0, 0.0)
        val arZUp = times(up, arRotation)
        val sensor = rotation.latest() ?: return UNTRACKED
        val between = times(sensor, conjugate(arZUp))
        val heading = 2 * atan2(between[3], between[0])
        // The position, z up, then turned to the rotation's heading.
        val px = x
        val py = -z
        val pz = y
        val c = cos(heading)
        val s = sin(heading)
        return doubleArrayOf(c * px - s * py, s * px + c * py, pz, 1.0)
    }

    companion object {
        const val PERMISSION_REQUEST = 4782
        private val UNTRACKED = doubleArrayOf(0.0, 0.0, 0.0, 0.0)

        private fun conjugate(q: DoubleArray) = doubleArrayOf(q[0], -q[1], -q[2], -q[3])

        private fun times(a: DoubleArray, b: DoubleArray) = doubleArrayOf(
            a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
            a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
            a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
        )
    }
}

/** An OpenGL context with a one-pixel surface, current on the thread that makes it. */
private class OffscreenGl {
    private val display: EGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
    private val context: EGLContext
    private val surface: EGLSurface

    init {
        val version = IntArray(2)
        EGL14.eglInitialize(display, version, 0, version, 1)
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        val wanted = intArrayOf(EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE)
        EGL14.eglChooseConfig(display, wanted, 0, configs, 0, 1, count, 0)
        context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT, intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
        surface = EGL14.eglCreatePbufferSurface(display, configs[0], intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
        EGL14.eglMakeCurrent(display, surface, surface, context)
    }

    fun release() {
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        EGL14.eglDestroySurface(display, surface)
        EGL14.eglDestroyContext(display, context)
        EGL14.eglTerminate(display)
    }
}
