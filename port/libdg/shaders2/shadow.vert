#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uShadowMVP;
void main() {
    gl_Position = uShadowMVP * vec4(aPos, 1.0);
}