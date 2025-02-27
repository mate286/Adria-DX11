
struct VSToPS
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEX;
};


VSToPS FullscreenQuad(uint vertexId : SV_VERTEXID)
{
    int2 texCoord = int2(vertexId & 1, vertexId >> 1);
    VSToPS output = (VSToPS)0;
    output.Tex = float2(texCoord);
    output.Pos = float4(2 * (texCoord.x - 0.5f), -2 * (texCoord.y - 0.5f), 0.0, 1);
    return output;
}