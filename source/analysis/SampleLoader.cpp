/**
 * SampleLoader.cpp — Audio File Loading Implementation
 * =====================================================
 *
 * Uses Apple's ExtAudioFile API from AudioToolbox to load any audio format
 * macOS supports (WAV, AIFF, FLAC, MP3, M4A, CAF, etc.) and convert it
 * to our internal format: interleaved 64-bit floating-point samples.
 *
 * The ExtAudioFile API is a high-level wrapper around AudioFile that adds
 * automatic sample rate conversion and format conversion. We tell it we
 * want Float64 output and it handles the rest, regardless of whether the
 * source file is 16-bit WAV, 24-bit AIFF, or compressed AAC.
 */

#include "SampleLoader.h"

// AudioToolbox provides ExtAudioFile — Apple's high-level audio file API.
// It's part of macOS and doesn't require any additional installation.
#include <AudioToolbox/AudioToolbox.h>

#include <iostream>  // For error logging to console

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SampleLoader::SampleLoader()
    : numFrames(0)
    , numChannels(0)
    , sampleRate(0.0)
{
    // Nothing to initialize — the vector starts empty, and isLoaded() returns false.
}

SampleLoader::~SampleLoader()
{
    // std::vector cleans up its own memory automatically.
}

// ============================================================================
// loadFromFile
// ============================================================================

bool SampleLoader::loadFromFile(const std::string& path)
{
    // Step 1: Clear any previously loaded data so we start fresh.
    clear();

    // Step 2: Convert the file path string to a CFURLRef.
    // Apple's audio APIs work with CFURLRef (Core Foundation URL) rather than
    // plain C strings. We create one from our std::string path.
    CFStringRef cfPath = CFStringCreateWithCString(
        kCFAllocatorDefault,    // Use the default memory allocator
        path.c_str(),           // Our C-string file path
        kCFStringEncodingUTF8   // UTF-8 encoding for the string
    );

    if (!cfPath)
    {
        std::cerr << "[SampleLoader] Failed to create CFString from path: " << path << std::endl;
        return false;
    }

    // Create a file URL from the path string.
    // The 'false' means this is a file path, not a directory.
    CFURLRef fileURL = CFURLCreateWithFileSystemPath(
        kCFAllocatorDefault,            // Default allocator
        cfPath,                         // The path string
        kCFURLPOSIXPathStyle,           // Unix-style path (with forward slashes)
        false                           // Not a directory
    );

    // We're done with the CFString — release it to avoid memory leaks.
    // Core Foundation uses manual reference counting (like old-school Objective-C).
    CFRelease(cfPath);

    if (!fileURL)
    {
        std::cerr << "[SampleLoader] Failed to create URL from path: " << path << std::endl;
        return false;
    }

    // Step 3: Open the audio file using ExtAudioFile.
    // This is the high-level API that handles format detection and decoding.
    ExtAudioFileRef audioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL(fileURL, &audioFile);

    // Done with the URL — release it.
    CFRelease(fileURL);

    if (status != noErr || !audioFile)
    {
        std::cerr << "[SampleLoader] Failed to open audio file: " << path
                  << " (error code: " << status << ")" << std::endl;
        return false;
    }

    // Step 4: Read the file's native format to learn its sample rate and channel count.
    // AudioStreamBasicDescription (ASBD) is Apple's struct that describes an audio
    // format — sample rate, bit depth, channel count, encoding, etc.
    AudioStreamBasicDescription fileFormat = {};
    UInt32 propSize = sizeof(fileFormat);
    status = ExtAudioFileGetProperty(
        audioFile,
        kExtAudioFileProperty_FileDataFormat,   // We want the source file's format
        &propSize,
        &fileFormat
    );

    if (status != noErr)
    {
        std::cerr << "[SampleLoader] Failed to read file format: " << path << std::endl;
        ExtAudioFileDispose(audioFile);
        return false;
    }

    // Store the original sample rate and channel count.
    sampleRate = fileFormat.mSampleRate;
    numChannels = static_cast<int>(fileFormat.mChannelsPerFrame);

    // Step 5: Tell ExtAudioFile what format we WANT the data in.
    // We want: 64-bit floating-point, interleaved, native byte order.
    // ExtAudioFile will automatically convert from whatever the file actually
    // contains (16-bit int, 24-bit, compressed AAC, etc.) to our requested format.
    AudioStreamBasicDescription clientFormat = {};
    clientFormat.mSampleRate       = sampleRate;                    // Keep original sample rate
    clientFormat.mFormatID         = kAudioFormatLinearPCM;         // Uncompressed PCM
    clientFormat.mFormatFlags      = kAudioFormatFlagIsFloat        // 64-bit float
                                   | kAudioFormatFlagIsNonInterleaved * 0  // We want interleaved
                                   | kAudioFormatFlagIsPacked;      // No padding between samples
    clientFormat.mBitsPerChannel   = 64;                            // 64-bit (double precision)
    clientFormat.mChannelsPerFrame = static_cast<UInt32>(numChannels);
    clientFormat.mFramesPerPacket  = 1;                             // PCM always has 1 frame/packet
    clientFormat.mBytesPerFrame    = static_cast<UInt32>(sizeof(double) * numChannels);
    clientFormat.mBytesPerPacket   = clientFormat.mBytesPerFrame;   // Same as bytes/frame for PCM

    status = ExtAudioFileSetProperty(
        audioFile,
        kExtAudioFileProperty_ClientDataFormat,  // Set the OUTPUT format (what we read)
        sizeof(clientFormat),
        &clientFormat
    );

    if (status != noErr)
    {
        std::cerr << "[SampleLoader] Failed to set client format: " << path << std::endl;
        ExtAudioFileDispose(audioFile);
        return false;
    }

    // Step 6: Get the total number of frames in the file.
    // We need this to allocate the right amount of memory.
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    status = ExtAudioFileGetProperty(
        audioFile,
        kExtAudioFileProperty_FileLengthFrames,  // Total frame count
        &propSize,
        &totalFrames
    );

    if (status != noErr || totalFrames <= 0)
    {
        std::cerr << "[SampleLoader] Failed to get frame count: " << path << std::endl;
        ExtAudioFileDispose(audioFile);
        return false;
    }

    numFrames = static_cast<int64_t>(totalFrames);

    // Step 7: Allocate memory and read all the audio data.
    // Total samples = frames * channels (because interleaved).
    sampleData.resize(static_cast<size_t>(numFrames * numChannels));

    // AudioBufferList is Apple's struct for passing audio data around.
    // It contains one or more AudioBuffer structs, each pointing to a
    // block of sample data. For interleaved audio, we use a single buffer.
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = static_cast<UInt32>(numChannels);
    bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(sampleData.size() * sizeof(double));
    bufferList.mBuffers[0].mData = sampleData.data();

    // Read all frames at once. ioNumFrames is both input (how many we want)
    // and output (how many we actually got).
    UInt32 framesToRead = static_cast<UInt32>(numFrames);
    status = ExtAudioFileRead(audioFile, &framesToRead, &bufferList);

    // Close the file — we've read everything into memory.
    ExtAudioFileDispose(audioFile);

    if (status != noErr)
    {
        std::cerr << "[SampleLoader] Failed to read audio data: " << path << std::endl;
        clear();
        return false;
    }

    // The actual number of frames read might differ from what we requested
    // (e.g., for variable-rate compressed formats). Update our count.
    if (framesToRead != static_cast<UInt32>(numFrames))
    {
        numFrames = static_cast<int64_t>(framesToRead);
        sampleData.resize(static_cast<size_t>(numFrames * numChannels));
    }

    // Remember the file path for state save/restore.
    filePath = path;

    std::cout << "[SampleLoader] Loaded: " << path << std::endl;
    std::cout << "  Frames: " << numFrames
              << ", Channels: " << numChannels
              << ", Sample Rate: " << sampleRate << " Hz"
              << ", Duration: " << (numFrames / sampleRate) << " seconds"
              << std::endl;

    return true;
}

// ============================================================================
// clear
// ============================================================================

void SampleLoader::clear()
{
    // Release all sample data and reset to initial state.
    sampleData.clear();

    // shrink_to_fit() tells the vector to actually free its memory.
    // Without this, clear() might keep the memory allocated for reuse.
    sampleData.shrink_to_fit();

    numFrames = 0;
    numChannels = 0;
    sampleRate = 0.0;
    filePath.clear();
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
