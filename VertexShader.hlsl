struct VS_INPUT
{
    float4 pos : POSITION;
    float2 texCoord : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

struct ConstantBufferData
{
    float4x4 mvp;
};

ConstantBuffer<ConstantBufferData> wvpMat : register(b0);

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(input.pos, wvpMat.mvp);
    output.texCoord = input.texCoord;
    return output;
}