#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// NOTE: Add your custom variables here

#define     MAX_LIGHTS              16
#define     LIGHT_DIRECTIONAL       0
#define     LIGHT_POINT             1
#define     LIGHT_SPOT              2

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float attenuation;
    float innerCutoff;
    float outerCutoff;
};

// Input lighting values
uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

// Shadow mapping for the sun light
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform int shadowMapResolution;
uniform int shadowPass;

// Spot light shadow mapping
uniform sampler2D spotShadowMap;
uniform mat4 spotLightVP;
uniform int spotShadowMapResolution;

float ShadowCalc(vec3 worldPos, vec3 normal, vec3 lightDir, sampler2D smap, mat4 vp, int res)
{
    vec4 lp = vp * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;

    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.0001);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(res);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float d = texture(smap, proj.xy + vec2(x,y) * texel).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}


void main()
{
    if (shadowPass == 1) { finalColor = vec4(1.0); return; }
    // Texel color fetching from texture sampler
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    vec4 tint = colDiffuse*fragColor;

    // NOTE: Implement here your fragment shader code

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        vec3 light = vec3(0.0);
        float attenuation = 1.0;
        
        if (lights[i].enabled == 1)
        {

            if (lights[i].type == LIGHT_DIRECTIONAL)
            {
                light = -normalize(lights[i].target - lights[i].position);
                float shadow = ShadowCalc(fragPosition, normal, light, shadowMap, lightVP, shadowMapResolution);
                attenuation *= (1.0 - shadow);
            }


            if (lights[i].type == LIGHT_POINT)
            {
                vec3 toLight = lights[i].position - fragPosition;
                float dist = length(toLight);
                light = normalize(toLight);
                attenuation = 1.0 / (1.0 + lights[i].attenuation * dist * dist);

            }

            if (lights[i].type == LIGHT_SPOT){
                vec3 toLight = lights[i].position - fragPosition;
                float dist = length(toLight);
                light = normalize(toLight);

                attenuation = 1.0 / (1.0 + lights[i].attenuation * dist * dist);

                vec3 coneAxis = normalize(lights[i].target - lights[i].position);
                float theta = dot(-light, coneAxis);

                float epsilon = lights[i].innerCutoff - lights[i].outerCutoff;
                float coneFactor = clamp((theta - lights[i].outerCutoff)/epsilon, 0.0, 1.0);
                attenuation *= coneFactor;

                float shadow = ShadowCalc(fragPosition, normal, light, spotShadowMap, spotLightVP, spotShadowMapResolution);
                attenuation *= (1.0 - shadow);
            }

            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL*attenuation;


            float specCo = 0.0;
            //if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0); // 16 refers to shine
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 64.0) * 0.3;

            specular += specCo;
        }
    }

    finalColor = (texelColor*((tint + vec4(specular, 1.0))*vec4(lightDot, 1.0)));
    finalColor += texelColor*(ambient/10.0)*tint;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
