package com.droiddeck.launcher.core

import android.content.Context
import android.content.ContextWrapper
import android.content.res.AssetManager
import androidx.test.core.app.ApplicationProvider
import java.io.File
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream
import org.apache.commons.compress.compressors.zstandard.ZstdCompressorOutputStream
import org.robolectric.annotation.Config
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class AddonPackTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val root get() = File(context.filesDir, "addons/sample")

    @Before fun clean() {
        root.deleteRecursively()
    }

    private fun installed(version: String, complete: Boolean = true, modified: Long = 1_000L): File =
        File(root, version).apply {
            mkdirs()
            File(this, "tool").writeText("x")
            if (complete) File(this, AddonPack.COMPLETE).writeText(version)
            setLastModified(modified)
        }

    private fun packBytes(): ByteArray {
        val bytes = ByteArrayOutputStream()
        ZstdCompressorOutputStream(bytes).use { zstd ->
            TarArchiveOutputStream(zstd).use { tar ->
                val contents = "embedded tool".toByteArray()
                tar.putArchiveEntry(TarArchiveEntry("tool").apply { size = contents.size.toLong() })
                tar.write(contents)
                tar.closeArchiveEntry()
            }
        }
        return bytes.toByteArray()
    }

    private fun assetContext(bytes: ByteArray): Context {
        val zip = File.createTempFile("addon-assets", ".apk", context.cacheDir)
        ZipOutputStream(zip.outputStream()).use { out ->
            out.putNextEntry(ZipEntry("assets/addon-packs/sample.tar.zst"))
            out.write(bytes)
            out.closeEntry()
        }
        val assets = AssetManager::class.java.getDeclaredConstructor().newInstance()
        val cookie = AssetManager::class.java.getDeclaredMethod("addAssetPath", String::class.java)
            .invoke(assets, zip.absolutePath) as Int
        assertTrue(cookie != 0)
        return object : ContextWrapper(context) {
            override fun getAssets() = assets
            override fun getApplicationContext(): Context = this
        }
    }

    @Test(timeout = 10000) fun embeddedPackInstallsWithoutPublishingADownload() {
        val bytes = packBytes()
        val archive = File(context.cacheDir, "embedded-pack.tar.zst").apply { writeBytes(bytes) }
        val pack = AddonPack("sample", Hashes.sha256(archive), bytes.size.toLong(), listOf("tool"))
        val local = assetContext(bytes)
        assertEquals(true, pack.copyEmbeddedArchive(local, File(root, "copied.tar.zst")))
        assertTrue(pack.await(local))
        assertEquals("embedded tool", pack.file(local, "tool").readText())
        assertTrue(pack.installed(local))
    }

    @Test fun corruptedEmbeddedPackIsRejectedAndRemoved() {
        val archive = File(root, "bad.tar.zst")
        val pack = AddonPack("sample", "a".repeat(64), 3L, listOf("tool"))
        assertEquals(false, pack.copyEmbeddedArchive(assetContext(byteArrayOf(1, 2, 3)), archive))
        assertFalse(archive.exists())
        assertFalse(pack.installed(context))
    }

    @Test fun unpinnedLocalBuildReusesTheNewestCompletePack() {
        installed("a".repeat(64), modified = 1_000L)
        installed("b".repeat(64), modified = 2_000L)
        installed("c".repeat(64), complete = false, modified = 3_000L)
        val pack = AddonPack("sample", "", 0L, listOf("tool"), reuseDeviceCopy = true)

        assertTrue(pack.installed(context))
        assertEquals("b".repeat(64), pack.dir(context).name)
    }

    @Test fun unpinnedLocalBuildWithNothingInstalledHasNoPack() {
        assertFalse(AddonPack("sample", "", 0L, listOf("tool"), reuseDeviceCopy = true).installed(context))
    }

    @Test fun pinnedBuildIgnoresOtherInstalledVersions() {
        installed("b".repeat(64))
        val pack = AddonPack("sample", "a".repeat(64), 1L, listOf("tool"))

        assertFalse(pack.installed(context))
        assertEquals("a".repeat(64), pack.dir(context).name)
    }

    @Test fun ciBuildWithoutAPinNeverReuses() {
        installed("b".repeat(64))
        assertFalse(AddonPack("sample", "", 0L, listOf("tool"), reuseDeviceCopy = false).installed(context))
    }
}
