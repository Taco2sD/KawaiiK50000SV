/**
 * SampleLoader.h — Audio File Loading
 * =====================================
 *
 * This class loads an audio file (WAV, AIFF, FLAC, MP3, M4A, etc.) from disk
 * into memory as raw floating-point sample data. It uses Apple's AudioToolbox
 * framework (ExtAudioFile API), which is built into macOS and supports every
 * common audio format natively — no third-party libraries needed.
 *
 * HOW AUDIO FILES WORK (briefly):
 *   Audio is a stream of numbers ("samples") representing air pressure at
 *   evenly-spaced points in time. A CD uses 44,100 samples per second
 *   (44.1 kHz) with 16-bit integers. We convert everything to 64-bit
 *   floating-point (double) because that's what our synthesis engine uses.
 *
 *   Stereo audio has two channels — left and right — interleaved in memory:
 *   [L0, R0, L1, R1, L2, R2, ...]
 *
 * USAGE:
 *   SampleLoader loader;
 *   if (loader.loadFromFile("/path/to/sample.wav")) {
 *       // loader.getSampleData() -> pointer to interleaved float64 samples
 *       // loader.getNumFrames()  -> number of sample frames (not total samples!)
 *       // loader.getNumChannels() -> 1 for mono, 2 for stereo
 *       // loader.getSampleRate()  -> e.g. 44100.0
 *   }
 *
 * FRAME vs SAMPLE:
 *   A "frame" is one sample per channel. In stereo, 1 frame = 2 samples.
 *   100 frames of stereo audio = 200 individual sample values.
 *   We use "frame" to avoid ambiguity.
 */

#pragma once

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

class SampleLoader
{
public:
    SampleLoader();
    ~SampleLoader();

    /**
     * Load an audio file from the given file path.
     *
     * This reads the entire file into memory as interleaved 64-bit floating-point
     * samples, regardless of the original format. The AudioToolbox framework
     * handles all decoding (WAV, AIFF, FLAC, MP3, AAC, etc.) transparently.
     *
     * @param filePath  Absolute path to the audio file (e.g., "/Users/me/song.wav")
     * @return          true if the file was loaded successfully, false on error
     */
    bool loadFromFile(const std::string& filePath);

    /**
     * Clear all loaded sample data and reset to empty state.
     */
    void clear();

    // --- Accessors ---

    /** Pointer to the raw interleaved sample data (L0, R0, L1, R1, ...) */
    const double* getSampleData() const { return sampleData.data(); }

    /** Number of sample frames (divide total samples by channel count to get this) */
    int64_t getNumFrames() const { return numFrames; }

    /** Number of audio channels (1 = mono, 2 = stereo) */
    int getNumChannels() const { return numChannels; }

    /** Sample rate of the loaded audio in Hz (e.g., 44100.0) */
    double getSampleRate() const { return sampleRate; }

    /** True if a sample is currently loaded and ready to play */
    bool isLoaded() const { return !sampleData.empty(); }

    /** The file path of the currently loaded sample (empty if none) */
    const std::string& getFilePath() const { return filePath; }

private:
    // The actual sample data, stored as interleaved doubles.
    // For stereo: [L0, R0, L1, R1, L2, R2, ...]
    // For mono:   [S0, S1, S2, ...]
    std::vector<double> sampleData;

    int64_t numFrames;      // Number of sample frames
    int     numChannels;    // Number of channels (1 or 2)
    double  sampleRate;     // Original sample rate in Hz
    std::string filePath;   // Path to the loaded file (for state save/restore)
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
