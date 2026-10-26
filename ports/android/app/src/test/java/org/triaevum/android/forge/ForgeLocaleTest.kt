package org.triaevum.android.forge

import org.junit.Assert.assertEquals
import org.junit.Test

/** Owner decision 8, the same cases as forge-desktop's `choose_language`. */
class ForgeLocaleTest {
    @Test
    fun beforeAnyRomTheSystemLanguageIfASupportedRomHasIt() {
        assertEquals("it", ForgeLocale.choose(emptyList(), listOf(null, "it")))
        assertEquals("en", ForgeLocale.choose(emptyList(), listOf(null, "ja")))
        assertEquals("en", ForgeLocale.choose(emptyList(), listOf(null, "nl")))
        assertEquals(ForgeLocale.SUPPORTED_ROM_LANGUAGES, ForgeLocale.offered(emptyList()))
    }

    @Test
    fun theRomLanguagesDecide() {
        val usa = listOf("en", "fr", "es")
        // The kept choice first, then the system language, then English.
        assertEquals("es", ForgeLocale.choose(usa, listOf("es", "fr")))
        assertEquals("fr", ForgeLocale.choose(usa, listOf("it", "fr")))
        assertEquals("en", ForgeLocale.choose(usa, listOf("it", "de")))
        // A kept choice the ROM lacks is skipped, not forgotten: another ROM uses it again.
        assertEquals("it", ForgeLocale.choose(listOf("en", "fr", "de", "es", "it", "nl"), listOf("it", "fr")))
        // Without English, the ROM's first language.
        assertEquals("de", ForgeLocale.choose(listOf("de", "nl"), listOf("it", "ja")))
        assertEquals(usa, ForgeLocale.offered(usa))
    }
}
