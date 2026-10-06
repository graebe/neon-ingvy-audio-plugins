// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The gallery application: one window with every registered page, or, from
 * the command line, every page as a PNG.
 *
 *   "NI UI Gallery" [--page <id or name>]
 *   "NI UI Gallery" --shots <dir> [--scale <n>]
 *
 * --shots renders each page where the window shows it -- on the ground, right
 * of the list -- with JUCE's software renderer at --scale (default 2), writes
 * <dir>/<id>.png (the id is "<group>-<name>", see Gallery.h) and quits. That
 * is how a page is looked at without a screen: the same pixels a snapshot
 * test compares, at Retina size.
 */
#include "Gallery.h"

#include "UvLookAndFeel.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace
{

class Window final : public juce::DocumentWindow
{
public:
    explicit Window (const juce::String& page)
        : juce::DocumentWindow ("NI UI Gallery", uv::tok::colour::bg000, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);

        auto frame = std::make_unique<ni::ui::gallery::Frame> (ni::ui::gallery::pages());
        if (! (page.isNotEmpty() && frame->show (page)) && frame->numPages() > 0)
            frame->show (0);

        setContentOwned (frame.release(), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

/* Every page as <dir>/<id>.png. Returns the number that failed. */
int writeShots (const juce::File& dir, float scale)
{
    if (! dir.createDirectory())
        return 1;

    const auto list = ni::ui::gallery::pages();
    ni::ui::gallery::Frame frame (list);
    int failed = 0;

    for (int i = 0; i < frame.numPages(); ++i)
    {
        frame.show (i);
        const auto* page = frame.page();
        if (page == nullptr)
        {
            ++failed;
            continue;
        }

        const auto image = frame.createComponentSnapshot (page->getBounds(), true, scale,
                                                          juce::SoftwareImageType());
        const auto file = dir.getChildFile (ni::ui::gallery::idOf (list[(size_t) i]) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (image, out))
            ++failed;
        else
            std::cout << file.getFullPathName() << std::endl;
    }
    return failed;
}

class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "NI UI Gallery"; }
    const juce::String getApplicationVersion() override { return uv::tok::release; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        look = std::make_unique<juce::SharedResourcePointer<uv::SharedLookAndFeel>>();

        const auto args = getCommandLineParameterArray();
        const auto valueOf = [&args] (const juce::String& flag)
        {
            const int at = args.indexOf (flag);
            return at >= 0 && at + 1 < args.size() ? args[at + 1] : juce::String();
        };

        const auto shots = valueOf ("--shots");
        if (shots.isNotEmpty())
        {
            const auto scale = valueOf ("--scale");
            const float s = scale.isNotEmpty() ? juce::jmax (0.5f, scale.getFloatValue()) : 2.0f;
            const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (shots);
            setApplicationReturnValue (writeShots (dir, s) == 0 ? 0 : 1);
            quit();
            return;
        }

        window = std::make_unique<Window> (valueOf ("--page"));
    }

    void shutdown() override
    {
        window = nullptr;
        look = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<juce::SharedResourcePointer<uv::SharedLookAndFeel>> look;
    std::unique_ptr<Window> window;
};

} // namespace

START_JUCE_APPLICATION (App)
