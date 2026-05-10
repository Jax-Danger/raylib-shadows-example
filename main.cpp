/*******************************************************************************************
*
*   raylib [shaders] example - Shadow mapping with directional + spot lights
*
*   Demonstrates:
*       - Directional light (sun) with an orthographic shadow map
*       - Spot light with a perspective shadow map
*       - PCF (Percentage Closer Filtering) for soft shadow edges
*
*   Resources: resources/shaders/lighting.vs and lighting.fs
*
********************************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
#define MAX_LIGHTS         16
#define LIGHT_DIRECTIONAL   0
#define LIGHT_POINT         1
#define LIGHT_SPOT          2

#define SUN_SHADOW_RES   2048
#define SPOT_SHADOW_RES  1024

// -----------------------------------------------------------------------------
// Light type — mirrors the GLSL struct in lighting.fs
// -----------------------------------------------------------------------------
typedef struct {
    int type;
    int enabled;
    Vector3 position;
    Vector3 target;
    Color color;
    float attenuation;
    float innerCutoff;     // cosine of inner cone angle (for spot lights)
    float outerCutoff;     // cosine of outer cone angle (for spot lights)

    // Shader uniform locations
    int enabledLoc, typeLoc, positionLoc, targetLoc, colorLoc;
    int attenuationLoc, innerCutoffLoc, outerCutoffLoc;
} Light;

static int lightsCount = 0;


static Light CreateLight(int type, Vector3 position, Vector3 target, Color color, float attenuation, Shader shader);
static void UpdateLightValues(Shader shader, Light light);
static RenderTexture2D LoadShadowmapRenderTexture(int width, int height);
static void UnloadShadowmapRenderTexture(RenderTexture2D t);
static void DrawScene(Model plane, Model cube, Vector3 cubePositions[], int cubeCount);

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "raylib - shadow mapping (sun + spot)");

    DisableCursor();
    // ---- Camera ----
    Camera3D camera = { 0 };
    camera.position   = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target     = (Vector3){  0.0f,  1.0f,  0.0f };
    camera.up         = (Vector3){  0.0f,  1.0f,  0.0f };
    camera.fovy       = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // ---- Shader ----
    Shader shader = LoadShader("resources/shaders/lighting.vs",
                               "resources/shaders/lighting.fs");
    shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

    float ambient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient, SHADER_UNIFORM_VEC4);

    // ---- Sun shadow uniforms + render target ----
    int lightVPLoc    = GetShaderLocation(shader, "lightVP");
    int shadowMapLoc  = GetShaderLocation(shader, "shadowMap");
    int shadowPassLoc = GetShaderLocation(shader, "shadowPass");
    int sunRes        = SUN_SHADOW_RES;
    SetShaderValue(shader, GetShaderLocation(shader, "shadowMapResolution"),
                   &sunRes, SHADER_UNIFORM_INT);
    RenderTexture2D sunShadow = LoadShadowmapRenderTexture(SUN_SHADOW_RES, SUN_SHADOW_RES);

    // ---- Spot shadow uniforms + render target ----
    int spotLightVPLoc   = GetShaderLocation(shader, "spotLightVP");
    int spotShadowMapLoc = GetShaderLocation(shader, "spotShadowMap");
    int spotRes          = SPOT_SHADOW_RES;
    SetShaderValue(shader, GetShaderLocation(shader, "spotShadowMapResolution"),
                   &spotRes, SHADER_UNIFORM_INT);
    RenderTexture2D spotShadow = LoadShadowmapRenderTexture(SPOT_SHADOW_RES, SPOT_SHADOW_RES);

    // ---- Lights ----
    Light lights[MAX_LIGHTS] = { 0 };

    // 0: directional sun
    Vector3 sunDir = Vector3Normalize((Vector3){ -1.0f, -1.0f, -0.5f });
    lights[0] = CreateLight(LIGHT_DIRECTIONAL, (Vector3){0, 0, 0}, sunDir, WHITE, 0.0f, shader);
    UpdateLightValues(shader, lights[0]);

    // 1: spot light shining down
    Vector3 spotPos    = { 5.0f, 10.0f, 5.0f };
    Vector3 spotDir    = { 0.0f, -1.0f, 0.0f };
    Vector3 spotTarget = Vector3Add(spotPos, Vector3Normalize(spotDir));
    lights[1] = CreateLight(LIGHT_SPOT, spotPos, spotTarget, WHITE,
                            1.0f / (30.0f * 30.0f), shader);
    lights[1].innerCutoff = cosf(30.0f * DEG2RAD);
    lights[1].outerCutoff = cosf(45.0f * DEG2RAD);
    UpdateLightValues(shader, lights[1]);

    // ---- Scene ----
    Model plane = LoadModelFromMesh(GenMeshPlane(20.0f, 20.0f, 1, 1));
    Model cube  = LoadModelFromMesh(GenMeshCube(1.0f, 2.0f, 1.0f));
    plane.materials[0].shader = shader;
    cube.materials[0].shader  = shader;

    Vector3 cubePositions[4] = {
        { -3.0f, 1.0f,  0.0f },
        {  0.0f, 1.0f, -2.0f },
        {  3.0f, 1.0f,  1.0f },
        {  1.0f, 1.0f,  3.0f },
    };

    // ---- Shadow cameras ----
    // Sun: orthographic, follows the player camera each frame
    Camera3D sunCam = { 0 };
    sunCam.up         = (Vector3){ 0, 1, 0 };
    sunCam.fovy       = 30.0f;       // ortho half-extent
    sunCam.projection = CAMERA_ORTHOGRAPHIC;

    // Spot: perspective, FOV = outer cone angle, fixed in world (light doesn't move here)
    Camera3D spotCam = { 0 };
    spotCam.position   = lights[1].position;
    spotCam.target     = lights[1].target;
    spotCam.up         = (Vector3){ 1, 0, 0 };   // safe up — light points along Y
    spotCam.fovy       = acosf(lights[1].outerCutoff) * RAD2DEG * 2.0f;
    spotCam.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        // ---- Update ----
        UpdateCamera(&camera, CAMERA_FREE);

        // Push view position for specular highlights
        float viewPos[3] = { camera.position.x, camera.position.y, camera.position.z };
        SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], viewPos, SHADER_UNIFORM_VEC3);

        // Keep the sun shadow centered on whatever the player is looking at
        sunCam.position = Vector3Add(camera.position, Vector3Scale(sunDir, -30.0f));
        sunCam.target   = camera.position;

        // ---- Pass 1: Sun shadow ----
        int shadowPassFlag = 1;
        SetShaderValue(shader, shadowPassLoc, &shadowPassFlag, SHADER_UNIFORM_INT);

        BeginTextureMode(sunShadow);
            ClearBackground(WHITE);
            BeginMode3D(sunCam);
                Matrix sunView = rlGetMatrixModelview();
                Matrix sunProj = rlGetMatrixProjection();
                Matrix sunVP   = MatrixMultiply(sunView, sunProj);
                SetShaderValueMatrix(shader, lightVPLoc, sunVP);

                DrawScene(plane, cube, cubePositions, 4);
            EndMode3D();
        EndTextureMode();

        // ---- Pass 2: Spot shadow ----
        BeginTextureMode(spotShadow);
            ClearBackground(WHITE);
            BeginMode3D(spotCam);
                Matrix spotView = rlGetMatrixModelview();
                Matrix spotProj = rlGetMatrixProjection();
                Matrix spotVP   = MatrixMultiply(spotView, spotProj);
                SetShaderValueMatrix(shader, spotLightVPLoc, spotVP);

                DrawScene(plane, cube, cubePositions, 4);
            EndMode3D();
        EndTextureMode();

        // ---- Bind shadow maps to free texture slots ----
        shadowPassFlag = 0;
        SetShaderValue(shader, shadowPassLoc, &shadowPassFlag, SHADER_UNIFORM_INT);

        rlEnableShader(shader.id);

        int sunSlot = 10;
        rlActiveTextureSlot(sunSlot);
        rlEnableTexture(sunShadow.depth.id);
        rlSetUniform(shadowMapLoc, &sunSlot, SHADER_UNIFORM_INT, 1);

        int spotSlot = 11;
        rlActiveTextureSlot(spotSlot);
        rlEnableTexture(spotShadow.depth.id);
        rlSetUniform(spotShadowMapLoc, &spotSlot, SHADER_UNIFORM_INT, 1);

        // ---- Pass 3: Main camera (the visible frame) ----
        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginMode3D(camera);
                DrawScene(plane, cube, cubePositions, 4);
            EndMode3D();

            DrawText("Shadow mapping: directional sun + spot light", 10, 10, 18, BLACK);
            DrawFPS(10, 35);
        EndDrawing();
    }

    // ---- Cleanup ----
    UnloadModel(plane);
    UnloadModel(cube);
    UnloadShader(shader);
    UnloadShadowmapRenderTexture(sunShadow);
    UnloadShadowmapRenderTexture(spotShadow);
    CloseWindow();

    return 0;
}

// -----------------------------------------------------------------------------
// Light helpers
// -----------------------------------------------------------------------------
static Light CreateLight(int type, Vector3 position, Vector3 target,
                         Color color, float attenuation, Shader shader)
{
    Light light = { 0 };
    if (lightsCount >= MAX_LIGHTS) return light;

    light.enabled     = 1;
    light.type        = type;
    light.position    = position;
    light.target      = target;
    light.color       = color;
    light.attenuation = attenuation;
    light.innerCutoff = -1.0f;
    light.outerCutoff = -1.0f;

    light.enabledLoc     = GetShaderLocation(shader, TextFormat("lights[%i].enabled", lightsCount));
    light.typeLoc        = GetShaderLocation(shader, TextFormat("lights[%i].type", lightsCount));
    light.positionLoc    = GetShaderLocation(shader, TextFormat("lights[%i].position", lightsCount));
    light.targetLoc      = GetShaderLocation(shader, TextFormat("lights[%i].target", lightsCount));
    light.colorLoc       = GetShaderLocation(shader, TextFormat("lights[%i].color", lightsCount));
    light.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", lightsCount));
    light.innerCutoffLoc = GetShaderLocation(shader, TextFormat("lights[%i].innerCutoff", lightsCount));
    light.outerCutoffLoc = GetShaderLocation(shader, TextFormat("lights[%i].outerCutoff", lightsCount));

    lightsCount++;
    return light;
}

static void UpdateLightValues(Shader shader, Light light)
{
    SetShaderValue(shader, light.enabledLoc, &light.enabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, light.typeLoc,    &light.type,    SHADER_UNIFORM_INT);

    float pos[3] = { light.position.x, light.position.y, light.position.z };
    SetShaderValue(shader, light.positionLoc, pos, SHADER_UNIFORM_VEC3);

    float tgt[3] = { light.target.x, light.target.y, light.target.z };
    SetShaderValue(shader, light.targetLoc, tgt, SHADER_UNIFORM_VEC3);

    float col[4] = { light.color.r/255.0f, light.color.g/255.0f,
                     light.color.b/255.0f, light.color.a/255.0f };
    SetShaderValue(shader, light.colorLoc, col, SHADER_UNIFORM_VEC4);

    SetShaderValue(shader, light.attenuationLoc, &light.attenuation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, light.innerCutoffLoc, &light.innerCutoff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, light.outerCutoffLoc, &light.outerCutoff, SHADER_UNIFORM_FLOAT);
}

// -----------------------------------------------------------------------------
// Shadow map helpers — a RenderTexture2D with only a depth attachment
// -----------------------------------------------------------------------------
static RenderTexture2D LoadShadowmapRenderTexture(int width, int height)
{
    RenderTexture2D t = { 0 };
    t.id = rlLoadFramebuffer();
    t.texture.width  = width;
    t.texture.height = height;
    rlEnableFramebuffer(t.id);
    t.depth.id      = rlLoadTextureDepth(width, height, false);
    t.depth.width   = width;
    t.depth.height  = height;
    t.depth.format  = 19;       // DEPTH_COMPONENT_24BIT
    t.depth.mipmaps = 1;
    rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    return t;
}

static void UnloadShadowmapRenderTexture(RenderTexture2D t)
{
    rlUnloadTexture(t.depth.id);
    rlUnloadFramebuffer(t.id);
}

// -----------------------------------------------------------------------------
// Scene — a ground plane + a few cubes. Used by every render pass.
// -----------------------------------------------------------------------------
static void DrawScene(Model plane, Model cube, Vector3 cubePositions[], int cubeCount)
{
    DrawModel(plane, (Vector3){0, 0, 0}, 1.0f, LIGHTGRAY);
    for (int i = 0; i < cubeCount; i++)
        DrawModel(cube, cubePositions[i], 1.0f, RED);
}
