@group(1) @binding(0) var arg_0 : texture_storage_1d<bgra8unorm, write>;

fn textureDimensions_84f363() {
  var res : u32 = textureDimensions(arg_0);
}

@vertex
fn vertex_main() -> @builtin(position) vec4<f32> {
  textureDimensions_84f363();
  return vec4<f32>();
}

@fragment
fn fragment_main() {
  textureDimensions_84f363();
}

@compute @workgroup_size(1)
fn compute_main() {
  textureDimensions_84f363();
}
