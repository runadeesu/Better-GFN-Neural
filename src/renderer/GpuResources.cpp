#include "renderer/GpuResources.h"

#include <algorithm>
#include <vector>

namespace bgn {

static UINT bytesPerPixel(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
    case DXGI_FORMAT_R16_FLOAT: return 2;
    case DXGI_FORMAT_R32_FLOAT: return 4;
    case DXGI_FORMAT_R16G16_FLOAT: return 4;
    default: return 4;
    }
}

bool GpuTexture::create(ID3D11Device* dev, int w, int h, DXGI_FORMAT fmt, bool withUav, bool withRtv) {
    reset();
    if (w <= 0 || h <= 0) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(w);
    d.Height = UINT(h);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (withUav ? D3D11_BIND_UNORDERED_ACCESS : 0) | (withRtv ? D3D11_BIND_RENDER_TARGET : 0);
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex))) return false;
    if (FAILED(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return false;
    if (withUav && FAILED(dev->CreateUnorderedAccessView(tex.Get(), nullptr, &uav))) return false;
    if (withRtv && FAILED(dev->CreateRenderTargetView(tex.Get(), nullptr, &rtv))) return false;
    width = w;
    height = h;
    format = fmt;
    return true;
}

bool GpuTexture::ensure(ID3D11Device* dev, int w, int h, DXGI_FORMAT fmt, bool withUav, bool withRtv) {
    if (tex && width == w && height == h && format == fmt && (!withUav || uav) && (!withRtv || rtv)) return true;
    return create(dev, w, h, fmt, withUav, withRtv);
}

void GpuTexture::reset() {
    rtv.Reset();
    uav.Reset();
    srv.Reset();
    tex.Reset();
    width = height = 0;
    format = DXGI_FORMAT_UNKNOWN;
}

size_t GpuTexture::bytes() const { return size_t(width) * size_t(height) * bytesPerPixel(format); }

bool ReadbackBuffer::create(ID3D11Device* dev, UINT bytes, int ringSize) {
    reset();
    bytes_ = (bytes + 3) & ~3u;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = bytes_;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (FAILED(dev->CreateBuffer(&d, nullptr, &buffer_))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_R32_TYPELESS;
    u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = bytes_ / 4;
    u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (FAILED(dev->CreateUnorderedAccessView(buffer_.Get(), &u, &uav_))) return false;
    D3D11_BUFFER_DESC s{};
    s.ByteWidth = bytes_;
    s.Usage = D3D11_USAGE_STAGING;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_.resize(size_t(ringSize));
    pending_.assign(size_t(ringSize), false);
    for (auto& st : staging_)
        if (FAILED(dev->CreateBuffer(&s, nullptr, &st))) return false;
    return true;
}

void ReadbackBuffer::reset() {
    buffer_.Reset();
    uav_.Reset();
    staging_.clear();
    pending_.clear();
    write_ = read_ = 0;
}

void ReadbackBuffer::clear(ID3D11DeviceContext* ctx) {
    const UINT zero[4] = {0, 0, 0, 0};
    if (uav_) ctx->ClearUnorderedAccessViewUint(uav_.Get(), zero);
}

void ReadbackBuffer::requestReadback(ID3D11DeviceContext* ctx) {
    if (staging_.empty() || pending_[size_t(write_)]) return; // ring full, skip this sample
    ctx->CopyResource(staging_[size_t(write_)].Get(), buffer_.Get());
    pending_[size_t(write_)] = true;
    write_ = (write_ + 1) % int(staging_.size());
}

bool ReadbackBuffer::tryRead(ID3D11DeviceContext* ctx, void* dst, UINT bytes) {
    if (staging_.empty() || !pending_[size_t(read_)]) return false;
    D3D11_MAPPED_SUBRESOURCE m{};
    HRESULT hr = ctx->Map(staging_[size_t(read_)].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING || FAILED(hr)) return false;
    memcpy(dst, m.pData, std::min(bytes, bytes_));
    ctx->Unmap(staging_[size_t(read_)].Get(), 0);
    pending_[size_t(read_)] = false;
    read_ = (read_ + 1) % int(staging_.size());
    return true;
}

void dispatchCompute(ID3D11DeviceContext* ctx, ID3D11ComputeShader* cs, std::initializer_list<ID3D11ShaderResourceView*> srvs,
                     std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT gx, UINT gy, UINT gz) {
    ID3D11ShaderResourceView* s[8] = {};
    ID3D11UnorderedAccessView* u[8] = {};
    UINT ns = 0, nu = 0;
    for (auto* v : srvs)
        if (ns < 8) s[ns++] = v;
    for (auto* v : uavs)
        if (nu < 8) u[nu++] = v;
    ctx->CSSetShader(cs, nullptr, 0);
    if (ns) ctx->CSSetShaderResources(0, ns, s);
    if (nu) ctx->CSSetUnorderedAccessViews(0, nu, u, nullptr);
    if (gx && gy && gz) ctx->Dispatch(gx, gy, gz);
    ID3D11ShaderResourceView* ns8[8] = {};
    ID3D11UnorderedAccessView* nu8[8] = {};
    if (ns) ctx->CSSetShaderResources(0, ns, ns8);
    if (nu) ctx->CSSetUnorderedAccessViews(0, nu, nu8, nullptr);
}

} // namespace bgn
