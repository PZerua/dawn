struct tint_symbol {
  float4 value : SV_Position;
};

void transpose_ed4bdc() {
  float2x3 res = transpose(float3x2(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
}

tint_symbol vertex_main() {
  transpose_ed4bdc();
  const tint_symbol tint_symbol_1 = {float4(0.0f, 0.0f, 0.0f, 0.0f)};
  return tint_symbol_1;
}

void fragment_main() {
  transpose_ed4bdc();
  return;
}

[numthreads(1, 1, 1)]
void compute_main() {
  transpose_ed4bdc();
  return;
}

