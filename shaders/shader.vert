#version 450

// We need to pass the per-vertex colors to the fragment shader so it can output the interpolated values
layout(location = 0) out vec3 fragColor;

// The hassle of creating a vertex buffer with Vulkan ain't worth it for now so we just put it directly in the shader instead for now
vec2 positions[3] = vec2[](
     vec2(.0, -.5),
     vec2(.5, .5),
     vec2(-.5, .5)
);

// We want to specify a distinct color for each of the 3 vertices
vec3 colors[3] = vec3[](
     vec3(1., 0., 0.),
     vec3(0., 1., 0.),
     vec3(0., 0., 1.)
);

void main() {
     gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
     fragColor = colors[gl_VertexIndex];
}