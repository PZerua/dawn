#version 310 es

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void unused_entry_point() {
  return;
}
float t = 0.0f;
vec2 m() {
  t = 1.0f;
  return vec2(t);
}

void f() {
  vec2 tint_symbol = m();
  ivec2 v = ivec2(tint_symbol);
}

