// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * An Audio Unit from a BUNDLE PATH, registered in this process only.
 *
 * WHY NOT THE SYSTEM'S REGISTRY. AudioComponentFindNext lists what is INSTALLED
 * in ~/Library or /Library, so a test that looks a plugin up by its triple
 * tests whatever build happens to be installed -- which, on a machine with Live
 * running, is not the one this checkout just built, and cannot be replaced
 * without touching that session. It also cannot tell two builds apart: every
 * build claims the same triple.
 *
 * So the bundle is loaded from where the build put it, and its factory is
 * handed to AudioComponentRegister -- Apple's API for a component implemented
 * inside the calling process. The registration is visible to this process
 * alone, and it is made under a PRIVATE manufacturer code, so it can never be
 * confused with, or shadow, an installed copy of the same plugin. Nothing is
 * installed and the system registry is not touched.
 *
 * Everything else comes from the bundle's own Info.plist: its type, its
 * subtype, and the name of its factory function (iPlug2 writes all three from
 * config.h). What is loaded is therefore exactly what a host would load from
 * that bundle -- only found by path rather than by name.
 *
 * Header-only: each AU test is one C file.
 */
#ifndef NI_TEST_AU_BUNDLE_H
#define NI_TEST_AU_BUNDLE_H

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

/* The test-only manufacturer. No shipped plugin uses it (theirs is 'Grbe'). */
#define NI_TEST_AU_MANUFACTURER 'NiTs'

static OSType ni_au_fourcc(CFDictionaryRef d, CFStringRef key)
{
    CFTypeRef v = CFDictionaryGetValue(d, key);
    char s[8] = {0};
    if (!v || CFGetTypeID(v) != CFStringGetTypeID() ||
        !CFStringGetCString((CFStringRef) v, s, sizeof s, kCFStringEncodingASCII) ||
        strlen(s) != 4)
        return 0;
    return ((OSType) (unsigned char) s[0] << 24) | ((OSType) (unsigned char) s[1] << 16) |
           ((OSType) (unsigned char) s[2] << 8) | (OSType) (unsigned char) s[3];
}

/*
 * Load the bundle at `path` and register its first AudioComponents entry under
 * the test-only manufacturer. Returns the component, or NULL after saying why
 * on stdout. `desc`, if not NULL, receives the description it was registered
 * under.
 */
static AudioComponent ni_au_register(const char *path, AudioComponentDescription *desc)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8 *) path, (CFIndex) strlen(path), true);
    CFBundleRef bundle = url ? CFBundleCreate(NULL, url) : NULL;
    if (url) CFRelease(url);
    if (!bundle) {
        printf("  FAIL: no bundle at %s -- build it first\n", path);
        return NULL;
    }

    CFArrayRef comps = (CFArrayRef) CFBundleGetValueForInfoDictionaryKey(
        bundle, CFSTR("AudioComponents"));
    if (!comps || CFGetTypeID(comps) != CFArrayGetTypeID() || CFArrayGetCount(comps) < 1) {
        printf("  FAIL: %s declares no AudioComponents\n", path);
        return NULL;
    }
    CFDictionaryRef entry = (CFDictionaryRef) CFArrayGetValueAtIndex(comps, 0);
    CFStringRef factoryName = (CFStringRef) CFDictionaryGetValue(entry, CFSTR("factoryFunction"));
    AudioComponentDescription d = {
        .componentType = ni_au_fourcc(entry, CFSTR("type")),
        .componentSubType = ni_au_fourcc(entry, CFSTR("subtype")),
        .componentManufacturer = NI_TEST_AU_MANUFACTURER,
    };
    if (!d.componentType || !d.componentSubType || !factoryName) {
        printf("  FAIL: %s has an incomplete AudioComponents entry\n", path);
        return NULL;
    }

    /* Loaded through CFBundle, so the plugin finds its own bundle as a host's
     * copy would. */
    CFErrorRef err = NULL;
    if (!CFBundleLoadExecutableAndReturnError(bundle, &err)) {
        printf("  FAIL: %s did not load\n", path);
        if (err) CFRelease(err);
        return NULL;
    }
    AudioComponentFactoryFunction factory =
        (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName(bundle, factoryName);
    if (!factory) {
        printf("  FAIL: %s exports no factory function\n", path);
        return NULL;
    }

    UInt32 version = 0;
    CFNumberRef v = (CFNumberRef) CFDictionaryGetValue(entry, CFSTR("version"));
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue(v, kCFNumberSInt32Type, &version);

    AudioComponent comp = AudioComponentRegister(&d, CFSTR("Neon Ingvy test: in-process copy"),
                                                 version, factory);
    if (!comp) {
        printf("  FAIL: AudioComponentRegister refused %s\n", path);
        return NULL;
    }
    if (desc) *desc = d;
    return comp;
}

#endif /* NI_TEST_AU_BUNDLE_H */
