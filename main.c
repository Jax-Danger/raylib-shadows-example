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

// -----------------------------------------------------------------------------
// Shadow caster — bundles a depth target, the camera that renders into it,
// and the shader bindings used to sample it. Sun and spot use the same struct.
// -----------------------------------------------------------------------------
typedef struct {
    Camera3D camera;
    RenderTexture2D target;   // depth-only FBO; depth texture is at target.depth.id
    int vpLoc;
    int mapLoc;
    int slot;
} ShadowCaster;

static int lightsCount = 0;


static Light CreateLight(int type, Vector3 position, Vector3 target, Color color, float attenuation, Shader shader);
static void UpdateLightValues(Shader shader, Light light);
static RenderTexture2D LoadShadowmapRenderTexture(int width, int height);
static void UnloadShadowmapRenderTexture(RenderTexture2D t);
static ShadowCaster CreateShadowCaster(Shader shader, int resolution,
    const char *vpName, const char *mapName, const char *resName, int slot, int projection, float fovy);
static void RenderShadowPass(ShadowCaster sc, Shader shader,
                             Model plane, Model cube,
                             Vector3 cubePositions[], int cubeCount);
static void BindShadowMap(ShadowCaster sc);
static void DrawScene(Model plane, Model cube, Vector3 cubePositions[], int cubeCount);

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 400;
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
    Shader shader = LoadShader("resources/shaders/lighting.vs", "resources/shaders/lighting.fs");
    shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

    float ambient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SetShaderValue(shader, GetShaderLocation(shader, "ambient"), ambient, SHADER_UNIFORM_VEC4);

    int shadowPassLoc = GetShaderLocation(shader, "shadowPass");

    // ---- Lights ----
    Light lights[MAX_LIGHTS] = { 0 };

    // 0: directional sun
    Vector3 sunDir = Vector3Normalize((Vector3){ -1.0f, -1.0f, -0.5f });
    lights[0] = CreateLight(LIGHT_DIRECTIONAL, (Vector3){0, 0, 0}, sunDir, WHITE, 0.0f, shader);
    UpdateLightValues(shader, lights[0]);

    // 1: spot light shining down
    Vector3 spotPos    = { 10.0f, 10.0f, 5.0f };
    Vector3 spotDir    = { 0.0f, -1.0f, 0.0f };
    Vector3 spotTarget = Vector3Add(spotPos, Vector3Normalize(spotDir));
    lights[1] = CreateLight(LIGHT_SPOT, spotPos, spotTarget, WHITE,
                            1.0f / (30.0f * 30.0f), shader);
    lights[1].innerCutoff = cosf(30.0f * DEG2RAD);
    lights[1].outerCutoff = cosf(45.0f * DEG2RAD);
    UpdateLightValues(shader, lights[1]);

    // ---- Shadow casters ----
    ShadowCaster sun  = CreateShadowCaster(shader, SUN_SHADOW_RES,
                                           "lightVP", "shadowMap", "shadowMapResolution",
                                           10, CAMERA_ORTHOGRAPHIC, 30.0f);

    ShadowCaster spot = CreateShadowCaster(shader, SPOT_SHADOW_RES,
                                           "spotLightVP", "spotShadowMap", "spotShadowMapResolution",
                                           11, CAMERA_PERSPECTIVE,
                                           acosf(lights[1].outerCutoff) * RAD2DEG * 2.0f);
    spot.camera.position = lights[1].position;
    spot.camera.target   = lights[1].target;
    spot.camera.up       = (Vector3){ 1, 0, 0 };   // safe up — light points along Y

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

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        // ---- Update ----
        UpdateCamera(&camera, CAMERA_FREE);
        float t = GetTime();

        // Rotate the sun direction (slow arc across the sky)
        sunDir = Vector3Normalize((Vector3){ cosf(t * 0.3f), -1.0f, sinf(t * 0.3f) });
        lights[0].target = sunDir;   // position stays at origin; direction = target - (0,0,0)
        UpdateLightValues(shader, lights[0]);

        // Orbit the spot light, always shining straight down
        lights[1].position = (Vector3){ cosf(t) * 8.0f, 10.0f, sinf(t) * 8.0f };
        lights[1].target   = Vector3Add(lights[1].position, (Vector3){ 0.0f, -1.0f, 0.0f });
        UpdateLightValues(shader, lights[1]);

        spot.camera.position = lights[1].position;
        spot.camera.target   = lights[1].target;

        // Push view position for specular highlights
        float viewPos[3] = { camera.position.x, camera.position.y, camera.position.z };
        SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], viewPos, SHADER_UNIFORM_VEC3);

        // Sun shadow camera follows the player, aimed along the (now-rotating) sunDir
        sun.camera.position = Vector3Add(camera.position, Vector3Scale(sunDir, -30.0f));
        sun.camera.target   = camera.position;

        // ---- Shadow passes ----
        int shadowPassFlag = 1;
        SetShaderValue(shader, shadowPassLoc, &shadowPassFlag, SHADER_UNIFORM_INT);

        RenderShadowPass(sun,  shader, plane, cube, cubePositions, 4);
        RenderShadowPass(spot, shader, plane, cube, cubePositions, 4);

        // ---- Bind shadow maps for the main pass ----
        shadowPassFlag = 0;
        SetShaderValue(shader, shadowPassLoc, &shadowPassFlag, SHADER_UNIFORM_INT);

        rlEnableShader(shader.id);
        BindShadowMap(sun);
        BindShadowMap(spot);

        // ---- Main camera (the visible frame) ----
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
    UnloadShadowmapRenderTexture(sun.target);
    UnloadShadowmapRenderTexture(spot.target);
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
// ShadowCaster helpers — used identically for sun and spot
// -----------------------------------------------------------------------------
static ShadowCaster CreateShadowCaster(Shader shader, int resolution,
                                       const char *vpName, const char *mapName,
                                       const char *resName, int slot,
                                       int projection, float fovy)
{
    ShadowCaster sc = { 0 };
    sc.target = LoadShadowmapRenderTexture(resolution, resolution);
    sc.vpLoc  = GetShaderLocation(shader, vpName);
    sc.mapLoc = GetShaderLocation(shader, mapName);
    sc.slot   = slot;
    SetShaderValue(shader, GetShaderLocation(shader, resName),
                   &resolution, SHADER_UNIFORM_INT);

    sc.camera.up         = (Vector3){ 0, 1, 0 };
    sc.camera.fovy       = fovy;
    sc.camera.projection = projection;
    return sc;
}

static void RenderShadowPass(ShadowCaster sc, Shader shader,
                             Model plane, Model cube,
                             Vector3 cubePositions[], int cubeCount)
{
    BeginTextureMode(sc.target);
        ClearBackground(WHITE);
        BeginMode3D(sc.camera);
            Matrix vp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
            SetShaderValueMatrix(shader, sc.vpLoc, vp);
            DrawScene(plane, cube, cubePositions, cubeCount);
        EndMode3D();
    EndTextureMode();
}

static void BindShadowMap(ShadowCaster sc)
{
    rlActiveTextureSlot(sc.slot);
    rlEnableTexture(sc.target.depth.id);
    rlSetUniform(sc.mapLoc, &sc.slot, SHADER_UNIFORM_INT, 1);
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
