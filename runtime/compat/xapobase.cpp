// SPDX-License-Identifier: GPL-3.0-or-later
// Part of RXDK-360 - see LICENSE.md for the full GNU GPL v3.
//
// RXDK-360 compat library: a clean reimplementation of the XDK's XAPO base
// classes (CXAPOBase, CXAPOParametersBase) from the public XAPOBase.h/XAPO.h
// contract. The XDK ships these only as a prebuilt MSVC library (XAPOBase.lib,
// no source); a title that subclasses them - AdvancedXMic, HeadsetAudio,
// XAudio2CustomAPO - is compiled by clang and so needs the base's *Itanium* C++
// object model (vtable layout, RTTI `_ZTI...`, this-adjusting `_ZThn...` thunks,
// mangled member names). A format translation of the MSVC .lib cannot supply
// that - only recompilation can. So we build the base here, with clang, into the
// compat library we ship, and titles link it in place of XAPOBase.lib.
//
// Behaviour follows the documented XAPO base semantics (the same class the PC
// DirectX SDK ships in source form, marked "Redistributable Code" in the XDK's
// include\xbox\.rights): default-format validation against XAPOBASE_DEFAULT_*,
// and a lock-free triple-buffered parameter handoff for CXAPOParametersBase.
#include <XAPOBase.h>
#include <string.h>

// The default format the base advertises: 32-bit float, channel/framerate within
// the XAPO limits (XAPOBase.h's XAPOBASE_DEFAULT_* + IsInput/IsOutputFormatSupported).
static bool IsDefaultFormat(const WAVEFORMATEX* p)
{
    if (p == NULL) return false;
    UINT32 tag = p->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* pe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(p);
        tag = pe->SubFormat.Data1;   // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.Data1 == WAVE_FORMAT_IEEE_FLOAT
    }
    return tag == XAPOBASE_DEFAULT_FORMAT_TAG
        && p->wBitsPerSample == XAPOBASE_DEFAULT_FORMAT_BITSPERSAMPLE
        && p->nChannels     >= XAPOBASE_DEFAULT_FORMAT_MIN_CHANNELS
        && p->nChannels     <= XAPOBASE_DEFAULT_FORMAT_MAX_CHANNELS
        && p->nSamplesPerSec >= XAPOBASE_DEFAULT_FORMAT_MIN_FRAMERATE
        && p->nSamplesPerSec <= XAPOBASE_DEFAULT_FORMAT_MAX_FRAMERATE;
}


//--------------------------------------------------------------------------//
//  CXAPOBase
//--------------------------------------------------------------------------//
CXAPOBase::CXAPOBase(const XAPO_REGISTRATION_PROPERTIES* pRegistrationProperties)
    : m_pRegistrationProperties(pRegistrationProperties)
    , m_pfnMatrixMixFunction(NULL)
    , m_pfl32MatrixCoefficients(NULL)
    , m_nSrcFormatType(0)
    , m_fIsScalarMatrix(TRUE)
    , m_fIsLocked(FALSE)
    , m_lReferenceCount(1)
{
}

CXAPOBase::~CXAPOBase()
{
    if (m_pfl32MatrixCoefficients != NULL) {
        XAPOFree(m_pfl32MatrixCoefficients);
        m_pfl32MatrixCoefficients = NULL;
    }
}

HRESULT CXAPOBase::GetRegistrationProperties(XAPO_REGISTRATION_PROPERTIES** ppRegistrationProperties)
{
    if (ppRegistrationProperties == NULL) return E_POINTER;
    XAPO_REGISTRATION_PROPERTIES* pCopy =
        reinterpret_cast<XAPO_REGISTRATION_PROPERTIES*>(XAPOAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
    if (pCopy == NULL) return E_OUTOFMEMORY;
    memcpy(pCopy, m_pRegistrationProperties, sizeof(XAPO_REGISTRATION_PROPERTIES));
    *ppRegistrationProperties = pCopy;
    return S_OK;
}

HRESULT CXAPOBase::ValidateFormatDefault(WAVEFORMATEX* pFormat, BOOL fOverwrite)
{
    if (pFormat == NULL) return E_POINTER;
    if (IsDefaultFormat(pFormat)) return S_OK;
    if (fOverwrite) {
        // Overwrite with the nearest supported format: 32-bit float, keeping the
        // requested channel count / framerate clamped to the supported range.
        pFormat->wFormatTag      = XAPOBASE_DEFAULT_FORMAT_TAG;
        pFormat->wBitsPerSample  = XAPOBASE_DEFAULT_FORMAT_BITSPERSAMPLE;
        if (pFormat->nChannels < XAPOBASE_DEFAULT_FORMAT_MIN_CHANNELS)
            pFormat->nChannels = XAPOBASE_DEFAULT_FORMAT_MIN_CHANNELS;
        else if (pFormat->nChannels > XAPOBASE_DEFAULT_FORMAT_MAX_CHANNELS)
            pFormat->nChannels = XAPOBASE_DEFAULT_FORMAT_MAX_CHANNELS;
        if (pFormat->nSamplesPerSec < XAPOBASE_DEFAULT_FORMAT_MIN_FRAMERATE)
            pFormat->nSamplesPerSec = XAPOBASE_DEFAULT_FORMAT_MIN_FRAMERATE;
        else if (pFormat->nSamplesPerSec > XAPOBASE_DEFAULT_FORMAT_MAX_FRAMERATE)
            pFormat->nSamplesPerSec = XAPOBASE_DEFAULT_FORMAT_MAX_FRAMERATE;
        pFormat->nBlockAlign     = (WORD)(pFormat->nChannels * (pFormat->wBitsPerSample / 8));
        pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;
        pFormat->cbSize          = 0;
    }
    return XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT CXAPOBase::ValidateFormatPair(const WAVEFORMATEX* pSupportedFormat,
                                      WAVEFORMATEX* pRequestedFormat, BOOL fOverwrite)
{
    if (pSupportedFormat == NULL || pRequestedFormat == NULL) return E_POINTER;
    HRESULT hr = ValidateFormatDefault(pRequestedFormat, fOverwrite);
    if (FAILED(hr) || hr == XAPO_E_FORMAT_UNSUPPORTED) return hr;

    const UINT32 flags = m_pRegistrationProperties->Flags;
    bool ok = true;
    if (flags & XAPO_FLAG_CHANNELS_MUST_MATCH)
        ok = ok && (pSupportedFormat->nChannels == pRequestedFormat->nChannels);
    if (flags & XAPO_FLAG_FRAMERATE_MUST_MATCH)
        ok = ok && (pSupportedFormat->nSamplesPerSec == pRequestedFormat->nSamplesPerSec);
    if (flags & XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH)
        ok = ok && (pSupportedFormat->wBitsPerSample == pRequestedFormat->wBitsPerSample);
    if (ok) return S_OK;
    if (fOverwrite) {
        if (flags & XAPO_FLAG_CHANNELS_MUST_MATCH)
            pRequestedFormat->nChannels = pSupportedFormat->nChannels;
        if (flags & XAPO_FLAG_FRAMERATE_MUST_MATCH)
            pRequestedFormat->nSamplesPerSec = pSupportedFormat->nSamplesPerSec;
        if (flags & XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH)
            pRequestedFormat->wBitsPerSample = pSupportedFormat->wBitsPerSample;
        pRequestedFormat->nBlockAlign     = (WORD)(pRequestedFormat->nChannels * (pRequestedFormat->wBitsPerSample / 8));
        pRequestedFormat->nAvgBytesPerSec = pRequestedFormat->nSamplesPerSec * pRequestedFormat->nBlockAlign;
    }
    return XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT CXAPOBase::IsInputFormatSupported(const WAVEFORMATEX* /*pOutputFormat*/,
    const WAVEFORMATEX* pRequestedInputFormat, WAVEFORMATEX** ppSupportedInputFormat)
{
    if (pRequestedInputFormat == NULL) return E_POINTER;
    if (IsDefaultFormat(pRequestedInputFormat)) {
        if (ppSupportedInputFormat != NULL) *ppSupportedInputFormat = NULL;
        return S_OK;
    }
    if (ppSupportedInputFormat != NULL) {
        WAVEFORMATEX* pNearest = reinterpret_cast<WAVEFORMATEX*>(XAPOAlloc(sizeof(WAVEFORMATEX)));
        if (pNearest == NULL) return E_OUTOFMEMORY;
        memcpy(pNearest, pRequestedInputFormat, sizeof(WAVEFORMATEX));
        ValidateFormatDefault(pNearest, TRUE);
        *ppSupportedInputFormat = pNearest;
    }
    return XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT CXAPOBase::IsOutputFormatSupported(const WAVEFORMATEX* /*pInputFormat*/,
    const WAVEFORMATEX* pRequestedOutputFormat, WAVEFORMATEX** ppSupportedOutputFormat)
{
    if (pRequestedOutputFormat == NULL) return E_POINTER;
    if (IsDefaultFormat(pRequestedOutputFormat)) {
        if (ppSupportedOutputFormat != NULL) *ppSupportedOutputFormat = NULL;
        return S_OK;
    }
    if (ppSupportedOutputFormat != NULL) {
        WAVEFORMATEX* pNearest = reinterpret_cast<WAVEFORMATEX*>(XAPOAlloc(sizeof(WAVEFORMATEX)));
        if (pNearest == NULL) return E_OUTOFMEMORY;
        memcpy(pNearest, pRequestedOutputFormat, sizeof(WAVEFORMATEX));
        ValidateFormatDefault(pNearest, TRUE);
        *ppSupportedOutputFormat = pNearest;
    }
    return XAPO_E_FORMAT_UNSUPPORTED;
}

HRESULT CXAPOBase::LockForProcess(UINT32 InputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters,
    UINT32 OutputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pOutputLockedParameters)
{
    const XAPO_REGISTRATION_PROPERTIES* p = m_pRegistrationProperties;
    if (InputLockedParameterCount  < p->MinInputBufferCount  ||
        InputLockedParameterCount  > p->MaxInputBufferCount  ||
        OutputLockedParameterCount < p->MinOutputBufferCount ||
        OutputLockedParameterCount > p->MaxOutputBufferCount)
        return E_INVALIDARG;
    if ((p->Flags & XAPO_FLAG_BUFFERCOUNT_MUST_MATCH) &&
        InputLockedParameterCount != OutputLockedParameterCount)
        return E_INVALIDARG;

    // Validate each buffer's format against the default, and input/output pairs
    // against the MUST_MATCH flags.
    for (UINT32 i = 0; i < InputLockedParameterCount; ++i) {
        if (pInputLockedParameters == NULL || pInputLockedParameters[i].pFormat == NULL)
            return E_INVALIDARG;
        WAVEFORMATEX fmt = *pInputLockedParameters[i].pFormat;
        if (ValidateFormatDefault(&fmt, FALSE) != S_OK) return XAPO_E_FORMAT_UNSUPPORTED;
    }
    for (UINT32 i = 0; i < OutputLockedParameterCount; ++i) {
        if (pOutputLockedParameters == NULL || pOutputLockedParameters[i].pFormat == NULL)
            return E_INVALIDARG;
        WAVEFORMATEX fmt = *pOutputLockedParameters[i].pFormat;
        if (ValidateFormatDefault(&fmt, FALSE) != S_OK) return XAPO_E_FORMAT_UNSUPPORTED;
    }
    if (InputLockedParameterCount > 0 && OutputLockedParameterCount > 0) {
        HRESULT hr = ValidateFormatPair(pInputLockedParameters[0].pFormat,
            const_cast<WAVEFORMATEX*>(pOutputLockedParameters[0].pFormat), FALSE);
        if (hr != S_OK) return hr;
    }
    m_fIsLocked = TRUE;
    return S_OK;
}

void CXAPOBase::UnlockForProcess()
{
    m_fIsLocked = FALSE;
}


//--------------------------------------------------------------------------//
//  CXAPOParametersBase - lock-free triple-buffered parameter handoff.
//--------------------------------------------------------------------------//
CXAPOParametersBase::CXAPOParametersBase(const XAPO_REGISTRATION_PROPERTIES* pRegistrationProperties,
    BYTE* pParameterBlocks, UINT32 uParameterBlockByteSize, BOOL fProducer)
    : CXAPOBase(pRegistrationProperties)
    , m_pParameterBlocks(pParameterBlocks)
    , m_pCurrentParameters(pParameterBlocks)
    , m_pCurrentParametersInternal(pParameterBlocks)
    , m_uCurrentParametersIndex(0)
    , m_uParameterBlockByteSize(uParameterBlockByteSize)
    , m_fNewerResultsReady(FALSE)
    , m_fProducer(fProducer)
{
}

CXAPOParametersBase::~CXAPOParametersBase()
{
}

void CXAPOParametersBase::SetParameters(const void* pParameters, UINT32 ParameterByteSize)
{
    XAPOASSERT(!m_fProducer);
    XAPOASSERT(pParameters != NULL);
    XAPOASSERT(ParameterByteSize == m_uParameterBlockByteSize);
    OnSetParameters(pParameters, ParameterByteSize);

    // Write into the next of the three blocks, then publish it atomically so a
    // concurrent BeginProcess() sees either the old or the new block, never a
    // partial write.
    UINT32 next = (m_uCurrentParametersIndex + 1) % 3;
    BYTE* pNext = m_pParameterBlocks + (next * m_uParameterBlockByteSize);
    memcpy(pNext, pParameters, ParameterByteSize);
    m_uCurrentParametersIndex = next;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&m_pCurrentParameters), pNext);
    m_fNewerResultsReady = TRUE;
}

void CXAPOParametersBase::GetParameters(void* pParameters, UINT32 ParameterByteSize)
{
    XAPOASSERT(pParameters != NULL);
    XAPOASSERT(ParameterByteSize == m_uParameterBlockByteSize);
    BYTE* pCurrent = reinterpret_cast<BYTE*>(
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&m_pCurrentParameters), NULL, NULL));
    if (pCurrent == NULL) pCurrent = m_pParameterBlocks;
    memcpy(pParameters, pCurrent, ParameterByteSize);
}

BOOL CXAPOParametersBase::ParametersChanged()
{
    return m_fNewerResultsReady;
}

BYTE* CXAPOParametersBase::BeginProcess()
{
    // Latch the currently-published block for this processing pass.
    BYTE* pCurrent = reinterpret_cast<BYTE*>(
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&m_pCurrentParameters), NULL, NULL));
    if (pCurrent == NULL) pCurrent = m_pParameterBlocks;
    m_pCurrentParametersInternal = pCurrent;
    return pCurrent;
}

void CXAPOParametersBase::EndProcess()
{
    m_fNewerResultsReady = FALSE;
}
