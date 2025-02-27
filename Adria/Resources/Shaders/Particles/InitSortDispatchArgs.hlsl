RWBuffer<uint> DispatchArgs : register(u0);

cbuffer NumElementsCBuffer : register(b11)
{
    int4 NumElements;
};

[numthreads(1, 1, 1)]
void InitSortDispatchArgsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    DispatchArgs[0] = ((NumElements - 1) >> 9) + 1;
    DispatchArgs[1] = 1;
    DispatchArgs[2] = 1;
    DispatchArgs[3] = 0;
}