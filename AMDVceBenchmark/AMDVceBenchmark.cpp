/*******************************************************************************
 Copyright ©2014 Advanced Micro Devices, Inc. All rights reserved.

 Redistribution and use in source and binary forms, with or without 
 modification, are permitted provided that the following conditions are met:

 1   Redistributions of source code must retain the above copyright notice, 
 this list of conditions and the following disclaimer.
 2   Redistributions in binary form must reproduce the above copyright notice, 
 this list of conditions and the following disclaimer in the 
 documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

/**
 ********************************************************************************
 * @file <SimpleEncoder.cpp>
 *
 * @brief This sample encodes NV12 frames using AMF Encoder and writes them 
 *        to H.264 elementary stream 
 *
 ********************************************************************************
 */
#define ENABLE_4K

#include <stdio.h>
#include <tchar.h>
#include <DXGI1_2.h>
#include <d3d11.h>
#include "AMFFactory.h"
#include "VideoEncoderVCE.h"
#include "Thread.h"
#include "bmp.h"

#if defined(ENABLE_4K)
static amf_int32 widthIn                  = 2160;
static amf_int32 heightIn                 = 1200;
#else
static amf_int32 widthIn                  = 2048;
static amf_int32 heightIn                 = 1080;
#endif
static amf_int32 frameRateIn              = 60;
static amf_int64 bitRateIn                = 100000000; // 10000 - 100000000 bit/s
static amf_int32 frameCount               = 1000;

#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

static amf_int32 xPos = 0;
static amf_int32 yPos = 0;

#define MILLISEC_TIME     10000

class PollingThread : public amf::AMFThread
{
protected:
    amf::AMFContextPtr      m_pContext;
    amf::AMFComponentPtr    m_pEncoder;
    FILE                    *m_pFile;
public:
    PollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, const wchar_t *pFileName);
    ~PollingThread();
    virtual void Run();
};

//static void FillSurface(amf::AMFContext *context, amf::AMFSurface *surface, amf_int32 i);
static void FillSurfaceDX11(amf::AMFContext *context, amf::AMFSurface *surface, BYTE* pBuf);

BYTE* pBuffer1;
BYTE* pBuffer2;

#define USE_BMP 0
#define USE_NV12 1

#define SOURCE_FMT USE_NV12

#if (SOURCE_FMT == USE_BMP)
#if defined(ENABLE_4K)
const wchar_t* fileSource1 = L"source1_2k.bmp";
const wchar_t* fileSource2 = L"source2_2k.bmp";
#else
const wchar_t* fileSource1 = L"source1.bmp";
const wchar_t* fileSource2 = L"source2.bmp";
#endif
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_BGRA;
#elif (SOURCE_FMT == USE_NV12)
#if defined(ENABLE_4K)
const wchar_t* fileSource1 = L"source1_2k_nv12.yuv";
const wchar_t* fileSource2 = L"source2_2k_nv12.yuv";
#else
const wchar_t* fileSource1 = L"source1_nv12.yuv";
const wchar_t* fileSource2 = L"source2_nv12.yuv";
#endif
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_NV12;
#else
const wchar_t* fileSource1 = L"source1.bmp";
const wchar_t* fileSource2 = L"source2.bmp";
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_BGRA;
#endif

static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX11;

UINT Calcbpp(UINT format)
{
    UINT bpp = 0;

    switch (format)
    {
    case amf::AMF_SURFACE_BGRA:
    case amf::AMF_SURFACE_ARGB:
    case amf::AMF_SURFACE_RGBA:
        bpp = 32;
        break;
    case amf::AMF_SURFACE_NV12:
        bpp = 12;
        break;
    case amf::AMF_SURFACE_YV12:
        bpp = 16;
        break;
    default:
        bpp = 0;
        break;
    }

    return bpp;
}

int _tmain(int argc, _TCHAR* argv[])
{
    ::amf_increase_timer_precision();

    UINT size = widthIn * heightIn * Calcbpp(formatIn) / 8 * sizeof(BYTE);

    pBuffer1 = (BYTE*)malloc(size);
    RtlZeroMemory(pBuffer1, size);
    pBuffer2 = (BYTE*)malloc(size);
    RtlZeroMemory(pBuffer2, size);

    wchar_t buf[MAX_PATH * 2];
    ZeroMemory(buf, MAX_PATH * 2 * sizeof(wchar_t));
    GetCurrentDirectory(MAX_PATH * 2, buf);
    swprintf_s(buf, MAX_PATH * 2, L"%s\\%s", buf, fileSource1);

    FILE *pBmpFile = NULL;
    if (_wfopen_s(&pBmpFile, buf, L"rb") == 0)
    {
#if (SOURCE_FMT == USE_NV12) || (SOURCE_FMT == USE_YV12)
        fseek(pBmpFile, 0, 0);
#else
        fseek(pBmpFile, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), 0);
#endif
        size_t sizeRead = fread(pBuffer1, 1, widthIn * heightIn * Calcbpp(formatIn) / 8, pBmpFile);
        fclose(pBmpFile);
        pBmpFile = NULL;
    }

    ZeroMemory(buf, MAX_PATH * 2 * sizeof(wchar_t));
    GetCurrentDirectory(MAX_PATH * 2, buf);
    swprintf_s(buf, MAX_PATH * 2, L"%s\\%s", buf, fileSource2);

    if (_wfopen_s(&pBmpFile, buf, L"rb") == 0)
    {
#if (SOURCE_FMT == USE_NV12) || (SOURCE_FMT == USE_YV12)
        fseek(pBmpFile, 0, 0);
#else
        fseek(pBmpFile, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), 0);
#endif
        size_t sizeRead = fread(pBuffer2, 1, widthIn * heightIn * Calcbpp(formatIn) / 8, pBmpFile);
        fclose(pBmpFile);
        pBmpFile = NULL;
    }

    AMF_RESULT res = AMF_OK; // error checking can be added later

    // initialize AMF
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    amf::AMFSurfacePtr surfaceIn;
    amf::AMFSurfacePtr surfaceEven;
    amf::AMFSurfacePtr surfaceOdd;

    // Factory
    if (AMF_OK == res)
    {
        res = g_AMFFactory.Init();
    }

    // context
    if (AMF_OK == res)
    {
        res = g_AMFFactory.GetFactory()->CreateContext(&context);
    }

    if(res != AMF_OK)
    {
        printf("FAIL\n");
        return AMF_FAIL;
    }
    res = context->InitDX11(NULL); // can be DX11 device
    // component: encoder
    res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoEncoderVCE_AVC, &encoder);
    if(res != AMF_OK)
    {
        printf("FAIL\n");
        return AMF_FAIL;
    }
    //res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);// slowest
    //res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_WEBCAM); // faster then low latency mode but higher latency
    //res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY);// can't see mush difference compared with low latency mode.
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRateIn);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, bitRateIn);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE, FALSE);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_I, 22);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_P, 22);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_QP_B, 22);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_MIN_QP, 22);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_MAX_QP, 22);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
    //res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY);
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR);
    
#if defined(ENABLE_4K)
    res = encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);
#endif
    
    res = encoder->Init(formatIn, widthIn, heightIn);
    if(res != AMF_OK)
    {
        printf("FAIL\n");
        return AMF_FAIL;
    }

    // create input surface
    SYSTEMTIME curTime = { 0 };
    GetLocalTime(&curTime);

    ZeroMemory(buf, MAX_PATH * 2 * sizeof(wchar_t));
    GetCurrentDirectory(MAX_PATH * 2, buf);

    swprintf_s(buf, MAX_PATH * 2, L"%s\\AMDVCE_Encode_%04d%02d%02d_%02d%02d%02d.h264", buf, curTime.wYear, curTime.wMonth, curTime.wDay, curTime.wHour, curTime.wMinute, curTime.wSecond);

    PollingThread thread(context, encoder, buf);
    thread.Start();

    //Prepare even and odd surface from the 2 source image
    res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceEven);
    FillSurfaceDX11(context, surfaceEven, pBuffer1);
    res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceOdd);
    FillSurfaceDX11(context, surfaceOdd, pBuffer2);

    // encode some frames
    amf_int32 submitted = 0;
    while(submitted < frameCount)
    {
        // encode
        amf_pts start_time = amf_high_precision_clock();
        if(submitted % 2)
        {
            surfaceOdd->SetProperty(START_TIME_PROPERTY, start_time);
            res = encoder->SubmitInput(surfaceOdd);
        }
        else
        {
            surfaceEven->SetProperty(START_TIME_PROPERTY, start_time);
            res = encoder->SubmitInput(surfaceEven);
        }
        if(res == AMF_INPUT_FULL) // handle full queue
        {
            amf_sleep(1); // input queue is full: wait, poll and submit again
        }
        else
        {
            submitted++;
        }
    }
    // drain encoder; input queue can be full
    while(true)
    {
        res = encoder->Drain();
        if(res != AMF_INPUT_FULL) // handle full queue
        {
            break;
        }
        amf_sleep(1); // input queue is full: wait and try again
    }
    thread.WaitForStop();
   
    // clean-up in this order
    surfaceIn = NULL;
    encoder->Terminate();
    encoder = NULL;
    context->Terminate();
    context = NULL; // context is the last

    printf("PASS\n");

    free(pBuffer1);
    free(pBuffer2);

    return 0;
}

static void FillSurfaceDX11(amf::AMFContext *context, amf::AMFSurface *surface, BYTE* pBuf)
{
    HRESULT hr = S_OK;
    ID3D11Device *deviceDX11 = (ID3D11Device*)context->GetDX11Device(); // no reference counting - do not Release()
    ID3D11Texture2D *textureDX11 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()

    IDXGISurface2* pTempSurf = nullptr;
    ID3D11Texture2D* pTempTex = nullptr;
    ID3D11DeviceContext* m_DeviceContext;
    D3D11_TEXTURE2D_DESC desc;
    textureDX11->GetDesc(&desc);
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.MiscFlags = 0;
    desc.BindFlags = 0;
    hr = deviceDX11->CreateTexture2D(&desc, nullptr, &pTempTex);
    pTempTex->QueryInterface(__uuidof(IDXGISurface2), (reinterpret_cast<void**>(&pTempSurf)));

    // Copy from image buffer to texture surface buffer
    DXGI_MAPPED_RECT bufMapped;
    RtlZeroMemory(&bufMapped, sizeof(DXGI_MAPPED_RECT));
    hr = pTempSurf->Map(&bufMapped, DXGI_MAP_WRITE);
    if (widthIn != bufMapped.Pitch)
    {
        for (int i = 0; i < heightIn; i++)
        {
            RtlCopyMemory(bufMapped.pBits + i * (bufMapped.Pitch), pBuf + i * (widthIn), widthIn);
        }
        //RtlCopyMemory(bufMapped.pBits + bufMapped.Pitch * heightIn, pBuf + widthIn * heightIn, widthIn * heightIn / 2);
        for (int i = 0; i < heightIn; i++)
        {
            RtlCopyMemory(bufMapped.pBits + bufMapped.Pitch * heightIn + i * (bufMapped.Pitch) / 2,
                            pBuf + widthIn * heightIn + i * (widthIn) / 2, 
                            widthIn);
        }
    }
    else
    {
        RtlCopyMemory(bufMapped.pBits, pBuf, widthIn * heightIn * Calcbpp(formatIn) / 8);
    }

    // Copy from texture buffer to encoder surface
    deviceDX11->GetImmediateContext(&m_DeviceContext);
    m_DeviceContext->CopyResource(textureDX11, pTempTex);

    pTempSurf->Unmap();
    pTempSurf->Release();
    pTempTex->Release();

    m_DeviceContext->Flush();
    m_DeviceContext->Release();
}

PollingThread::PollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, const wchar_t *pFileName) : m_pContext(context), m_pEncoder(encoder), m_pFile(NULL)
{
    m_pFile = _wfopen(pFileName, L"wb");
}
PollingThread::~PollingThread()
{
    if(m_pFile)
    {
        fclose(m_pFile);
    }
}
void PollingThread::Run()
{
    RequestStop();

    amf_pts latency_time = 0;
    amf_pts write_duration = 0;
    amf_pts encode_duration = 0;
    amf_pts last_poll_time = 0;

    AMF_RESULT res = AMF_OK; // error checking can be added later
    while(true)
    {
        amf::AMFDataPtr data;
        res = m_pEncoder->QueryOutput(&data);
        if(res == AMF_EOF)
        {
            break; // Drain complete
        }
        if(data != NULL)
        {
            amf_pts poll_time = amf_high_precision_clock();
            amf_pts start_time = 0;
            data->GetProperty(START_TIME_PROPERTY, &start_time);
            if(start_time < last_poll_time ) // remove wait time if submission was faster then encode
            {
                start_time = last_poll_time;
            }
            last_poll_time = poll_time;

            encode_duration += poll_time - start_time;

            if(latency_time == 0)
            {
                latency_time = poll_time - start_time;
            }

            amf::AMFBufferPtr buffer(data); // query for buffer interface
            fwrite(buffer->GetNative(), 1, buffer->GetSize(), m_pFile);
            
            write_duration += amf_high_precision_clock() - poll_time;
        }
        else
        {
            amf_sleep(1);
        }

    }
    printf("latency           = %.4fms\nencode  per frame = %.4fms\nwrite per frame   = %.4fms\n", 
        double(latency_time)/MILLISEC_TIME,
        double(encode_duration )/MILLISEC_TIME/frameCount, 
        double(write_duration)/MILLISEC_TIME/frameCount);

    m_pEncoder = NULL;
    m_pContext = NULL;
}

