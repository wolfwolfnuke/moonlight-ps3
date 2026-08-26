//!ATTENTION: This file is only a reference shader for the future RSX texturing
//! path. It is NOT compiled by the project Makefile (RSX shaders are embedded
//! as ucode via cgtool/rsxtool at build time). It documents the intended
//! YUV420 -> RGB conversion done on the GPU instead of the current CPU blit.

struct input {
    float2 uv;
};

struct output {
    float4 color;
};

uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;

void main(in struct input IN, out struct output OUT)
{
    float Y = tex2D(tex_y, IN.uv).r - 0.0625;
    float U = tex2D(tex_u, IN.uv).r - 0.5;
    float V = tex2D(tex_v, IN.uv).r - 0.5;

    float r = 1.164 * Y + 1.596 * V;
    float g = 1.164 * Y - 0.392 * U - 0.813 * V;
    float b = 1.164 * Y + 2.017 * U;

    OUT.color = float4(r, g, b, 1.0);
}
