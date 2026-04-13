#version 450
layout(location=0) in vec3 fragColor;
layout(location=1) in vec3 fragNormal;
layout(location=0) out vec4 outColor;

void main() {
    vec3 n = normalize(fragNormal);
    float light = max(dot(n, normalize(vec3(1.0,1.0,1.0))), 0.1);
    outColor = vec4(fragColor, 1.0);
}
