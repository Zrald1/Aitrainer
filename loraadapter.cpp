#include "loraadapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

#if defined(_WIN32) && defined(AITRAINER_D3D11_GPU)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#endif

namespace {

int clampedRank(int rank)
{
    return std::max(1, std::min(rank, 16));
}

double clipped(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

#if defined(_WIN32) && defined(AITRAINER_D3D11_GPU)

struct GpuVector {
    float values[16];
};

struct GpuPairIndex {
    std::uint32_t aIndex;
    std::uint32_t bIndex;
};

struct GpuParams {
    std::uint32_t pairCount;
    std::uint32_t epochs;
    std::uint32_t rank;
    float alphaOverRank;
    float targetDelta;
    float learningRate;
    float pad0;
    float pad1;
};

struct GpuTextScanParams {
    std::uint32_t byteCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

template <typename T>
void releaseCom(T *&value)
{
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::string hresultMessage(const char *action, HRESULT hr)
{
    std::ostringstream out;
    out << action << " failed (HRESULT 0x"
        << std::hex << std::uppercase << static_cast<unsigned long>(hr) << ").";
    return out.str();
}

bool checkedByteWidth(size_t itemSize, size_t itemCount, UINT *byteWidth, std::string *status)
{
    const size_t byteCount = itemSize * itemCount;
    if (itemCount == 0 || byteCount == 0 || byteCount > std::numeric_limits<UINT>::max()) {
        if (status) {
            *status = "Local GPU training failed: sequence is too large for one Direct3D buffer.";
        }
        return false;
    }
    *byteWidth = static_cast<UINT>(byteCount);
    return true;
}

const char *loraComputeShaderSource()
{
    return R"HLSL(
struct Vector16 {
    float values[16];
};

struct PairIndex {
    uint aIndex;
    uint bIndex;
};

StructuredBuffer<PairIndex> pairs : register(t0);
RWStructuredBuffer<Vector16> aVectors : register(u0);
RWStructuredBuffer<Vector16> bVectors : register(u1);

cbuffer Params : register(b0) {
    uint pairCount;
    uint epochs;
    uint rank;
    float alphaOverRank;
    float targetDelta;
    float learningRate;
    float pad0;
    float pad1;
};

float clipValue(float value, float low, float high)
{
    return min(max(value, low), high);
}

[numthreads(1, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x != 0) {
        return;
    }

    for (uint epoch = 0; epoch < epochs; ++epoch) {
        for (uint pairOffset = 0; pairOffset < pairCount; ++pairOffset) {
            PairIndex pairIndex = pairs[pairOffset];
            Vector16 a = aVectors[pairIndex.aIndex];
            Vector16 b = bVectors[pairIndex.bIndex];
            float oldA[16];
            float dotValue = 0.0;

            [unroll]
            for (uint i = 0; i < 16; ++i) {
                oldA[i] = a.values[i];
                if (i < rank) {
                    dotValue += a.values[i] * b.values[i];
                }
            }

            float current = alphaOverRank * dotValue;
            float error = clipValue(targetDelta - current, -4.0, 4.0);
            float scale = learningRate * error * alphaOverRank;

            [unroll]
            for (uint i = 0; i < 16; ++i) {
                if (i < rank) {
                    a.values[i] = clipValue(a.values[i] + scale * b.values[i], -8.0, 8.0);
                    b.values[i] = clipValue(b.values[i] + scale * oldA[i], -8.0, 8.0);
                }
            }

            aVectors[pairIndex.aIndex] = a;
            bVectors[pairIndex.bIndex] = b;
        }
    }
}
)HLSL";
}

const char *textScanComputeShaderSource()
{
    return R"HLSL(
StructuredBuffer<uint> bytesIn : register(t0);
RWStructuredBuffer<uint> counts : register(u0);

cbuffer Params : register(b0) {
    uint byteCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint idx = dispatchId.x;
    if (idx >= byteCount) {
        return;
    }

    uint c = bytesIn[idx] & 255;
    if (c == 10 || c == 13) {
        InterlockedAdd(counts[0], 1);
    }
    if (c == 44) {
        InterlockedAdd(counts[1], 1);
    }
    if (c == 9) {
        InterlockedAdd(counts[2], 1);
    }
    if (c == 34) {
        InterlockedAdd(counts[3], 1);
    }
    if (c == 123 || c == 91) {
        InterlockedAdd(counts[4], 1);
    }
    if (c == 58) {
        InterlockedAdd(counts[5], 1);
    }
    if (c == 124 || c == 59) {
        InterlockedAdd(counts[6], 1);
    }
    if (c == 0 || c < 9 || (c > 13 && c < 32)) {
        InterlockedAdd(counts[7], 1);
    }
}
)HLSL";
}

class D3D11LoraComputeBackend {
public:
    ~D3D11LoraComputeBackend()
    {
        releaseCom(m_loraShader);
        releaseCom(m_textScanShader);
        releaseCom(m_context);
        releaseCom(m_device);
    }

    static D3D11LoraComputeBackend &instance()
    {
        static D3D11LoraComputeBackend backend;
        return backend;
    }

    bool available(std::string *status)
    {
        return initialize(status);
    }

    bool scanText(const std::string &text, std::string *status)
    {
        if (!initialize(status)) {
            return false;
        }

        if (text.empty()) {
            if (status) {
                *status = "Direct3D 11 local GPU parser scan skipped: dataset payload is empty.";
            }
            return false;
        }

        std::vector<std::uint32_t> bytes;
        bytes.reserve(text.size());
        for (unsigned char ch : text) {
            bytes.push_back(static_cast<std::uint32_t>(ch));
        }

        UINT byteBufferWidth = 0;
        if (!checkedByteWidth(sizeof(std::uint32_t), bytes.size(), &byteBufferWidth, status)) {
            return false;
        }

        ID3D11Buffer *inputBuffer = nullptr;
        ID3D11Buffer *countsBuffer = nullptr;
        ID3D11Buffer *paramsBuffer = nullptr;
        ID3D11ShaderResourceView *inputSrv = nullptr;
        ID3D11UnorderedAccessView *countsUav = nullptr;

        auto cleanup = [&]() {
            releaseCom(inputSrv);
            releaseCom(countsUav);
            releaseCom(inputBuffer);
            releaseCom(countsBuffer);
            releaseCom(paramsBuffer);
        };

        auto fail = [&](const std::string &message) {
            cleanup();
            if (status) {
                *status = message;
            }
            return false;
        };

        D3D11_SUBRESOURCE_DATA inputData = {};
        inputData.pSysMem = bytes.data();
        D3D11_BUFFER_DESC inputDesc = {};
        inputDesc.ByteWidth = byteBufferWidth;
        inputDesc.Usage = D3D11_USAGE_DEFAULT;
        inputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        inputDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        inputDesc.StructureByteStride = sizeof(std::uint32_t);
        HRESULT hr = m_device->CreateBuffer(&inputDesc, &inputData, &inputBuffer);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parser input buffer creation", hr));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC inputSrvDesc = {};
        inputSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        inputSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        inputSrvDesc.Buffer.FirstElement = 0;
        inputSrvDesc.Buffer.NumElements = static_cast<UINT>(bytes.size());
        hr = m_device->CreateShaderResourceView(inputBuffer, &inputSrvDesc, &inputSrv);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parser shader-resource view creation", hr));
        }

        const std::uint32_t zeroCounts[8] = {};
        D3D11_SUBRESOURCE_DATA countsData = {};
        countsData.pSysMem = zeroCounts;
        D3D11_BUFFER_DESC countsDesc = {};
        countsDesc.ByteWidth = sizeof(zeroCounts);
        countsDesc.Usage = D3D11_USAGE_DEFAULT;
        countsDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        countsDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        countsDesc.StructureByteStride = sizeof(std::uint32_t);
        hr = m_device->CreateBuffer(&countsDesc, &countsData, &countsBuffer);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parser count buffer creation", hr));
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC countsUavDesc = {};
        countsUavDesc.Format = DXGI_FORMAT_UNKNOWN;
        countsUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        countsUavDesc.Buffer.FirstElement = 0;
        countsUavDesc.Buffer.NumElements = 8;
        hr = m_device->CreateUnorderedAccessView(countsBuffer, &countsUavDesc, &countsUav);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parser unordered-access view creation", hr));
        }

        GpuTextScanParams params = {};
        params.byteCount = static_cast<std::uint32_t>(bytes.size());
        D3D11_SUBRESOURCE_DATA paramsData = {};
        paramsData.pSysMem = &params;
        D3D11_BUFFER_DESC paramsDesc = {};
        paramsDesc.ByteWidth = sizeof(GpuTextScanParams);
        paramsDesc.Usage = D3D11_USAGE_DEFAULT;
        paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_device->CreateBuffer(&paramsDesc, &paramsData, &paramsBuffer);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parser parameter buffer creation", hr));
        }

        ID3D11ShaderResourceView *srvs[1] = {inputSrv};
        ID3D11UnorderedAccessView *uavs[1] = {countsUav};
        UINT initialCounts[1] = {0};
        m_context->CSSetShader(m_textScanShader, nullptr, 0);
        m_context->CSSetShaderResources(0, 1, srvs);
        m_context->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);
        m_context->CSSetConstantBuffers(0, 1, &paramsBuffer);
        const UINT groups = static_cast<UINT>((bytes.size() + 255) / 256);
        m_context->Dispatch(groups, 1, 1);

        ID3D11ShaderResourceView *nullSrvs[1] = {nullptr};
        ID3D11UnorderedAccessView *nullUavs[1] = {nullptr};
        ID3D11Buffer *nullBuffers[1] = {nullptr};
        m_context->CSSetShaderResources(0, 1, nullSrvs);
        m_context->CSSetUnorderedAccessViews(0, 1, nullUavs, initialCounts);
        m_context->CSSetConstantBuffers(0, 1, nullBuffers);
        m_context->CSSetShader(nullptr, nullptr, 0);

        D3D11_BUFFER_DESC stagingDesc = countsDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Buffer *stagingBuffer = nullptr;
        hr = m_device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
        if (FAILED(hr)) {
            releaseCom(stagingBuffer);
            return fail(hresultMessage("Direct3D parser staging buffer creation", hr));
        }

        m_context->CopyResource(stagingBuffer, countsBuffer);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = m_context->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            releaseCom(stagingBuffer);
            return fail(hresultMessage("Direct3D parser result mapping", hr));
        }

        std::uint32_t counts[8] = {};
        std::memcpy(counts, mapped.pData, sizeof(counts));
        m_context->Unmap(stagingBuffer, 0);
        releaseCom(stagingBuffer);

        cleanup();
        if (status) {
            const bool jsonLike = counts[4] > 0 && counts[5] > 0;
            const bool csvLike = counts[1] > counts[2] && counts[1] > counts[6];
            const bool tsvLike = counts[2] > 0 && counts[2] >= counts[1];
            const bool pipeLike = counts[6] > 0 && counts[6] >= counts[1];
            const char *shape = jsonLike ? "json/jsonl"
                : (tsvLike ? "tsv"
                   : (pipeLike ? "pipe/semicolon"
                      : (csvLike ? "csv" : "plain text")));
            std::ostringstream out;
            out << "Direct3D 11 local GPU parser scan complete: "
                << bytes.size() << " bytes, "
                << counts[0] << " line breaks, detected " << shape << " structure.";
            *status = out.str();
        }
        return true;
    }

    bool trainSequence(const std::vector<GpuPairIndex> &pairs,
                       std::vector<GpuVector> &aVectors,
                       std::vector<GpuVector> &bVectors,
                       const GpuParams &params,
                       std::string *status)
    {
        if (!initialize(status)) {
            return false;
        }

        UINT pairBytes = 0;
        UINT aBytes = 0;
        UINT bBytes = 0;
        if (!checkedByteWidth(sizeof(GpuPairIndex), pairs.size(), &pairBytes, status)
            || !checkedByteWidth(sizeof(GpuVector), aVectors.size(), &aBytes, status)
            || !checkedByteWidth(sizeof(GpuVector), bVectors.size(), &bBytes, status)) {
            return false;
        }

        ID3D11Buffer *pairBuffer = nullptr;
        ID3D11Buffer *aBuffer = nullptr;
        ID3D11Buffer *bBuffer = nullptr;
        ID3D11Buffer *paramsBuffer = nullptr;
        ID3D11ShaderResourceView *pairSrv = nullptr;
        ID3D11UnorderedAccessView *aUav = nullptr;
        ID3D11UnorderedAccessView *bUav = nullptr;

        auto cleanup = [&]() {
            releaseCom(pairSrv);
            releaseCom(aUav);
            releaseCom(bUav);
            releaseCom(pairBuffer);
            releaseCom(aBuffer);
            releaseCom(bBuffer);
            releaseCom(paramsBuffer);
        };

        auto fail = [&](const std::string &message) {
            cleanup();
            if (status) {
                *status = message;
            }
            return false;
        };

        D3D11_SUBRESOURCE_DATA pairData = {};
        pairData.pSysMem = pairs.data();
        D3D11_BUFFER_DESC pairDesc = {};
        pairDesc.ByteWidth = pairBytes;
        pairDesc.Usage = D3D11_USAGE_DEFAULT;
        pairDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        pairDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        pairDesc.StructureByteStride = sizeof(GpuPairIndex);
        HRESULT hr = m_device->CreateBuffer(&pairDesc, &pairData, &pairBuffer);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D pair buffer creation", hr));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC pairSrvDesc = {};
        pairSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        pairSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        pairSrvDesc.Buffer.FirstElement = 0;
        pairSrvDesc.Buffer.NumElements = static_cast<UINT>(pairs.size());
        hr = m_device->CreateShaderResourceView(pairBuffer, &pairSrvDesc, &pairSrv);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D pair shader-resource view creation", hr));
        }

        auto createVectorBuffer = [&](const std::vector<GpuVector> &values,
                                      UINT byteWidth,
                                      ID3D11Buffer **buffer,
                                      ID3D11UnorderedAccessView **uav,
                                      const char *label) -> bool {
            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = values.data();
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = byteWidth;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(GpuVector);
            HRESULT localHr = m_device->CreateBuffer(&desc, &data, buffer);
            if (FAILED(localHr)) {
                return fail(hresultMessage(label, localHr));
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = static_cast<UINT>(values.size());
            localHr = m_device->CreateUnorderedAccessView(*buffer, &uavDesc, uav);
            if (FAILED(localHr)) {
                return fail(hresultMessage("Direct3D unordered-access view creation", localHr));
            }

            return true;
        };

        if (!createVectorBuffer(aVectors, aBytes, &aBuffer, &aUav, "Direct3D LoRA A-vector buffer creation")
            || !createVectorBuffer(bVectors, bBytes, &bBuffer, &bUav, "Direct3D LoRA B-vector buffer creation")) {
            return false;
        }

        D3D11_SUBRESOURCE_DATA paramsData = {};
        paramsData.pSysMem = &params;
        D3D11_BUFFER_DESC paramsDesc = {};
        paramsDesc.ByteWidth = sizeof(GpuParams);
        paramsDesc.Usage = D3D11_USAGE_DEFAULT;
        paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_device->CreateBuffer(&paramsDesc, &paramsData, &paramsBuffer);
        if (FAILED(hr)) {
            return fail(hresultMessage("Direct3D parameter buffer creation", hr));
        }

        ID3D11ShaderResourceView *srvs[1] = {pairSrv};
        ID3D11UnorderedAccessView *uavs[2] = {aUav, bUav};
        UINT initialCounts[2] = {0, 0};
        m_context->CSSetShader(m_loraShader, nullptr, 0);
        m_context->CSSetShaderResources(0, 1, srvs);
        m_context->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);
        m_context->CSSetConstantBuffers(0, 1, &paramsBuffer);
        m_context->Dispatch(1, 1, 1);

        ID3D11ShaderResourceView *nullSrvs[1] = {nullptr};
        ID3D11UnorderedAccessView *nullUavs[2] = {nullptr, nullptr};
        ID3D11Buffer *nullBuffers[1] = {nullptr};
        m_context->CSSetShaderResources(0, 1, nullSrvs);
        m_context->CSSetUnorderedAccessViews(0, 2, nullUavs, initialCounts);
        m_context->CSSetConstantBuffers(0, 1, nullBuffers);
        m_context->CSSetShader(nullptr, nullptr, 0);

        auto readBack = [&](ID3D11Buffer *source, void *target, size_t byteCount, const char *label) -> bool {
            D3D11_BUFFER_DESC desc = {};
            source->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Buffer *staging = nullptr;
            HRESULT localHr = m_device->CreateBuffer(&desc, nullptr, &staging);
            if (FAILED(localHr)) {
                releaseCom(staging);
                return fail(hresultMessage(label, localHr));
            }

            m_context->CopyResource(staging, source);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            localHr = m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(localHr)) {
                releaseCom(staging);
                return fail(hresultMessage("Direct3D GPU result mapping", localHr));
            }

            std::memcpy(target, mapped.pData, byteCount);
            m_context->Unmap(staging, 0);
            releaseCom(staging);
            return true;
        };

        if (!readBack(aBuffer, aVectors.data(), aBytes, "Direct3D A-vector staging buffer creation")
            || !readBack(bBuffer, bVectors.data(), bBytes, "Direct3D B-vector staging buffer creation")) {
            return false;
        }

        cleanup();
        if (status) {
            *status = m_status;
        }
        return true;
    }

private:
    bool initialize(std::string *status)
    {
        if (m_attempted) {
            if (status) {
                *status = m_status;
            }
            return m_ready;
        }

        m_attempted = true;

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        const D3D_FEATURE_LEVEL requestedLevels[] = {D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       0,
                                       requestedLevels,
                                       1,
                                       D3D11_SDK_VERSION,
                                       &m_device,
                                       &featureLevel,
                                       &m_context);
        if (FAILED(hr)) {
            m_status = hresultMessage("Direct3D 11 hardware compute device creation", hr);
            if (status) {
                *status = m_status;
            }
            return false;
        }

        ID3DBlob *shaderBlob = nullptr;
        ID3DBlob *errorBlob = nullptr;
        const char *shaderSource = loraComputeShaderSource();
        hr = D3DCompile(shaderSource,
                        std::strlen(shaderSource),
                        "AitrainerLoraCompute.hlsl",
                        nullptr,
                        nullptr,
                        "main",
                        "cs_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS,
                        0,
                        &shaderBlob,
                        &errorBlob);
        if (FAILED(hr)) {
            std::string compileError = hresultMessage("Direct3D LoRA compute shader compilation", hr);
            if (errorBlob && errorBlob->GetBufferPointer()) {
                compileError += " ";
                compileError += static_cast<const char *>(errorBlob->GetBufferPointer());
            }
            releaseCom(shaderBlob);
            releaseCom(errorBlob);
            releaseCom(m_context);
            releaseCom(m_device);
            m_status = compileError;
            if (status) {
                *status = m_status;
            }
            return false;
        }

        hr = m_device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                           shaderBlob->GetBufferSize(),
                                           nullptr,
                                           &m_loraShader);
        releaseCom(shaderBlob);
        releaseCom(errorBlob);
        if (FAILED(hr)) {
            releaseCom(m_context);
            releaseCom(m_device);
            m_status = hresultMessage("Direct3D LoRA compute shader creation", hr);
            if (status) {
                *status = m_status;
            }
            return false;
        }

        const char *scanSource = textScanComputeShaderSource();
        hr = D3DCompile(scanSource,
                        std::strlen(scanSource),
                        "AitrainerDatasetScan.hlsl",
                        nullptr,
                        nullptr,
                        "main",
                        "cs_5_0",
                        D3DCOMPILE_ENABLE_STRICTNESS,
                        0,
                        &shaderBlob,
                        &errorBlob);
        if (FAILED(hr)) {
            std::string compileError = hresultMessage("Direct3D parser scan shader compilation", hr);
            if (errorBlob && errorBlob->GetBufferPointer()) {
                compileError += " ";
                compileError += static_cast<const char *>(errorBlob->GetBufferPointer());
            }
            releaseCom(shaderBlob);
            releaseCom(errorBlob);
            releaseCom(m_loraShader);
            releaseCom(m_context);
            releaseCom(m_device);
            m_status = compileError;
            if (status) {
                *status = m_status;
            }
            return false;
        }

        hr = m_device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                           shaderBlob->GetBufferSize(),
                                           nullptr,
                                           &m_textScanShader);
        releaseCom(shaderBlob);
        releaseCom(errorBlob);
        if (FAILED(hr)) {
            releaseCom(m_loraShader);
            releaseCom(m_context);
            releaseCom(m_device);
            m_status = hresultMessage("Direct3D parser scan shader creation", hr);
            if (status) {
                *status = m_status;
            }
            return false;
        }

        m_ready = true;
        m_status = "Direct3D 11 local GPU LoRA compute and parser scan ready.";
        if (status) {
            *status = m_status;
        }
        return true;
    }

    bool m_attempted = false;
    bool m_ready = false;
    std::string m_status = "Direct3D 11 local GPU has not been checked yet.";
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;
    ID3D11ComputeShader *m_loraShader = nullptr;
    ID3D11ComputeShader *m_textScanShader = nullptr;
};

#endif

}

LoraAdapter::LoraAdapter(int rank, double alpha)
    : m_rank(clampedRank(rank))
    , m_alpha(alpha)
{
}

void LoraAdapter::configure(int rank, double alpha)
{
    rank = clampedRank(rank);
    if (rank != m_rank) {
        clear();
    }
    m_rank = rank;
    m_alpha = alpha;
}

void LoraAdapter::clear()
{
    m_a.clear();
    m_b.clear();
    m_trainedPairs.clear();
}

int LoraAdapter::rank() const
{
    return m_rank;
}

double LoraAdapter::alpha() const
{
    return m_alpha;
}

int LoraAdapter::pairCount() const
{
    return static_cast<int>(m_trainedPairs.size());
}

std::vector<double> LoraAdapter::initializedVector(const std::string &token, int salt) const
{
    std::vector<double> values(static_cast<size_t>(m_rank));
    std::hash<std::string> hash;
    for (int i = 0; i < m_rank; ++i) {
        const size_t h = hash(token + "#" + std::to_string(salt) + "#" + std::to_string(i));
        const double unit = static_cast<double>(h % 2001) / 1000.0 - 1.0;
        values[static_cast<size_t>(i)] = unit * 0.01;
    }
    return values;
}

std::vector<double> &LoraAdapter::ensureVector(std::map<std::string, std::vector<double>> &matrix,
                                               const std::string &token,
                                               int salt)
{
    auto it = matrix.find(token);
    if (it == matrix.end() || static_cast<int>(it->second.size()) != m_rank) {
        it = matrix.insert_or_assign(token, initializedVector(token, salt)).first;
    }
    return it->second;
}

double LoraAdapter::delta(const std::string &from, const std::string &to) const
{
    auto aIt = m_a.find(from);
    auto bIt = m_b.find(to);
    if (aIt == m_a.end() || bIt == m_b.end()) {
        return 0.0;
    }

    double dot = 0.0;
    for (int i = 0; i < m_rank; ++i) {
        dot += aIt->second[static_cast<size_t>(i)] * bIt->second[static_cast<size_t>(i)];
    }
    return (m_alpha / static_cast<double>(m_rank)) * dot;
}

void LoraAdapter::trainPair(const std::string &from,
                            const std::string &to,
                            double targetDelta,
                            double learningRate)
{
    if (from.empty() || to.empty()) {
        return;
    }

    std::vector<double> &a = ensureVector(m_a, from, 11);
    std::vector<double> &b = ensureVector(m_b, to, 29);
    const std::vector<double> oldA = a;
    const double current = delta(from, to);
    const double error = clipped(targetDelta - current, -4.0, 4.0);
    const double scale = learningRate * error * (m_alpha / static_cast<double>(m_rank));

    for (int i = 0; i < m_rank; ++i) {
        const size_t idx = static_cast<size_t>(i);
        a[idx] = clipped(a[idx] + scale * b[idx], -8.0, 8.0);
        b[idx] = clipped(b[idx] + scale * oldA[idx], -8.0, 8.0);
    }

    m_trainedPairs.insert({from, to});
}

void LoraAdapter::trainSequence(const std::vector<std::string> &tokens,
                                int epochs,
                                double learningRate,
                                double salience)
{
    if (tokens.size() < 2) {
        return;
    }

    epochs = std::max(1, std::min(epochs, 64));
    learningRate = clipped(learningRate, 0.001, 1.0);
    salience = clipped(salience, 0.1, 8.0);

    for (int epoch = 0; epoch < epochs; ++epoch) {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            trainPair(tokens[i], tokens[i + 1], salience, learningRate);
        }
    }
}

bool LoraAdapter::trainSequenceGpu(const std::vector<std::string> &tokens,
                                   int epochs,
                                   double learningRate,
                                   double salience,
                                   std::string *status)
{
#if defined(_WIN32) && defined(AITRAINER_D3D11_GPU)
    if (tokens.size() < 2) {
        if (status) {
            *status = "Local GPU training skipped: not enough tokens.";
        }
        return false;
    }

    epochs = std::max(1, std::min(epochs, 64));
    learningRate = clipped(learningRate, 0.001, 1.0);
    salience = clipped(salience, 0.1, 8.0);

    std::map<std::string, std::uint32_t> aIndexByToken;
    std::map<std::string, std::uint32_t> bIndexByToken;
    std::vector<std::string> aTokens;
    std::vector<std::string> bTokens;
    std::vector<GpuVector> aVectors;
    std::vector<GpuVector> bVectors;
    std::vector<GpuPairIndex> pairIndices;
    std::vector<std::pair<std::string, std::string>> trainedPairs;

    auto toGpuVector = [this](const std::vector<double> &source) {
        GpuVector vector = {};
        for (int i = 0; i < m_rank && i < 16; ++i) {
            vector.values[i] = static_cast<float>(source[static_cast<size_t>(i)]);
        }
        return vector;
    };

    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string &from = tokens[i];
        const std::string &to = tokens[i + 1];
        if (from.empty() || to.empty()) {
            continue;
        }

        std::vector<double> &a = ensureVector(m_a, from, 11);
        std::vector<double> &b = ensureVector(m_b, to, 29);

        auto aIt = aIndexByToken.find(from);
        if (aIt == aIndexByToken.end()) {
            const std::uint32_t index = static_cast<std::uint32_t>(aVectors.size());
            aIndexByToken[from] = index;
            aTokens.push_back(from);
            aVectors.push_back(toGpuVector(a));
            aIt = aIndexByToken.find(from);
        }

        auto bIt = bIndexByToken.find(to);
        if (bIt == bIndexByToken.end()) {
            const std::uint32_t index = static_cast<std::uint32_t>(bVectors.size());
            bIndexByToken[to] = index;
            bTokens.push_back(to);
            bVectors.push_back(toGpuVector(b));
            bIt = bIndexByToken.find(to);
        }

        pairIndices.push_back({aIt->second, bIt->second});
        trainedPairs.push_back({from, to});
    }

    if (pairIndices.empty()) {
        if (status) {
            *status = "Local GPU training skipped: no valid adjacent token pairs.";
        }
        return false;
    }

    GpuParams params = {};
    params.pairCount = static_cast<std::uint32_t>(pairIndices.size());
    params.epochs = static_cast<std::uint32_t>(epochs);
    params.rank = static_cast<std::uint32_t>(m_rank);
    params.alphaOverRank = static_cast<float>(m_alpha / static_cast<double>(m_rank));
    params.targetDelta = static_cast<float>(salience);
    params.learningRate = static_cast<float>(learningRate);

    std::string gpuStatus;
    if (!D3D11LoraComputeBackend::instance().trainSequence(pairIndices,
                                                           aVectors,
                                                           bVectors,
                                                           params,
                                                           &gpuStatus)) {
        if (status) {
            *status = gpuStatus;
        }
        return false;
    }

    for (size_t i = 0; i < aTokens.size(); ++i) {
        std::vector<double> &target = ensureVector(m_a, aTokens[i], 11);
        for (int r = 0; r < m_rank; ++r) {
            target[static_cast<size_t>(r)] = aVectors[i].values[r];
        }
    }

    for (size_t i = 0; i < bTokens.size(); ++i) {
        std::vector<double> &target = ensureVector(m_b, bTokens[i], 29);
        for (int r = 0; r < m_rank; ++r) {
            target[static_cast<size_t>(r)] = bVectors[i].values[r];
        }
    }

    for (const auto &pair : trainedPairs) {
        m_trainedPairs.insert(pair);
    }

    if (status) {
        std::ostringstream out;
        out << "Direct3D 11 local GPU LoRA trained "
            << pairIndices.size() << " ordered pairs for "
            << epochs << " epochs.";
        *status = out.str();
    }
    return true;
#else
    (void)tokens;
    (void)epochs;
    (void)learningRate;
    (void)salience;
    if (status) {
        *status = "Local GPU training is unavailable in this build; Direct3D 11 compute support was not compiled.";
    }
    return false;
#endif
}

bool LoraAdapter::localGpuAvailable(std::string *status)
{
#if defined(_WIN32) && defined(AITRAINER_D3D11_GPU)
    return D3D11LoraComputeBackend::instance().available(status);
#else
    if (status) {
        *status = "Local GPU training is unavailable in this build; Direct3D 11 compute support was not compiled.";
    }
    return false;
#endif
}

bool LoraAdapter::localGpuDatasetScan(const std::string &text, std::string *status)
{
#if defined(_WIN32) && defined(AITRAINER_D3D11_GPU)
    return D3D11LoraComputeBackend::instance().scanText(text, status);
#else
    (void)text;
    if (status) {
        *status = "Local GPU parser scan is unavailable in this build; Direct3D 11 compute support was not compiled.";
    }
    return false;
#endif
}

std::vector<std::pair<std::pair<std::string, std::string>, double>> LoraAdapter::materializedDeltas(double minimumDelta) const
{
    std::vector<std::pair<std::pair<std::string, std::string>, double>> deltas;
    for (const auto &pair : m_trainedPairs) {
        const double value = delta(pair.first, pair.second);
        if (value >= minimumDelta) {
            deltas.push_back({pair, value});
        }
    }
    return deltas;
}

bool LoraAdapter::save(const std::string &filePath) const
{
    std::ofstream out(filePath);
    if (!out.is_open()) {
        return false;
    }

    out << "AITRAINER_LORA_V1 " << m_rank << " " << m_alpha << "\n";
    for (const auto &item : m_a) {
        out << "A " << item.first;
        for (double value : item.second) {
            out << " " << value;
        }
        out << "\n";
    }
    for (const auto &item : m_b) {
        out << "B " << item.first;
        for (double value : item.second) {
            out << " " << value;
        }
        out << "\n";
    }
    for (const auto &pair : m_trainedPairs) {
        out << "P " << pair.first << " " << pair.second << "\n";
    }
    return true;
}

bool LoraAdapter::load(const std::string &filePath)
{
    std::ifstream in(filePath);
    if (!in.is_open()) {
        return true;
    }

    clear();

    std::string header;
    int rank = 4;
    double alpha = 8.0;
    in >> header >> rank >> alpha;
    if (header != "AITRAINER_LORA_V1") {
        clear();
        return false;
    }
    configure(rank, alpha);

    std::string kind;
    while (in >> kind) {
        if (kind == "A" || kind == "B") {
            std::string token;
            in >> token;
            std::vector<double> values(static_cast<size_t>(m_rank), 0.0);
            for (int i = 0; i < m_rank; ++i) {
                in >> values[static_cast<size_t>(i)];
            }
            if (kind == "A") {
                m_a[token] = values;
            } else {
                m_b[token] = values;
            }
        } else if (kind == "P") {
            std::string from;
            std::string to;
            in >> from >> to;
            if (!from.empty() && !to.empty()) {
                m_trainedPairs.insert({from, to});
            }
        } else {
            std::string rest;
            std::getline(in, rest);
        }
    }

    return true;
}
