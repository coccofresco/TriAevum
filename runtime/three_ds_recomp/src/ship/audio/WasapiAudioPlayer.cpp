#ifdef _WIN32
#include "ship/audio/WasapiAudioPlayer.h"
#include "ship/utils/HResultException.h"
#include <algorithm>
#include <limits>
#include <spdlog/spdlog.h>

// These constants are currently missing from the MinGW headers.
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

namespace Ship {

void WasapiAudioPlayer::ThrowIfFailed(HRESULT res) {
    if (FAILED(res)) {
        throw HResultException(res);
    }
}

bool WasapiAudioPlayer::SetupStream() {
    try {
        ThrowIfFailed(mDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &mDevice));
        ThrowIfFailed(mDevice->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, IID_PPV_ARGS_Helper(&mClient)));

        // Use GetNumOutputChannels() to determine stereo vs surround
        mNumChannels = this->GetNumOutputChannels();

        if (mNumChannels == 2) {
            WAVEFORMATEX desired;
            desired.wFormatTag = WAVE_FORMAT_PCM;
            desired.nChannels = mNumChannels; // Stereo audio
            desired.wBitsPerSample = 16;      // 16-bit audio
            desired.nSamplesPerSec = this->GetSampleRate();
            desired.nBlockAlign = desired.nChannels * desired.wBitsPerSample / 8;
            desired.nAvgBytesPerSec = desired.nSamplesPerSec * desired.nBlockAlign;
            desired.cbSize = 0;

            ThrowIfFailed(mClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                2000000, 0, &desired, nullptr));
        } else if (mNumChannels == 6) {
            // 5.1 surround (6 channels)
            WAVEFORMATEXTENSIBLE desired;
            desired.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            desired.Format.nChannels = mNumChannels; // 6 channels for 5.1 audio
            desired.Format.wBitsPerSample = 16;      // 16-bit audio
            desired.Format.nSamplesPerSec = this->GetSampleRate();
            desired.Format.nBlockAlign = desired.Format.nChannels * desired.Format.wBitsPerSample / 8;
            desired.Format.nAvgBytesPerSec = desired.Format.nSamplesPerSec * desired.Format.nBlockAlign;
            desired.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            desired.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
            desired.Samples.wValidBitsPerSample = 16;
            desired.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

            ThrowIfFailed(mClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                2000000, 0, (WAVEFORMATEX*)&desired, nullptr));
        }

        ThrowIfFailed(mClient->GetBufferSize(&mBufferFrameCount));
        ThrowIfFailed(mClient->GetService(IID_PPV_ARGS(&mRenderClient)));

        mStarted = false;
        mInitialized = true;
    } catch (const HResultException& e) {
        SPDLOG_ERROR("WasapiAudioPlayer::SetupStream failed: {}", e.what());
        return false;
    }

    return true;
}

bool WasapiAudioPlayer::DoInit() {
    try {
        if (!mDeviceEnumerator) {
            ThrowIfFailed(
                CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&mDeviceEnumerator)));
            ThrowIfFailed(mDeviceEnumerator->RegisterEndpointNotificationCallback(this));
        }
    } catch (const HResultException& e) {
        SPDLOG_ERROR("WasapiAudioPlayer::DoInit failed: {}", e.what());
        return false;
    }

    return true;
}

void WasapiAudioPlayer::DoClose() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClient) {
        mClient->Stop();
    }
    mRenderClient.Reset();
    mClient.Reset();
    mDevice.Reset();
    mInitialized = false;
    mStarted = false;
    mPendingSamples.clear();
    mPendingSampleOffset = 0;
}

size_t WasapiAudioPlayer::PendingFrameCount() const {
    if (mNumChannels <= 0 || mPendingSampleOffset >= mPendingSamples.size()) {
        return 0;
    }
    return (mPendingSamples.size() - mPendingSampleOffset) /
           static_cast<size_t>(mNumChannels);
}

void WasapiAudioPlayer::ConsumePendingFrames(size_t frameCount) {
    mPendingSampleOffset += frameCount * static_cast<size_t>(mNumChannels);
    if (mPendingSampleOffset == mPendingSamples.size()) {
        mPendingSamples.clear();
        mPendingSampleOffset = 0;
        return;
    }
    if (mPendingSampleOffset >= 16384U &&
        mPendingSampleOffset * 2U >= mPendingSamples.size()) {
        mPendingSamples.erase(
            mPendingSamples.begin(),
            mPendingSamples.begin() +
                static_cast<std::ptrdiff_t>(mPendingSampleOffset));
        mPendingSampleOffset = 0;
    }
}

int WasapiAudioPlayer::Buffered() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mInitialized) {
        if (!SetupStream()) {
            return 0;
        }
    }
    try {
        UINT32 padding;
        ThrowIfFailed(mClient->GetCurrentPadding(&padding));
        const size_t total = static_cast<size_t>(padding) +
                             PendingFrameCount();
        return static_cast<int32_t>(std::min<size_t>(
            total, static_cast<size_t>(std::numeric_limits<int32_t>::max())));
    } catch (const HResultException& e) {
        SPDLOG_ERROR("WasapiAudioPlayer::Buffered failed: {}", e.what());
        return 0;
    }
}

void WasapiAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mInitialized) {
        if (!SetupStream()) {
            return;
        }
    }
    try {
        const UINT32 inputFrames = static_cast<UINT32>(
            len / (static_cast<size_t>(mNumChannels) * sizeof(int16_t)));
        const size_t inputSamples =
            static_cast<size_t>(inputFrames) * mNumChannels;
        if (inputSamples != 0U) {
            const auto* samples = reinterpret_cast<const int16_t*>(buf);
            mPendingSamples.insert(mPendingSamples.end(), samples,
                                   samples + inputSamples);
        }

        UINT32 padding;
        ThrowIfFailed(mClient->GetCurrentPadding(&padding));

        bool recoveringFromUnderrun = false;
        if (mStarted && padding == 0) {
            ThrowIfFailed(mClient->Stop());
            ThrowIfFailed(mClient->Reset());
            mStarted = false;
            recoveringFromUnderrun = true;
            SPDLOG_WARN("WASAPI output underrun; re-priming the render buffer");
        }

        UINT32 available = mBufferFrameCount - padding;
        const UINT32 frames = static_cast<UINT32>(std::min<size_t>(
            available, PendingFrameCount()));
        if (frames == 0) {
            return;
        }

        BYTE* data;
        ThrowIfFailed(mRenderClient->GetBuffer(frames, &data));
        memcpy(data, mPendingSamples.data() + mPendingSampleOffset,
               static_cast<size_t>(frames) * mNumChannels * sizeof(int16_t));
        if (recoveringFromUnderrun) {
            constexpr UINT32 kRecoveryFadeInFrames = 128;
            const UINT32 fadeFrames =
                std::min(frames, kRecoveryFadeInFrames);
            auto* samples = reinterpret_cast<int16_t*>(data);
            for (UINT32 frame = 0; frame < fadeFrames; ++frame) {
                for (int32_t channel = 0; channel < mNumChannels; ++channel) {
                    const size_t sampleIndex =
                        static_cast<size_t>(frame) * mNumChannels + channel;
                    samples[sampleIndex] = static_cast<int16_t>(
                        static_cast<int32_t>(samples[sampleIndex]) *
                        static_cast<int32_t>(frame + 1) /
                        static_cast<int32_t>(fadeFrames));
                }
            }
        }
        ThrowIfFailed(mRenderClient->ReleaseBuffer(frames, 0));
        ConsumePendingFrames(frames);

        const UINT32 startThreshold = std::min<UINT32>(
            mBufferFrameCount,
            static_cast<UINT32>(std::max(1, GetDesiredBuffered())));
        if (!mStarted && padding + frames >= startThreshold) {
            mStarted = true;
            ThrowIfFailed(mClient->Start());
        }
    } catch (const HResultException& e) { SPDLOG_ERROR("WasapiAudioPlayer::DoPlay failed: {}", e.what()); }
}

void WasapiAudioPlayer::DoFlush() {
    std::lock_guard<std::mutex> lock(mMutex);
    mPendingSamples.clear();
    mPendingSampleOffset = 0;
    if (!mClient) {
        mStarted = false;
        return;
    }
    try {
        if (mStarted) {
            ThrowIfFailed(mClient->Stop());
        }
        ThrowIfFailed(mClient->Reset());
        mStarted = false;
    } catch (const HResultException& e) {
        SPDLOG_ERROR("WasapiAudioPlayer::DoFlush failed: {}", e.what());
    }
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                                    LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eRender && role == eConsole) {
        // This callback runs on a separate thread, so we need to protect mInitialized
        std::lock_guard<std::mutex> lock(mMutex);
        mInitialized = false;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
    return S_OK;
}

ULONG STDMETHODCALLTYPE WasapiAudioPlayer::AddRef() {
    return InterlockedIncrement(&mRefCount);
}

ULONG STDMETHODCALLTYPE WasapiAudioPlayer::Release() {
    ULONG rc = InterlockedDecrement(&mRefCount);
    if (rc == 0) {
        delete this;
    }
    return rc;
}

HRESULT STDMETHODCALLTYPE WasapiAudioPlayer::QueryInterface(REFIID riid, VOID** ppvInterface) {
    if (riid == __uuidof(IUnknown)) {
        AddRef();
        *ppvInterface = (IUnknown*)this;
    } else if (riid == __uuidof(IMMNotificationClient)) {
        AddRef();
        *ppvInterface = (IMMNotificationClient*)this;
    } else {
        *ppvInterface = nullptr;
        return E_NOINTERFACE;
    }
    return S_OK;
}
} // namespace Ship
#endif
