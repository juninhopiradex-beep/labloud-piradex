// =============================================================================
//  LABLOUD by Piradex — pastas do utilizador
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>

namespace lxpaths
{
// macOS ~/Library/Application Support/Piradex/LABLOUD   ·   Windows %APPDATA%\Piradex\LABLOUD   ·   Linux ~/.config/Piradex/LABLOUD
inline juce::File userDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    auto d = base.getChildFile ("Piradex").getChildFile ("LABLOUD");
    d.createDirectory();
    return d;
}
inline juce::File settingsFile() { return userDir().getChildFile ("settings.xml"); }
}
