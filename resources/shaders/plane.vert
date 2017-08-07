#version 450
#extension GL_ARB_separate_shader_objects : enable

// Attributes
layout(location = 0) in vec3 v;
layout(location = 1) in vec3 n;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 tang;
layout(location = 4) in vec3 binor;

// Uniform: the light structure (position in view space)
layout(set = 0, binding = 0) uniform Uniforms {
    mat4 camProjection;
    mat4 camView;
    mat4 rotationOnlyView;
    mat4 camViewInverse;
    mat4 lightProjection;
    mat4 lightView;
	vec4 lightPosition;
	vec4 lightIa;
	vec4 lightId;
	vec4 lightIs;
	float lightShininess;
} uniforms;

layout(push_constant) uniform Model {
    mat4 matrix;
    mat3 normalMatrix;
} model;


// Output: tangent space matrix, position in view space and uv.
layout(location = 0) out mat3 outTbn;
layout(location = 3) out vec3 outPosition;
layout(location = 4) out vec2 outUV;
layout(location = 5) out vec3 outLightSpacePosition;
layout(location = 6) out vec3 outModelPosition;
layout(location = 7) out vec3 outTangentSpacePosition;
layout(location = 8) out vec3 outTangentSpaceView;
layout(location = 9) out vec3 outTangentSpaceLight;


void main(){
	// We multiply the coordinates by the MVP matrix, and ouput the result.
	gl_Position = uniforms.camProjection * uniforms.camView * model.matrix * vec4(v, 1.0);
	
	outPosition = (uniforms.camProjection * uniforms.camView * vec4(v,1.0)).xyz;
	
	outUV = uv;
	
	// Compute the TBN matrix (from tangent space to view space).
	vec3 T = normalize(model.normalMatrix * tang);
	vec3 B = normalize(model.normalMatrix * binor);
	vec3 N = normalize(model.normalMatrix * n);
	outTbn = mat3(T, B, N);
	
	// Compute position in light space
	outLightSpacePosition = 0.5*(uniforms.lightProjection * uniforms.lightView * model.matrix * vec4(v,1.0)).xyz + 0.5;
	
	outModelPosition = v;
	
	outTangentSpacePosition = transpose(outTbn) * outPosition;
	
	outTangentSpaceView = transpose(outTbn) * vec3(0.0);
	
	outTangentSpaceLight = transpose(outTbn) * uniforms.lightPosition.xyz;
	
}
