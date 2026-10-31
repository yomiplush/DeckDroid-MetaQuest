package com.droiddeck.launcher.session

import android.net.wifi.WifiManager
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class SessionMulticastTest {
    @Test fun sessionDiscoveryLockIsIdempotentAndReleasedWithSessionLocks() {
        val service = Robolectric.buildService(SessionService::class.java).get()
        val acquire = SessionService::class.java.getDeclaredMethod("acquireLocks").apply { isAccessible = true }
        val release = SessionService::class.java.getDeclaredMethod("releaseLocks").apply { isAccessible = true }
        val field = SessionService::class.java.getDeclaredField("multicastLock").apply { isAccessible = true }
        try {
            acquire.invoke(service)
            val lock = field.get(service) as WifiManager.MulticastLock
            assertTrue(lock.isHeld)
            acquire.invoke(service)
            assertSame(lock, field.get(service))
            release.invoke(service)
            assertFalse(lock.isHeld)
            assertNull(field.get(service))
            release.invoke(service)
        } finally {
            release.invoke(service)
        }
    }
}
