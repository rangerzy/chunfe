// Exercise miniaudio's real CoreAudio enumeration with two synthetic duplex
// devices. No physical devices or system default changes are required.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stdio.h>
#include <string.h>

static const AudioObjectID firstDevice = 100;
static const AudioObjectID secondDevice = 200;
static AudioObjectID defaultOutput;
static AudioObjectID defaultInput;

static OSStatus propertySize(AudioObjectID object, const AudioObjectPropertyAddress* address,
                             UInt32 qualifierSize, const void* qualifier, UInt32* size)
{
    (void)object; (void)qualifierSize; (void)qualifier;
    if (address->mSelector == kAudioHardwarePropertyDevices) {
        *size = 2 * sizeof(AudioObjectID);
        return noErr;
    }
    if (address->mSelector == kAudioDevicePropertyStreamConfiguration) {
        *size = sizeof(AudioBufferList);
        return noErr;
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus propertyData(AudioObjectID object, const AudioObjectPropertyAddress* address,
                             UInt32 qualifierSize, const void* qualifier, UInt32* size, void* data)
{
    (void)qualifierSize; (void)qualifier; (void)size;
    switch (address->mSelector) {
    case kAudioHardwarePropertyDevices: {
        AudioObjectID* devices = (AudioObjectID*)data;
        devices[0] = firstDevice;
        devices[1] = secondDevice;
        return noErr;
    }
    case kAudioHardwarePropertyDefaultOutputDevice:
        *(AudioObjectID*)data = defaultOutput;
        return noErr;
    case kAudioHardwarePropertyDefaultInputDevice:
        *(AudioObjectID*)data = defaultInput;
        return noErr;
    case kAudioDevicePropertyDeviceUID:
    case kAudioDevicePropertyDeviceNameCFString:
        *(CFStringRef*)data = object == firstDevice ? CFSTR("Device A") : CFSTR("Device B");
        return noErr;
    case kAudioDevicePropertyStreamConfiguration: {
        AudioBufferList* buffers = (AudioBufferList*)data;
        memset(buffers, 0, sizeof(*buffers));
        buffers->mNumberBuffers = 1;
        buffers->mBuffers[0].mNumberChannels = 2;
        return noErr;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static void releaseString(CFTypeRef value) { (void)value; }

typedef struct {
    int count;
    int errors;
} Results;

static ma_bool32 checkDevice(ma_context* context, ma_device_type type,
                             const ma_device_info* info, void* userData)
{
    Results* results = (Results*)userData;
    const AudioObjectID id = strcmp(info->name, "Device A") == 0 ? firstDevice : secondDevice;
    const ma_bool32 expected = id == (type == ma_device_type_playback ? defaultOutput : defaultInput);
    (void)context;
    ++results->count;
    if (info->isDefault != expected) {
        fprintf(stderr, "%s %s: default=%d, expected=%d\n", info->name,
                type == ma_device_type_playback ? "output" : "input", info->isDefault, expected);
        ++results->errors;
    }
    return MA_TRUE;
}

static int checkDefaults(AudioObjectID output, AudioObjectID input)
{
    ma_context context;
    Results results = {0, 0};
    memset(&context, 0, sizeof(context));
    context.allocationCallbacks = ma_allocation_callbacks_init_default();
    context.coreaudio.AudioObjectGetPropertyDataSize = (ma_proc)propertySize;
    context.coreaudio.AudioObjectGetPropertyData = (ma_proc)propertyData;
    context.coreaudio.CFStringGetCString = (ma_proc)CFStringGetCString;
    context.coreaudio.CFRelease = (ma_proc)releaseString;
    defaultOutput = output;
    defaultInput = input;
    if (ma_context_enumerate_devices__coreaudio(&context, checkDevice, &results) != MA_SUCCESS ||
        results.count != 4 || results.errors != 0)
        return 1;
    return 0;
}

int main(void)
{
    return checkDefaults(firstDevice, secondDevice) ||
           checkDefaults(secondDevice, firstDevice) ||
           checkDefaults(firstDevice, firstDevice);
}
