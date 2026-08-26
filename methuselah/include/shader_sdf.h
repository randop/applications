#pragma once

// GLSL 330 raylib sdf.fs for desktop OpenGL 3.3+
static const char *SHADER_SDF = R"(
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    // Calculate alpha using signed distance field
    float distanceFromOutline = texture(texture0, fragTexCoord).a - 0.5;
    float distanceChangePerFragment = length(vec2(dFdx(distanceFromOutline), dFdy(distanceFromOutline)));
    float alpha = smoothstep(-distanceChangePerFragment, distanceChangePerFragment, distanceFromOutline);

    finalColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
)";
