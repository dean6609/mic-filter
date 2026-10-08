#pragma once
// Minimal declarations of the documented Windows audio processing object (APO) ABI.
// Written for this project from Microsoft's public API reference so the repository does
// not redistribute Windows SDK headers. Only what MicFilter uses is declared; method order
// and struct layouts must match Windows exactly.
// https://learn.microsoft.com/windows-hardware/drivers/audio/implementing-audio-processing-objects
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>
#include <mmreg.h>
#include <unknwn.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#include <audioapotypes.h> // MinGW: HNSTIME, APO_BUFFER_FLAGS, APO_CONNECTION_PROPERTY

#define APOERR_ALREADY_INITIALIZED          static_cast<HRESULT>(0x887D0001L)
#define APOERR_NOT_INITIALIZED              static_cast<HRESULT>(0x887D0002L)
#define APOERR_FORMAT_NOT_SUPPORTED         static_cast<HRESULT>(0x887D0003L)
#define APOERR_ALREADY_UNLOCKED             static_cast<HRESULT>(0x887D0006L)
#define APOERR_NUM_CONNECTIONS_INVALID      static_cast<HRESULT>(0x887D0007L)
#define APOERR_INVALID_OUTPUT_MAXFRAMECOUNT static_cast<HRESULT>(0x887D0008L)
#define APOERR_INVALID_CONNECTION_FORMAT    static_cast<HRESULT>(0x887D0009L)
#define APOERR_APO_LOCKED                   static_cast<HRESULT>(0x887D000AL)

#define APO_CONNECTION_DESCRIPTOR_SIGNATURE 'ACDS'
#define APO_CONNECTION_PROPERTY_SIGNATURE   'ACPS'

struct UNCOMPRESSEDAUDIOFORMAT {
    GUID guidFormatType;
    DWORD dwSamplesPerFrame;
    DWORD dwBytesPerSampleContainer;
    DWORD dwValidBitsPerSample;
    FLOAT fFramesPerSecond;
    DWORD dwChannelMask;
};

struct IAudioMediaType : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL* compressed) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType* other, DWORD* flags) = 0;
    virtual const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* format) = 0;
};

enum APO_CONNECTION_BUFFER_TYPE {
    APO_CONNECTION_BUFFER_TYPE_ALLOCATED = 0,
    APO_CONNECTION_BUFFER_TYPE_EXTERNAL = 1,
    APO_CONNECTION_BUFFER_TYPE_DEPENDANT = 2
};

struct APO_CONNECTION_DESCRIPTOR {
    APO_CONNECTION_BUFFER_TYPE Type;
    UINT_PTR pBuffer;
    UINT32 u32MaxFrameCount;
    IAudioMediaType* pFormat;
    UINT32 u32Signature;
};

enum APO_FLAG {
    APO_FLAG_NONE = 0,
    APO_FLAG_INPLACE = 0x1,
    APO_FLAG_SAMPLESPERFRAME_MUST_MATCH = 0x2,
    APO_FLAG_FRAMESPERSECOND_MUST_MATCH = 0x4,
    APO_FLAG_BITSPERSAMPLE_MUST_MATCH = 0x8,
    APO_FLAG_DEFAULT = 0x2 | 0x4 | 0x8
};

struct APO_REG_PROPERTIES {
    CLSID clsid;
    APO_FLAG Flags;
    WCHAR szFriendlyName[256];
    WCHAR szCopyrightInfo[256];
    UINT32 u32MajorVersion;
    UINT32 u32MinorVersion;
    UINT32 u32MinInputConnections;
    UINT32 u32MaxInputConnections;
    UINT32 u32MinOutputConnections;
    UINT32 u32MaxOutputConnections;
    UINT32 u32MaxInstances;
    UINT32 u32NumAPOInterfaces;
    IID iidAPOInterfaceList[1];
};

struct IAudioProcessingObjectRT : public IUnknown {
    virtual void STDMETHODCALLTYPE APOProcess(UINT32 inputs, APO_CONNECTION_PROPERTY** in,
                                              UINT32 outputs, APO_CONNECTION_PROPERTY** out) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 outputFrames) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 inputFrames) = 0;
};

struct IAudioProcessingObjectConfiguration : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputs, APO_CONNECTION_DESCRIPTOR** in,
                                                     UINT32 outputs, APO_CONNECTION_DESCRIPTOR** out) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockForProcess() = 0;
};

struct IAudioProcessingObject : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* time) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** properties) = 0;
    virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32 bytes, BYTE* data) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* requested,
                                                             IAudioMediaType** supported) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* opposite, IAudioMediaType* requested,
                                                              IAudioMediaType** supported) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* count) = 0;
};

struct IAudioSystemEffects : public IUnknown {};

// Public initialization context, from Microsoft's APOInitSystemEffects/2 reference.
struct APOInitBaseStruct {UINT32 cbSize;CLSID clsid;};
struct APOInitSystemEffects {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
};
struct APOInitSystemEffects2 {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
    UINT nSoftwareIoDeviceInCollection,nSoftwareIoConnectorIndex;
    GUID AudioProcessingMode;
    BOOL InitializeForDiscoveryOnly;
};
static_assert(sizeof(APOInitSystemEffects)==56 && offsetof(APOInitSystemEffects,pAPOEndpointProperties)==24);
static_assert(sizeof(APOInitSystemEffects2)==88);

// MinGW's __uuidof needs an explicit GUID for every interface it is used with.
__CRT_UUID_DECL(IAudioProcessingObject,0xfd7f2b29,0x24d0,0x4b5c,0xb1,0x77,0x59,0x2c,0x39,0xf9,0xca,0x10)
__CRT_UUID_DECL(IAudioProcessingObjectRT,0x9e1d6a6d,0xddbc,0x4e95,0xa4,0xc7,0xad,0x64,0xba,0x37,0x84,0x6c)
__CRT_UUID_DECL(IAudioProcessingObjectConfiguration,0x0e5ed805,0xaba6,0x49c3,0x8f,0x9a,0x2b,0x8c,0x88,0x9c,0x4f,0xa8)
__CRT_UUID_DECL(IAudioSystemEffects,0x5fa00f27,0xadd6,0x499a,0x8a,0x9d,0x6b,0x98,0x52,0x1f,0xa7,0x5b)
__CRT_UUID_DECL(IAudioMediaType,0x4e997f73,0xb71f,0x4798,0x87,0x3b,0xed,0x7d,0xfc,0xf1,0x5b,0x4d)

// Layout checks against the Windows SDK definitions (x64).
static_assert(sizeof(UNCOMPRESSEDAUDIOFORMAT)==36);
static_assert(sizeof(APO_CONNECTION_DESCRIPTOR)==40 && offsetof(APO_CONNECTION_DESCRIPTOR,pFormat)==24);
static_assert(sizeof(APO_REG_PROPERTIES)==1092 && offsetof(APO_REG_PROPERTIES,u32MajorVersion)==1044);
static_assert(sizeof(APO_CONNECTION_PROPERTY)==24);
