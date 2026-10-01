#pragma once
// Precompiled shader registry. All HLSL is compiled at build time by fxc
// (cmake/Shaders.cmake) and embedded in the executable; the list of shader
// ids comes from the generated "bgn_shader_table.inc".

#include <d3d11.h>

#include <array>
#include <string>

#include "platform/Win32.h"

namespace bgn {

enum class ShaderId : int {
#define BGN_SHADER(name, kind) name,
#include "bgn_shader_table.inc"
#undef BGN_SHADER
    Count
};

const char* shaderName(ShaderId id);

class ShaderLibrary {
public:
    bool init(ID3D11Device* device);
    void release();
    ID3D11ComputeShader* cs(ShaderId id) const;
    ID3D11VertexShader* vs(ShaderId id) const;
    ID3D11PixelShader* ps(ShaderId id) const;
    const std::string& error() const { return error_; }

private:
    static constexpr int kCount = static_cast<int>(ShaderId::Count);
    std::array<ComPtr<ID3D11ComputeShader>, kCount> cs_;
    std::array<ComPtr<ID3D11VertexShader>, kCount> vs_;
    std::array<ComPtr<ID3D11PixelShader>, kCount> ps_;
    std::string error_;
};

} // namespace bgn
