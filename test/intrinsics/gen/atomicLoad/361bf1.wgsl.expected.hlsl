groupshared uint arg_0;

void atomicLoad_361bf1() {
  uint atomic_result = 0u;
  InterlockedOr(arg_0, 0, atomic_result);
  uint res = atomic_result;
}

struct tint_symbol_1 {
  uint local_invocation_index : SV_GroupIndex;
};

[numthreads(1, 1, 1)]
void compute_main(tint_symbol_1 tint_symbol) {
  const uint local_invocation_index = tint_symbol.local_invocation_index;
  {
    uint atomic_result_1 = 0u;
    InterlockedExchange(arg_0, 0u, atomic_result_1);
  }
  GroupMemoryBarrierWithGroupSync();
  atomicLoad_361bf1();
  return;
}
