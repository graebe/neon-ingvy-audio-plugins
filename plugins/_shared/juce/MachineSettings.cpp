// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The per-machine settings file. MachineSettings.h says what is in it.
 */
#include "MachineSettings.h"

#include <juce_data_structures/juce_data_structures.h>

namespace ni
{

namespace
{
/* One file for every instance in the process, opened once. */
struct Store
{
    Store() : file (options()) {}

    static juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "NI Plugins";
        o.folderName = "Neon Ingvy";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }

    juce::PropertiesFile file;
};

juce::String motionKey (const char* product)
{
    return "motion." + juce::String (product);
}
} // namespace

bool MachineSettings::motion (const char* product)
{
    const juce::SharedResourcePointer<Store> store;
    return store->file.getBoolValue (motionKey (product), true);
}

void MachineSettings::setMotion (const char* product, bool on)
{
    const juce::SharedResourcePointer<Store> store;
    store->file.setValue (motionKey (product), on);
    store->file.saveIfNeeded();
}

} // namespace ni
