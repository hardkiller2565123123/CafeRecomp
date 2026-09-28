#include "wiiu_window.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

static HWND g_window;
static bool g_class_registered;
static bool g_window_closed;
static bool g_window_hidden;
static bool g_has_frame;
static bool g_has_pixels;
static uint32_t g_frame_width;
static uint32_t g_frame_height;
static size_t g_frame_capacity;
static uint8_t* g_frame_pixels;
static uint8_t g_frame_red;
static uint8_t g_frame_green;
static uint8_t g_frame_blue;
static bool g_last_accepted_frame_detailed;
static uint32_t g_uniform_frame_streak;
static uint32_t g_ignored_uniform_frames;
static uint32_t g_input_held;
static uint32_t g_keyboard_held;
static uint32_t g_controller_held;
static uint32_t g_input_pressed;
static uint32_t g_input_released;
static bool g_input_edges_observed;
static float g_left_stick_x;
static float g_left_stick_y;
static float g_controller_stick_x;
static float g_controller_stick_y;
static uint16_t g_touch_x = 2033u;
static uint16_t g_touch_y = 1994u;
static bool g_touch_held;
static bool g_touch_latched;
static bool g_touch_latch_observed;
static uint32_t g_input_log_count;
static char g_reason[128] = "GX2 boot";

typedef DWORD(WINAPI* XInputGetStateProc)(DWORD, XINPUT_STATE*);

static HMODULE g_xinput_module;
static XInputGetStateProc g_xinput_get_state;
static bool g_xinput_initialized;
static bool g_xinput_connected;
static DWORD g_xinput_controller_index;

static ID3D11Device* g_d3d_device;
static ID3D11DeviceContext* g_d3d_context;
static IDXGISwapChain* g_d3d_swap_chain;
static ID3D11RenderTargetView* g_d3d_render_target;
static ID3D11VertexShader* g_d3d_vertex_shader;
static ID3D11PixelShader* g_d3d_pixel_shader;
static ID3D11InputLayout* g_d3d_input_layout;
static ID3D11Buffer* g_d3d_vertex_buffer;
static ID3D11SamplerState* g_d3d_sampler;
static struct { uint32_t word; ID3D11SamplerState* state; } g_effect_samplers[64];
static ID3D11Texture2D* g_d3d_frame_texture;
static ID3D11ShaderResourceView* g_d3d_frame_view;
static ID3D11VertexShader* g_d3d_quad_vertex_shader;
static ID3D11PixelShader* g_d3d_quad_pixel_shader;
static ID3D11PixelShader* g_d3d_surface_pixel_shader;
static ID3D11PixelShader* g_d3d_material_pixel_shader;
static ID3D11Buffer* g_d3d_material_buffer;
static ID3D11VertexShader* g_d3d_post_vs;
static ID3D11PixelShader* g_d3d_post_ps;
static ID3D11InputLayout* g_d3d_post_layout;
static ID3D11Buffer* g_d3d_post_vertices;
static ID3D11Buffer* g_d3d_post_constants;
static ID3D11Buffer* g_d3d_scene_constants;
static struct {ID3D11Buffer* buffer;ID3D11ShaderResourceView* view;uint32_t size;} g_scene_blocks[16];
static struct { char* code; ID3D11PixelShader* shader; uint32_t key; uint64_t used; } g_scene_shaders[128];
static uint64_t g_scene_shader_serial;
static struct { D3D11_BLEND_DESC desc; ID3D11BlendState* state; } g_scene_blends[64];
static ID3D11InputLayout* g_d3d_quad_input_layout;
static ID3D11Buffer* g_d3d_quad_vertex_buffer;
static ID3D11BlendState* g_d3d_quad_blend_state;
static ID3D11RasterizerState* g_d3d_quad_rasterizer_state;
static uint32_t g_d3d_frame_width;
static uint32_t g_d3d_frame_height;
static uint32_t g_d3d_backbuffer_width;
static uint32_t g_d3d_backbuffer_height;
static bool g_d3d_ready;
static bool g_d3d_resize_pending;

enum {
    NATIVE_GPU_SURFACE_LIMIT = 256,
    NATIVE_GPU_TEXTURE_LIMIT = 64,
    NATIVE_GPU_BLEND_CACHE_LIMIT = 16,
    NATIVE_GPU_QUAD_BATCH_LIMIT = 2048,
};

typedef struct {
    bool valid;
    bool contents_valid;
    uint32_t key;
    uint32_t width;
    uint32_t height;
    uint64_t use_serial;
    ID3D11Texture2D* texture;
    ID3D11Texture2D* readback_texture;
    DXGI_FORMAT format;
    ID3D11RenderTargetView* render_target;
    ID3D11ShaderResourceView* view;
} NativeGpuSurface;

typedef struct {
    bool valid;
    const uint8_t* source;
    uint64_t serial;
    uint32_t width;
    uint32_t height;
    uint64_t use_serial;
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* view;
} NativeGpuTexture;

typedef struct {
    bool valid;
    WiiUWindowGpuBlendControl control;
    ID3D11BlendState* state;
} NativeGpuBlendState;

static NativeGpuSurface g_native_gpu_surfaces[NATIVE_GPU_SURFACE_LIMIT];
static NativeGpuTexture g_native_gpu_textures[NATIVE_GPU_TEXTURE_LIMIT];
static NativeGpuBlendState
    g_native_gpu_blend_states[NATIVE_GPU_BLEND_CACHE_LIMIT];
static uint64_t g_native_gpu_use_serial;

typedef struct {
    float x;
    float y;
    float u;
    float v;
} NativePresenterVertex;

typedef struct {
    float x;
    float y;
    float u;
    float v;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} NativeGpuQuadVertex;

static const char g_native_presenter_hlsl[] =
    "struct VSInput { float2 position : POSITION; float2 uv : TEXCOORD0; };\n"
    "struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VSOutput vs_main(VSInput input) {\n"
    "    VSOutput output;\n"
    "    output.position = float4(input.position, 0.0, 1.0);\n"
    "    output.uv = input.uv;\n"
    "    return output;\n"
    "}\n"
    "Texture2D frame_texture : register(t0);\n"
    "SamplerState frame_sampler : register(s0);\n"
    "float4 ps_main(VSOutput input) : SV_TARGET {\n"
    "    return frame_texture.Sample(frame_sampler, input.uv);\n"
    "}\n"
    "struct QuadInput {\n"
    "    float2 position : POSITION;\n"
    "    float2 uv : TEXCOORD0;\n"
    "    float4 color : COLOR0;\n"
    "};\n"
    "struct QuadOutput {\n"
    "    float4 position : SV_POSITION;\n"
    "    float2 uv : TEXCOORD0;\n"
    "    float4 color : COLOR0;\n"
    "};\n"
    "QuadOutput quad_vs_main(QuadInput input) {\n"
    "    QuadOutput output;\n"
    "    output.position = float4(input.position, 0.0, 1.0);\n"
    "    output.uv = input.uv;\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n"
    "Texture2D quad_texture : register(t0);\n"
    "SamplerState quad_sampler : register(s0);\n"
    "float4 quad_ps_main(QuadOutput input) : SV_TARGET {\n"
    "    float coverage = quad_texture.Sample(quad_sampler, input.uv).a;\n"
    "    return float4(input.color.rgb, coverage * input.color.a);\n"
    "}\n"
    "float4 surface_ps_main(QuadOutput input) : SV_TARGET {\n"
    "    return quad_texture.Sample(quad_sampler, input.uv) * input.color;\n"
    "}\n"
    "cbuffer Material : register(b0) { float4 material_bias; float4 material_scale; float4 material_mode; };\n"
    "float4 material_ps_main(QuadOutput input) : SV_TARGET {\n"
    "    float4 sample_value = quad_texture.Sample(quad_sampler, input.uv);\n"
    "    if (material_mode.x > 0.5) {\n"
    "        float coverage = sample_value.a >= 0.54 ? sample_value.a - 0.5 : 0.0;\n"
    "        return float4(input.color.rgb, coverage * input.color.a);\n"
    "    }\n"
    "    return (sample_value * material_scale + material_bias) * input.color;\n"
    "}\n"
    "struct PostVertex { float4 position : POSITION; float4 p0 : TEXCOORD0; float4 p1 : TEXCOORD1; float4 p2 : TEXCOORD2; float4 p3 : TEXCOORD3; float4 p4 : TEXCOORD4; float4 p5 : TEXCOORD5; float4 p6 : TEXCOORD6; float4 p7 : TEXCOORD7; float4 p8 : TEXCOORD8; float4 p9 : TEXCOORD9; float4 p10 : TEXCOORD10; float4 p11 : TEXCOORD11; float4 p12 : TEXCOORD12; float4 p13 : TEXCOORD13; float4 p14 : TEXCOORD14; float4 p15 : TEXCOORD15; };\n"
    "struct PostOutput { float4 position : SV_POSITION; float4 p0 : TEXCOORD0; float4 p1 : TEXCOORD1; float4 p2 : TEXCOORD2; float4 p3 : TEXCOORD3; float4 p4 : TEXCOORD4; float4 p5 : TEXCOORD5; float4 p6 : TEXCOORD6; float4 p7 : TEXCOORD7; float4 p8 : TEXCOORD8; float4 p9 : TEXCOORD9; float4 p10 : TEXCOORD10; float4 p11 : TEXCOORD11; float4 p12 : TEXCOORD12; float4 p13 : TEXCOORD13; float4 p14 : TEXCOORD14; float4 p15 : TEXCOORD15; };\n"
    "PostOutput post_vs_main(PostVertex v) { PostOutput o; o.position=v.position; o.p0=v.p0; o.p1=v.p1; o.p2=v.p2; o.p3=v.p3; o.p4=v.p4; o.p5=v.p5; o.p6=v.p6; o.p7=v.p7; o.p8=v.p8; o.p9=v.p9; o.p10=v.p10; o.p11=v.p11; o.p12=v.p12; o.p13=v.p13; o.p14=v.p14; o.p15=v.p15; return o; }\n"
    "Texture2D effect_texture : register(t1);\n"
    "Texture2D effect_mask : register(t2);\n"
    "SamplerState effect_sampler : register(s1);\n"
    "SamplerState mask_sampler : register(s2);\n"
    "cbuffer PostConstants : register(b0) { uint4 post_select; uint post_mode; uint3 post_padding; };\n"
    "float post_channel(float4 v, uint s) { return s<4 ? v[s] : (s==5 ? 1.0 : 0.0); }\n"
    "float4 post_sample(float2 uv) { float4 v=quad_texture.Sample(quad_sampler,uv);\n"
    " return float4(post_channel(v,post_select.x),post_channel(v,post_select.y),post_channel(v,post_select.z),post_channel(v,post_select.w)); }\n"
    "float4 post_ps_main(PostOutput v) : SV_TARGET {\n"
    " if(post_mode>=7) { float4 t=quad_texture.Sample(quad_sampler,v.p3.xy)*effect_texture.Sample(effect_sampler,v.p3.zw);\n"
    "  float4 m=effect_mask.Sample(mask_sampler,v.p4.xy);\n"
    "  float3 color=post_mode==7 ? t.rgb*t.rgb*m.rgb : t.rgb*t.rgb+m.rgb;\n"
    "  float a=post_mode==7 ? t.a*m.a : t.a;\n"
    "  return float4((v.p0.rgb-v.p1.rgb)*color+v.p1.rgb,v.p2.x*saturate(v.p1.a*saturate(4*(a-v.p0.a)))); }\n"
    " if(post_mode>=4) { float4 t=quad_texture.Sample(quad_sampler,v.p3.xy);\n"
    "  if(post_mode==4) t*=effect_texture.Sample(effect_sampler,v.p3.zw);\n"
    "  if(post_mode==5) t.rgb*=t.rgb;\n"
    "  float alpha=v.p0.a*t.a; if(post_mode!=4) alpha*=v.p1.a;\n"
    "  return float4((v.p0.rgb-v.p1.rgb)*t.rgb+v.p1.rgb,v.p2.x*saturate(alpha)); }\n"
    " if(post_mode==0) return post_sample(v.p0.xy);\n"
    " if(post_mode==1) return (post_sample(float2(v.p0.x,v.p1.y))+post_sample(float2(v.p0.z,v.p1.y))\n"
    "  +post_sample(float2(v.p0.x,v.p1.w))+post_sample(float2(v.p0.z,v.p1.w)))*0.25;\n"
    " if(post_mode==2) return post_sample(v.p0.yx)*0.352941185+post_sample(v.p0.wx)*0.294117659+post_sample(v.p0.zx)*0.352941185;\n"
    " return post_sample(v.p0.xy)*0.352941185+post_sample(v.p0.xw)*0.294117659+post_sample(v.p0.xz)*0.352941185;\n"
    "}\n";

enum {
    HOST_VPAD_A = 0x00008000u,
    HOST_VPAD_B = 0x00004000u,
    HOST_VPAD_X = 0x00002000u,
    HOST_VPAD_Y = 0x00001000u,
    HOST_VPAD_LEFT = 0x00000800u,
    HOST_VPAD_RIGHT = 0x00000400u,
    HOST_VPAD_UP = 0x00000200u,
    HOST_VPAD_DOWN = 0x00000100u,
    HOST_VPAD_ZL = 0x00000080u,
    HOST_VPAD_ZR = 0x00000040u,
    HOST_VPAD_L = 0x00000020u,
    HOST_VPAD_R = 0x00000010u,
    HOST_VPAD_PLUS = 0x00000008u,
    HOST_VPAD_MINUS = 0x00000004u,
    HOST_VPAD_STICK_R = 0x00020000u,
    HOST_VPAD_STICK_L = 0x00040000u,
};

static uint32_t key_button(WPARAM key) {
    switch (key) {
    case VK_RETURN:
    case VK_SPACE:
        return HOST_VPAD_A;
    case 'Z':
    case VK_BACK:
        return HOST_VPAD_B;
    case 'X':
        return HOST_VPAD_X;
    case 'C':
        return HOST_VPAD_Y;
    case VK_LEFT:
        return HOST_VPAD_LEFT;
    case VK_RIGHT:
        return HOST_VPAD_RIGHT;
    case VK_UP:
        return HOST_VPAD_UP;
    case VK_DOWN:
        return HOST_VPAD_DOWN;
    case 'Q':
        return HOST_VPAD_L;
    case 'E':
        return HOST_VPAD_R;
    case VK_LSHIFT:
    case VK_RSHIFT:
        return HOST_VPAD_ZL;
    case VK_LCONTROL:
    case VK_RCONTROL:
        return HOST_VPAD_ZR;
    case 'P':
        return HOST_VPAD_PLUS;
    case 'O':
        return HOST_VPAD_MINUS;
    default:
        return 0u;
    }
}

static void update_stick(void) {
    if (!g_window || g_window_hidden) {
        g_left_stick_x = g_left_stick_y = 0.0f;
        return;
    }
    bool keyboard_active = GetForegroundWindow() == g_window;
    float keyboard_x =
        (keyboard_active && (GetAsyncKeyState('D') & 0x8000) ? 1.0f : 0.0f) -
        (keyboard_active && (GetAsyncKeyState('A') & 0x8000) ? 1.0f : 0.0f);
    float keyboard_y =
        (keyboard_active && (GetAsyncKeyState('W') & 0x8000) ? 1.0f : 0.0f) -
        (keyboard_active && (GetAsyncKeyState('S') & 0x8000) ? 1.0f : 0.0f);
    g_left_stick_x = keyboard_x != 0.0f ? keyboard_x : g_controller_stick_x;
    g_left_stick_y = keyboard_y != 0.0f ? keyboard_y : g_controller_stick_y;
}

static void refresh_input_held(void) {
    uint32_t held = g_keyboard_held | g_controller_held;
    uint32_t pressed = held & ~g_input_held;
    uint32_t released = g_input_held & ~held;
    if (pressed != 0u || released != 0u) {
        g_input_pressed |= pressed;
        g_input_released |= released;
        g_input_edges_observed = false;
    }
    g_input_held = held;
}

static float normalize_xinput_axis(SHORT value, SHORT dead_zone) {
    if (value > -dead_zone && value < dead_zone)
        return 0.0f;
    if (value > 0)
        return (float)(value - dead_zone) / (float)(32767 - dead_zone);
    return (float)(value + dead_zone) / (float)(32768 - dead_zone);
}

static uint32_t map_xinput_buttons(const XINPUT_GAMEPAD* gamepad) {
    if (!gamepad)
        return 0u;

    uint32_t held = 0u;
    WORD buttons = gamepad->wButtons;
    if (buttons & XINPUT_GAMEPAD_A)
        held |= HOST_VPAD_A;
    if (buttons & XINPUT_GAMEPAD_B)
        held |= HOST_VPAD_B;
    if (buttons & XINPUT_GAMEPAD_X)
        held |= HOST_VPAD_X;
    if (buttons & XINPUT_GAMEPAD_Y)
        held |= HOST_VPAD_Y;
    if (buttons & XINPUT_GAMEPAD_DPAD_LEFT)
        held |= HOST_VPAD_LEFT;
    if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT)
        held |= HOST_VPAD_RIGHT;
    if (buttons & XINPUT_GAMEPAD_DPAD_UP)
        held |= HOST_VPAD_UP;
    if (buttons & XINPUT_GAMEPAD_DPAD_DOWN)
        held |= HOST_VPAD_DOWN;
    if (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER)
        held |= HOST_VPAD_L;
    if (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER)
        held |= HOST_VPAD_R;
    if (buttons & XINPUT_GAMEPAD_START)
        held |= HOST_VPAD_PLUS;
    if (buttons & XINPUT_GAMEPAD_BACK)
        held |= HOST_VPAD_MINUS;
    if (buttons & XINPUT_GAMEPAD_LEFT_THUMB)
        held |= HOST_VPAD_STICK_L;
    if (buttons & XINPUT_GAMEPAD_RIGHT_THUMB)
        held |= HOST_VPAD_STICK_R;
    if (gamepad->bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        held |= HOST_VPAD_ZL;
    if (gamepad->bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        held |= HOST_VPAD_ZR;
    return held;
}

static void initialize_xinput(void) {
    if (g_xinput_initialized)
        return;
    g_xinput_initialized = true;

    static const wchar_t* const libraries[] = {
        L"xinput1_4.dll",
        L"xinput1_3.dll",
        L"xinput9_1_0.dll",
    };
    for (uint32_t i = 0u; i < sizeof(libraries) / sizeof(libraries[0]); i++) {
        HMODULE module = LoadLibraryW(libraries[i]);
        if (!module)
            continue;
        XInputGetStateProc get_state =
            (XInputGetStateProc)GetProcAddress(module, "XInputGetState");
        if (get_state) {
            g_xinput_module = module;
            g_xinput_get_state = get_state;
            return;
        }
        FreeLibrary(module);
    }
}

static void poll_xinput(void) {
    /* Hidden diagnostics use explicit guest-side test input only. Never
       consume the user's controller while another application is in use. */
    if (!g_window || g_window_hidden) return;
    initialize_xinput();

    /* The guest pumps thousands of times between frames during boot. Device
       enumeration must not run at that frequency, especially for empty
       controller slots. Keep keyboard/connected-pad updates at 250 Hz and
       retry disconnected pads four times per second. */
    static ULONGLONG next_input_poll;
    static ULONGLONG next_disconnected_probe;
    ULONGLONG now = GetTickCount64();
    if (now < next_input_poll)
        return;
    next_input_poll = now + 4u;
    if (!g_xinput_connected && now < next_disconnected_probe) {
        refresh_input_held();
        update_stick();
        return;
    }
    next_disconnected_probe = now + 250u;

    bool connected = false;
    uint32_t held = 0u;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    DWORD controller_index = 0u;
    if (g_xinput_get_state) {
        for (DWORD index = 0u; index < XUSER_MAX_COUNT; index++) {
            XINPUT_STATE state;
            memset(&state, 0, sizeof(state));
            if (g_xinput_get_state(index, &state) != ERROR_SUCCESS)
                continue;
            connected = true;
            controller_index = index;
            held = map_xinput_buttons(&state.Gamepad);
            stick_x = normalize_xinput_axis(state.Gamepad.sThumbLX,
                                            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            stick_y = normalize_xinput_axis(state.Gamepad.sThumbLY,
                                            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            break;
        }
    }

    if (connected != g_xinput_connected) {
        if (connected) {
            fprintf(stderr, "input: XInput controller connected (slot %lu)\n",
                    (unsigned long)controller_index);
        } else {
            fprintf(stderr, "input: XInput controller disconnected\n");
        }
    }
    g_xinput_connected = connected;
    g_xinput_controller_index = controller_index;
    g_controller_held = held;
    g_controller_stick_x = stick_x;
    g_controller_stick_y = stick_y;
    refresh_input_held();
    update_stick();
}

static bool frame_rect_for_client(const RECT* client, RECT* frame) {
    if (!client || !frame || !g_has_frame || g_frame_width == 0u ||
        g_frame_height == 0u) {
        return false;
    }

    LONG client_width = client->right - client->left;
    LONG client_height = client->bottom - client->top;
    if (client_width <= 0 || client_height <= 0)
        return false;

    LONG frame_width = client_width;
    LONG frame_height = (LONG)(((int64_t)client_width * g_frame_height) /
                               g_frame_width);
    if (frame_height > client_height) {
        frame_height = client_height;
        frame_width = (LONG)(((int64_t)client_height * g_frame_width) /
                             g_frame_height);
    }
    frame->left = (client_width - frame_width) / 2;
    frame->top = (client_height - frame_height) / 2;
    frame->right = frame->left + frame_width;
    frame->bottom = frame->top + frame_height;
    return frame_width > 0 && frame_height > 0;
}

static bool update_touch_position(HWND hwnd, LPARAM lparam) {
    RECT client;
    RECT frame;
    if (!GetClientRect(hwnd, &client) || !frame_rect_for_client(&client, &frame))
        return false;

    LONG x = (LONG)(short)LOWORD(lparam);
    LONG y = (LONG)(short)HIWORD(lparam);
    if (x < frame.left || x >= frame.right || y < frame.top ||
        y >= frame.bottom) {
        return false;
    }

    uint32_t frame_width = (uint32_t)(frame.right - frame.left);
    uint32_t frame_height = (uint32_t)(frame.bottom - frame.top);
    uint32_t local_x = (uint32_t)(x - frame.left);
    uint32_t local_y = (uint32_t)(y - frame.top);
    g_touch_x = (uint16_t)(92u +
                           ((uint64_t)local_x * 3883u) / frame_width);
    g_touch_y = (uint16_t)(4095u - 254u -
                           ((uint64_t)local_y * 3694u) / frame_height);
    return true;
}

static bool frame_is_nearly_uniform(const uint8_t* pixels, uint32_t width,
                                    uint32_t height, uint32_t stride) {
    uint8_t min_b = 255u;
    uint8_t min_g = 255u;
    uint8_t min_r = 255u;
    uint8_t max_b = 0u;
    uint8_t max_g = 0u;
    uint8_t max_r = 0u;

    for (uint32_t sample_y = 0; sample_y < 18u; sample_y++) {
        uint32_t y = (uint32_t)(((uint64_t)sample_y * (height - 1u)) / 17u);
        const uint8_t* row = pixels + (size_t)y * stride;
        for (uint32_t sample_x = 0; sample_x < 32u; sample_x++) {
            uint32_t x =
                (uint32_t)(((uint64_t)sample_x * (width - 1u)) / 31u);
            const uint8_t* pixel = row + (size_t)x * 4u;
            if (pixel[0] < min_b)
                min_b = pixel[0];
            if (pixel[1] < min_g)
                min_g = pixel[1];
            if (pixel[2] < min_r)
                min_r = pixel[2];
            if (pixel[0] > max_b)
                max_b = pixel[0];
            if (pixel[1] > max_g)
                max_g = pixel[1];
            if (pixel[2] > max_r)
                max_r = pixel[2];
        }
    }

    return (uint32_t)(max_b - min_b) <= 3u &&
           (uint32_t)(max_g - min_g) <= 3u &&
           (uint32_t)(max_r - min_r) <= 3u;
}

static bool accept_uniform_frame(void) {
    if (!g_has_frame || !g_last_accepted_frame_detailed) {
        g_uniform_frame_streak = 0u;
        return true;
    }

    g_uniform_frame_streak++;
    g_ignored_uniform_frames++;
    if (g_ignored_uniform_frames <= 16u ||
        (g_ignored_uniform_frames % 240u) == 0u) {
        fprintf(stderr,
                "window: held detailed frame across empty swap "
                "(streak=%u, total=%u)\n",
                g_uniform_frame_streak, g_ignored_uniform_frames);
    }
    return false;
}

static void accept_detailed_frame(void) {
    g_uniform_frame_streak = 0u;
    g_last_accepted_frame_detailed = true;
}

#define NATIVE_RELEASE(object)                                                  \
    do {                                                                        \
        if ((object) != NULL) {                                                 \
            IUnknown_Release((IUnknown*)(object));                             \
            (object) = NULL;                                                   \
        }                                                                       \
    } while (0)

static void native_presenter_release_frame_texture(void) {
    NATIVE_RELEASE(g_d3d_frame_view);
    NATIVE_RELEASE(g_d3d_frame_texture);
    g_d3d_frame_width = 0u;
    g_d3d_frame_height = 0u;
}

static void native_presenter_release_backbuffer(void) {
    NATIVE_RELEASE(g_d3d_render_target);
    g_d3d_backbuffer_width = 0u;
    g_d3d_backbuffer_height = 0u;
}

static void native_gpu_release_surface(NativeGpuSurface* surface) {
    if (!surface)
        return;
    NATIVE_RELEASE(surface->readback_texture);
    NATIVE_RELEASE(surface->view);
    NATIVE_RELEASE(surface->render_target);
    NATIVE_RELEASE(surface->texture);
    memset(surface, 0, sizeof(*surface));
}

static void native_gpu_release_texture(NativeGpuTexture* texture) {
    if (!texture)
        return;
    NATIVE_RELEASE(texture->view);
    NATIVE_RELEASE(texture->texture);
    memset(texture, 0, sizeof(*texture));
}

static void native_gpu_release_all(void) {
    for(unsigned i=0;i<64;++i) { NATIVE_RELEASE(g_effect_samplers[i].state); }
    for (uint32_t i = 0u; i < NATIVE_GPU_SURFACE_LIMIT; i++)
        native_gpu_release_surface(&g_native_gpu_surfaces[i]);
    for (uint32_t i = 0u; i < NATIVE_GPU_TEXTURE_LIMIT; i++)
        native_gpu_release_texture(&g_native_gpu_textures[i]);
    for (uint32_t i = 0u; i < NATIVE_GPU_BLEND_CACHE_LIMIT; i++) {
        NATIVE_RELEASE(g_native_gpu_blend_states[i].state);
        memset(&g_native_gpu_blend_states[i], 0,
               sizeof(g_native_gpu_blend_states[i]));
    }
    g_native_gpu_use_serial = 0u;
}

static NativeGpuSurface* native_gpu_find_surface(uint32_t key) {
    if (key == 0u)
        return NULL;
    for (uint32_t i = 0u; i < NATIVE_GPU_SURFACE_LIMIT; i++) {
        NativeGpuSurface* surface = &g_native_gpu_surfaces[i];
        if (surface->valid && surface->key == key)
            return surface;
    }
    return NULL;
}

static NativeGpuSurface* native_gpu_ensure_surface_format(uint32_t key,
                                                    uint32_t width,
                                                    uint32_t height,DXGI_FORMAT format) {
    if (!g_d3d_ready || key == 0u || width == 0u || height == 0u)
        return NULL;

    NativeGpuSurface* existing = native_gpu_find_surface(key);
    if(format==DXGI_FORMAT_UNKNOWN)format=existing?existing->format:DXGI_FORMAT_B8G8R8A8_UNORM;
    if (existing && existing->width == width && existing->height == height && existing->format==format) {
        existing->use_serial = ++g_native_gpu_use_serial;
        return existing;
    }

    NativeGpuSurface* replacement = existing;
    if (!replacement) {
        for (uint32_t i = 0u; i < NATIVE_GPU_SURFACE_LIMIT; i++) {
            NativeGpuSurface* candidate = &g_native_gpu_surfaces[i];
            if (!candidate->valid) {
                replacement = candidate;
                break;
            }
            if (!replacement || candidate->use_serial < replacement->use_serial)
                replacement = candidate;
        }
    }
    if (!replacement)
        return NULL;

    native_gpu_release_surface(replacement);
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = format;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT result = ID3D11Device_CreateTexture2D(g_d3d_device, &desc, NULL,
                                                    &replacement->texture);
    if (FAILED(result))
        goto failed;
    result = ID3D11Device_CreateRenderTargetView(
        g_d3d_device, (ID3D11Resource*)replacement->texture, NULL,
        &replacement->render_target);
    if (FAILED(result))
        goto failed;
    result = ID3D11Device_CreateShaderResourceView(
        g_d3d_device, (ID3D11Resource*)replacement->texture, NULL,
        &replacement->view);
    if (FAILED(result))
        goto failed;
    replacement->valid = true;
    replacement->key = key;
    replacement->width = width;
    replacement->height = height;
    replacement->format = format;
    replacement->use_serial = ++g_native_gpu_use_serial;
    return replacement;

failed:
    native_gpu_release_surface(replacement);
    return NULL;
}
static NativeGpuSurface* native_gpu_ensure_surface(uint32_t key,uint32_t width,uint32_t height) {
    return native_gpu_ensure_surface_format(key,width,height,DXGI_FORMAT_UNKNOWN);
}
bool wiiu_window_gpu_configure(uint32_t key,uint32_t width,uint32_t height,uint32_t format) {
    DXGI_FORMAT native;
    switch(format) {
    case 0x806:native=DXGI_FORMAT_R16_FLOAT;break;
    case 0x810:native=DXGI_FORMAT_R16G16_FLOAT;break;
    case 0x820:native=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
    case 0x80e:native=DXGI_FORMAT_R32_FLOAT;break;
    case 0x81e:native=DXGI_FORMAT_R32G32_FLOAT;break;
    case 0x823:native=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
    case 0x816:native=DXGI_FORMAT_R11G11B10_FLOAT;break;
    default:native=DXGI_FORMAT_B8G8R8A8_UNORM;break;
    }
    return native_gpu_ensure_surface_format(key,width,height,native)!=NULL;
}
static unsigned native_float_layout(DXGI_FORMAT format,unsigned* bytes) {
    *bytes=2;
    switch(format) {
    case DXGI_FORMAT_R16_FLOAT:return 1;
    case DXGI_FORMAT_R16G16_FLOAT:return 2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return 4;
    case DXGI_FORMAT_R32_FLOAT:*bytes=4;return 1;
    case DXGI_FORMAT_R32G32_FLOAT:*bytes=4;return 2;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:*bytes=4;return 4;
    case DXGI_FORMAT_R11G11B10_FLOAT:*bytes=0;return 3;
    default:*bytes=1;return 0;
    }
}
static float native_half_to_float(uint16_t h) {
    unsigned e=(h>>10)&31,m=h&1023;
    float f=e==31?(m?NAN:INFINITY):e?ldexpf(1+m/1024.0f,(int)e-15):ldexpf((float)m,-24);
    return h&0x8000?-f:f;
}
/* BGRA uploads contain only [0,1]; convert exactly with nearest-even rounding. */
static uint16_t native_unorm8_to_half(uint8_t value) {
    float f=value/255.0f;uint32_t bits;memcpy(&bits,&f,4);
    if(!value)return 0;
    uint32_t mantissa=(bits&0x7fffff)+0xfff+((bits>>13)&1);
    return (uint16_t)((((bits>>23)-112)<<10)+(mantissa>>13));
}

static NativeGpuTexture* native_gpu_ensure_texture(
    const uint8_t* source, uint32_t width, uint32_t height, uint64_t serial) {
    if (!g_d3d_ready || !source || width == 0u || height == 0u)
        return NULL;

    for (uint32_t i = 0u; i < NATIVE_GPU_TEXTURE_LIMIT; i++) {
        NativeGpuTexture* texture = &g_native_gpu_textures[i];
        if (texture->valid && texture->source == source &&
            texture->serial == serial && texture->width == width &&
            texture->height == height) {
            texture->use_serial = ++g_native_gpu_use_serial;
            return texture;
        }
    }

    NativeGpuTexture* replacement = NULL;
    for (uint32_t i = 0u; i < NATIVE_GPU_TEXTURE_LIMIT; i++) {
        NativeGpuTexture* candidate = &g_native_gpu_textures[i];
        if (!candidate->valid) {
            replacement = candidate;
            break;
        }
        if (!replacement || candidate->use_serial < replacement->use_serial)
            replacement = candidate;
    }
    if (!replacement)
        return NULL;

    native_gpu_release_texture(replacement);
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data;
    memset(&data, 0, sizeof(data));
    data.pSysMem = source;
    data.SysMemPitch = width * 4u;
    HRESULT result = ID3D11Device_CreateTexture2D(g_d3d_device, &desc, &data,
                                                    &replacement->texture);
    if (FAILED(result))
        goto failed;
    result = ID3D11Device_CreateShaderResourceView(
        g_d3d_device, (ID3D11Resource*)replacement->texture, NULL,
        &replacement->view);
    if (FAILED(result))
        goto failed;
    replacement->valid = true;
    replacement->source = source;
    replacement->serial = serial;
    replacement->width = width;
    replacement->height = height;
    replacement->use_serial = ++g_native_gpu_use_serial;
    return replacement;

failed:
    native_gpu_release_texture(replacement);
    return NULL;
}

static bool native_gpu_blend_controls_match(
    const WiiUWindowGpuBlendControl* left,
    const WiiUWindowGpuBlendControl* right) {
    return left && right &&
           left->color_source_factor == right->color_source_factor &&
           left->color_destination_factor == right->color_destination_factor &&
           left->color_operation == right->color_operation &&
           left->separate_alpha == right->separate_alpha &&
           left->alpha_source_factor == right->alpha_source_factor &&
           left->alpha_destination_factor == right->alpha_destination_factor &&
           left->alpha_operation == right->alpha_operation &&
           left->color_control_valid == right->color_control_valid &&
           left->blend_enabled == right->blend_enabled &&
           left->color_enabled == right->color_enabled &&
           left->write_mask_valid == right->write_mask_valid &&
           left->write_mask == right->write_mask;
}

static D3D11_BLEND native_gpu_blend_factor(uint32_t factor) {
    switch (factor) {
    case 0u:
        return D3D11_BLEND_ZERO;
    case 1u:
        return D3D11_BLEND_ONE;
    case 2u:
        return D3D11_BLEND_SRC_COLOR;
    case 3u:
        return D3D11_BLEND_INV_SRC_COLOR;
    case 4u:
        return D3D11_BLEND_SRC_ALPHA;
    case 5u:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case 6u:
        return D3D11_BLEND_DEST_ALPHA;
    case 7u:
        return D3D11_BLEND_INV_DEST_ALPHA;
    case 8u:
        return D3D11_BLEND_DEST_COLOR;
    case 9u:
        return D3D11_BLEND_INV_DEST_COLOR;
    case 10u:
        return D3D11_BLEND_SRC_ALPHA_SAT;
    case 11u:
        return D3D11_BLEND_SRC_ALPHA;
    case 12u:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case 13u:
        return D3D11_BLEND_BLEND_FACTOR;
    case 14u:
        return D3D11_BLEND_INV_BLEND_FACTOR;
    case 15u:
        return D3D11_BLEND_SRC1_COLOR;
    case 16u:
        return D3D11_BLEND_INV_SRC1_COLOR;
    case 17u:
        return D3D11_BLEND_SRC1_ALPHA;
    case 18u:
        return D3D11_BLEND_INV_SRC1_ALPHA;
    case 19u:
        return D3D11_BLEND_BLEND_FACTOR;
    case 20u:
        return D3D11_BLEND_INV_BLEND_FACTOR;
    default:
        return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND_OP native_gpu_blend_operation(uint32_t operation) {
    switch (operation) {
    case 1u:
        return D3D11_BLEND_OP_SUBTRACT;
    case 2u:
        return D3D11_BLEND_OP_MIN;
    case 3u:
        return D3D11_BLEND_OP_MAX;
    case 4u:
        return D3D11_BLEND_OP_REV_SUBTRACT;
    default:
        return D3D11_BLEND_OP_ADD;
    }
}

static ID3D11BlendState* native_gpu_get_blend_state(
    const WiiUWindowGpuBlendControl* control) {
    if (!control || !control->valid)
        return g_d3d_quad_blend_state;
    NativeGpuBlendState* replacement = NULL;
    for (uint32_t i = 0u; i < NATIVE_GPU_BLEND_CACHE_LIMIT; i++) {
        NativeGpuBlendState* entry = &g_native_gpu_blend_states[i];
        if (entry->valid && native_gpu_blend_controls_match(&entry->control,
                                                            control)) {
            return entry->state;
        }
        if (!entry->valid && !replacement)
            replacement = entry;
    }
    if (!replacement)
        replacement = &g_native_gpu_blend_states[0];
    NATIVE_RELEASE(replacement->state);
    memset(replacement, 0, sizeof(*replacement));

    uint32_t alpha_source = control->separate_alpha
                                ? control->alpha_source_factor
                                : control->color_source_factor;
    uint32_t alpha_destination = control->separate_alpha
                                     ? control->alpha_destination_factor
                                     : control->color_destination_factor;
    uint32_t alpha_operation = control->separate_alpha
                                   ? control->alpha_operation
                                   : control->color_operation;
    D3D11_BLEND_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.RenderTarget[0].BlendEnable = !control->color_control_valid || control->blend_enabled;
    desc.RenderTarget[0].SrcBlend =
        native_gpu_blend_factor(control->color_source_factor);
    desc.RenderTarget[0].DestBlend =
        native_gpu_blend_factor(control->color_destination_factor);
    desc.RenderTarget[0].BlendOp =
        native_gpu_blend_operation(control->color_operation);
    desc.RenderTarget[0].SrcBlendAlpha = native_gpu_blend_factor(alpha_source);
    desc.RenderTarget[0].DestBlendAlpha =
        native_gpu_blend_factor(alpha_destination);
    desc.RenderTarget[0].BlendOpAlpha = native_gpu_blend_operation(alpha_operation);
    desc.RenderTarget[0].RenderTargetWriteMask =
        control->color_control_valid && !control->color_enabled ? 0 :
        control->write_mask_valid ? control->write_mask : D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(ID3D11Device_CreateBlendState(g_d3d_device, &desc,
                                              &replacement->state))) {
        return g_d3d_quad_blend_state;
    }
    replacement->valid = true;
    replacement->control = *control;
    return replacement->state;
}

static void native_presenter_shutdown(void) {
    if (g_d3d_context)
        ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0u, NULL, NULL);
    native_gpu_release_all();
    native_presenter_release_frame_texture();
    native_presenter_release_backbuffer();
    NATIVE_RELEASE(g_d3d_post_vs);
    NATIVE_RELEASE(g_d3d_post_ps);
    NATIVE_RELEASE(g_d3d_post_layout);
    NATIVE_RELEASE(g_d3d_post_vertices);
    NATIVE_RELEASE(g_d3d_post_constants);
    NATIVE_RELEASE(g_d3d_quad_rasterizer_state);
    NATIVE_RELEASE(g_d3d_scene_constants);
    for(unsigned i=0;i<16;++i) {
        NATIVE_RELEASE(g_scene_blocks[i].view);NATIVE_RELEASE(g_scene_blocks[i].buffer);
        g_scene_blocks[i].size=0;
    }
    for(unsigned i=0;i<64;++i)NATIVE_RELEASE(g_scene_blends[i].state);
    for(unsigned i=0;i<128;++i) {
        NATIVE_RELEASE(g_scene_shaders[i].shader);
        free(g_scene_shaders[i].code);g_scene_shaders[i].code=NULL;
    }
    NATIVE_RELEASE(g_d3d_quad_blend_state);
    NATIVE_RELEASE(g_d3d_quad_vertex_buffer);
    NATIVE_RELEASE(g_d3d_quad_input_layout);
    NATIVE_RELEASE(g_d3d_surface_pixel_shader);
    NATIVE_RELEASE(g_d3d_material_pixel_shader);
    NATIVE_RELEASE(g_d3d_material_buffer);
    NATIVE_RELEASE(g_d3d_quad_pixel_shader);
    NATIVE_RELEASE(g_d3d_quad_vertex_shader);
    NATIVE_RELEASE(g_d3d_sampler);
    NATIVE_RELEASE(g_d3d_vertex_buffer);
    NATIVE_RELEASE(g_d3d_input_layout);
    NATIVE_RELEASE(g_d3d_pixel_shader);
    NATIVE_RELEASE(g_d3d_vertex_shader);
    NATIVE_RELEASE(g_d3d_swap_chain);
    NATIVE_RELEASE(g_d3d_context);
    NATIVE_RELEASE(g_d3d_device);
    g_d3d_ready = false;
    g_d3d_resize_pending = false;
}

static bool native_presenter_compile_shader(const char* entry,
                                            const char* target,
                                            ID3DBlob** output) {
    if (!entry || !target || !output)
        return false;

    ID3DBlob* errors = NULL;
    HRESULT result = D3DCompile(g_native_presenter_hlsl,
                                sizeof(g_native_presenter_hlsl) - 1u,
                                "sm3dw_native_presenter.hlsl", NULL, NULL,
                                entry, target, D3DCOMPILE_ENABLE_STRICTNESS,
                                0u, output, &errors);
    if (FAILED(result)) {
        if (errors) {
            fprintf(stderr, "window: D3D11 %s compile failed: %.*s\n", entry,
                    (int)ID3D10Blob_GetBufferSize(errors),
                    (const char*)ID3D10Blob_GetBufferPointer(errors));
        } else {
            fprintf(stderr, "window: D3D11 %s compile failed (0x%08lX)\n",
                    entry, (unsigned long)result);
        }
        NATIVE_RELEASE(errors);
        return false;
    }
    NATIVE_RELEASE(errors);
    return true;
}

static bool native_presenter_create_pipeline(void) {
    static const NativePresenterVertex vertices[] = {
        {-1.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
    };
    static const D3D11_INPUT_ELEMENT_DESC input_elements[] = {
        {"POSITION", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 0u,
         D3D11_INPUT_PER_VERTEX_DATA, 0u},
        {"TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 8u,
         D3D11_INPUT_PER_VERTEX_DATA, 0u},
    };
    static const D3D11_INPUT_ELEMENT_DESC quad_input_elements[] = {
        {"POSITION", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 0u,
         D3D11_INPUT_PER_VERTEX_DATA, 0u},
        {"TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 8u,
         D3D11_INPUT_PER_VERTEX_DATA, 0u},
        {"COLOR", 0u, DXGI_FORMAT_R8G8B8A8_UNORM, 0u, 16u,
         D3D11_INPUT_PER_VERTEX_DATA, 0u},
    };

    ID3DBlob* vertex_blob = NULL;
    ID3DBlob* pixel_blob = NULL;
    ID3DBlob* quad_vertex_blob = NULL;
    ID3DBlob* quad_pixel_blob = NULL;
    ID3DBlob* surface_pixel_blob = NULL;
    ID3DBlob* material_pixel_blob = NULL;
    bool success = false;
    if (!native_presenter_compile_shader("vs_main", "vs_4_0", &vertex_blob) ||
        !native_presenter_compile_shader("ps_main", "ps_4_0", &pixel_blob) ||
        !native_presenter_compile_shader("quad_vs_main", "vs_4_0",
                                         &quad_vertex_blob) ||
        !native_presenter_compile_shader("quad_ps_main", "ps_4_0",
                                         &quad_pixel_blob) ||
        !native_presenter_compile_shader("surface_ps_main", "ps_4_0",
                                         &surface_pixel_blob) ||
        !native_presenter_compile_shader("material_ps_main", "ps_4_0",
                                         &material_pixel_blob)) {
        goto done;
    }

    HRESULT result = ID3D11Device_CreateVertexShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(vertex_blob),
        ID3D10Blob_GetBufferSize(vertex_blob), NULL, &g_d3d_vertex_shader);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreatePixelShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(pixel_blob),
        ID3D10Blob_GetBufferSize(pixel_blob), NULL, &g_d3d_pixel_shader);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreateInputLayout(
        g_d3d_device, input_elements,
        (UINT)(sizeof(input_elements) / sizeof(input_elements[0])),
        ID3D10Blob_GetBufferPointer(vertex_blob),
        ID3D10Blob_GetBufferSize(vertex_blob), &g_d3d_input_layout);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreateVertexShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(quad_vertex_blob),
        ID3D10Blob_GetBufferSize(quad_vertex_blob), NULL,
        &g_d3d_quad_vertex_shader);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreatePixelShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(quad_pixel_blob),
        ID3D10Blob_GetBufferSize(quad_pixel_blob), NULL,
        &g_d3d_quad_pixel_shader);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreatePixelShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(surface_pixel_blob),
        ID3D10Blob_GetBufferSize(surface_pixel_blob), NULL,
        &g_d3d_surface_pixel_shader);
    if (FAILED(result))
        goto done;
    result = ID3D11Device_CreateInputLayout(
        g_d3d_device, quad_input_elements,
        (UINT)(sizeof(quad_input_elements) / sizeof(quad_input_elements[0])),
        ID3D10Blob_GetBufferPointer(quad_vertex_blob),
        ID3D10Blob_GetBufferSize(quad_vertex_blob),
        &g_d3d_quad_input_layout);
    if (FAILED(result))
        goto done;

    D3D11_BUFFER_DESC buffer_desc;
    result = ID3D11Device_CreatePixelShader(
        g_d3d_device, ID3D10Blob_GetBufferPointer(material_pixel_blob),
        ID3D10Blob_GetBufferSize(material_pixel_blob), NULL, &g_d3d_material_pixel_shader);
    if (FAILED(result)) goto done;
    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.ByteWidth = sizeof(WiiUWindowGpuMaterial);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    result = ID3D11Device_CreateBuffer(g_d3d_device, &buffer_desc, NULL, &g_d3d_material_buffer);
    if (FAILED(result)) goto done;
    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.ByteWidth = (UINT)sizeof(vertices);
    buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial_data;
    memset(&initial_data, 0, sizeof(initial_data));
    initial_data.pSysMem = vertices;
    result = ID3D11Device_CreateBuffer(g_d3d_device, &buffer_desc,
                                       &initial_data, &g_d3d_vertex_buffer);
    if (FAILED(result))
        goto done;

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.ByteWidth =
        (UINT)(sizeof(NativeGpuQuadVertex) * 6u * NATIVE_GPU_QUAD_BATCH_LIMIT);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = ID3D11Device_CreateBuffer(g_d3d_device, &buffer_desc, NULL,
                                       &g_d3d_quad_vertex_buffer);
    if (FAILED(result))
        goto done;

    D3D11_SAMPLER_DESC sampler_desc;
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    result = ID3D11Device_CreateSamplerState(g_d3d_device, &sampler_desc,
                                              &g_d3d_sampler);
    if (FAILED(result))
        goto done;

    D3D11_BLEND_DESC blend_desc;
    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    result = ID3D11Device_CreateBlendState(g_d3d_device, &blend_desc,
                                             &g_d3d_quad_blend_state);
    if (FAILED(result))
        goto done;

    D3D11_RASTERIZER_DESC rasterizer_desc;
    memset(&rasterizer_desc, 0, sizeof(rasterizer_desc));
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.ScissorEnable = TRUE;
    result = ID3D11Device_CreateRasterizerState(
        g_d3d_device, &rasterizer_desc, &g_d3d_quad_rasterizer_state);
    if (FAILED(result))
        goto done;
    success = true;

done:
    NATIVE_RELEASE(vertex_blob);
    NATIVE_RELEASE(pixel_blob);
    NATIVE_RELEASE(quad_vertex_blob);
    NATIVE_RELEASE(quad_pixel_blob);
    NATIVE_RELEASE(surface_pixel_blob);
    NATIVE_RELEASE(material_pixel_blob);
    if (!success)
        fprintf(stderr, "window: D3D11 presenter pipeline setup failed\n");
    return success;
}

static bool native_presenter_create_backbuffer(void) {
    ID3D11Texture2D* backbuffer = NULL;
    HRESULT result = IDXGISwapChain_GetBuffer(
        g_d3d_swap_chain, 0u, &IID_ID3D11Texture2D, (void**)&backbuffer);
    if (FAILED(result) || !backbuffer)
        return false;

    result = ID3D11Device_CreateRenderTargetView(g_d3d_device,
                                                   (ID3D11Resource*)backbuffer,
                                                   NULL, &g_d3d_render_target);
    if (SUCCEEDED(result)) {
        D3D11_TEXTURE2D_DESC desc;
        ID3D11Texture2D_GetDesc(backbuffer, &desc);
        g_d3d_backbuffer_width = desc.Width;
        g_d3d_backbuffer_height = desc.Height;
    }
    NATIVE_RELEASE(backbuffer);
    return SUCCEEDED(result);
}

static bool native_presenter_resize(void) {
    if (!g_d3d_ready || !g_d3d_swap_chain)
        return false;

    RECT client;
    if (!g_window || !GetClientRect(g_window, &client))
        return false;
    uint32_t width = (uint32_t)(client.right - client.left);
    uint32_t height = (uint32_t)(client.bottom - client.top);
    if (width == 0u || height == 0u)
        return true;
    if (!g_d3d_resize_pending && g_d3d_render_target &&
        g_d3d_backbuffer_width == width && g_d3d_backbuffer_height == height) {
        return true;
    }

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0u, NULL, NULL);
    native_presenter_release_backbuffer();
    HRESULT result = IDXGISwapChain_ResizeBuffers(
        g_d3d_swap_chain, 0u, width, height, DXGI_FORMAT_UNKNOWN, 0u);
    if (FAILED(result)) {
        fprintf(stderr, "window: D3D11 resize failed (0x%08lX)\n",
                (unsigned long)result);
        return false;
    }
    g_d3d_resize_pending = false;
    if (!native_presenter_create_backbuffer()) {
        fprintf(stderr, "window: D3D11 render-target setup failed\n");
        return false;
    }
    return true;
}

static bool native_presenter_ensure_frame_texture(uint32_t width,
                                                   uint32_t height) {
    if (!g_d3d_ready || width == 0u || height == 0u)
        return false;
    if (g_d3d_frame_texture && g_d3d_frame_width == width &&
        g_d3d_frame_height == height) {
        return true;
    }

    native_presenter_release_frame_texture();
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT result = ID3D11Device_CreateTexture2D(g_d3d_device, &desc, NULL,
                                                    &g_d3d_frame_texture);
    if (FAILED(result))
        return false;
    result = ID3D11Device_CreateShaderResourceView(
        g_d3d_device, (ID3D11Resource*)g_d3d_frame_texture, NULL,
        &g_d3d_frame_view);
    if (FAILED(result)) {
        native_presenter_release_frame_texture();
        return false;
    }
    g_d3d_frame_width = width;
    g_d3d_frame_height = height;
    return true;
}

static bool native_presenter_present(ID3D11ShaderResourceView* frame_view,
                                     float red, float green, float blue) {
    bool textured = frame_view != NULL;
    if (!g_d3d_ready || !native_presenter_resize())
        return false;
    if (!g_d3d_render_target || g_d3d_backbuffer_width == 0u ||
        g_d3d_backbuffer_height == 0u) {
        return true;
    }

    RECT client;
    RECT frame;
    if (!GetClientRect(g_window, &client))
        return false;
    if (textured && !frame_rect_for_client(&client, &frame))
        return false;

    float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!textured) {
        clear_color[0] = red;
        clear_color[1] = green;
        clear_color[2] = blue;
    }
    /* Layout draws leave a scissor state behind; the scan buffer must not. */
    ID3D11DeviceContext_RSSetState(g_d3d_context, NULL);
    ID3D11DeviceContext_OMSetBlendState(g_d3d_context, NULL, NULL,
                                         0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 1u,
                                            &g_d3d_render_target, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(g_d3d_context,
                                               g_d3d_render_target,
                                               clear_color);

    if (textured) {
        D3D11_VIEWPORT viewport;
        memset(&viewport, 0, sizeof(viewport));
        viewport.TopLeftX = (float)frame.left;
        viewport.TopLeftY = (float)frame.top;
        viewport.Width = (float)(frame.right - frame.left);
        viewport.Height = (float)(frame.bottom - frame.top);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        UINT stride = (UINT)sizeof(NativePresenterVertex);
        UINT offset = 0u;
        ID3D11ShaderResourceView* view = frame_view;
        ID3D11SamplerState* sampler = g_d3d_sampler;
        ID3D11DeviceContext_RSSetViewports(g_d3d_context, 1u, &viewport);
        ID3D11DeviceContext_IASetInputLayout(g_d3d_context,
                                              g_d3d_input_layout);
        ID3D11DeviceContext_IASetVertexBuffers(g_d3d_context, 0u, 1u,
                                                &g_d3d_vertex_buffer, &stride,
                                                &offset);
        ID3D11DeviceContext_IASetPrimitiveTopology(
            g_d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_VSSetShader(g_d3d_context, g_d3d_vertex_shader,
                                        NULL, 0u);
        ID3D11DeviceContext_PSSetShader(g_d3d_context, g_d3d_pixel_shader,
                                        NULL, 0u);
        ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0u, 1u,
                                                  &view);
        ID3D11DeviceContext_PSSetSamplers(g_d3d_context, 0u, 1u, &sampler);
        ID3D11DeviceContext_Draw(g_d3d_context, 4u, 0u);
        view = NULL;
        ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0u, 1u,
                                                  &view);
    }

    HRESULT result = IDXGISwapChain_Present(g_d3d_swap_chain, 0u, 0u);
    if (FAILED(result)) {
        fprintf(stderr, "window: D3D11 present failed (0x%08lX)\n",
                (unsigned long)result);
        native_presenter_shutdown();
        return false;
    }
    return true;
}

static bool native_presenter_present_bgra(uint32_t width, uint32_t height,
                                          const uint8_t* pixels,
                                          uint32_t stride) {
    if (!pixels || stride < width * 4u ||
        !native_presenter_ensure_frame_texture(width, height)) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT result = ID3D11DeviceContext_Map(
        g_d3d_context, (ID3D11Resource*)g_d3d_frame_texture, 0u,
        D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
    if (FAILED(result))
        return false;
    for (uint32_t y = 0u; y < height; y++) {
        memcpy((uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch,
               pixels + (size_t)y * stride, (size_t)width * 4u);
    }
    ID3D11DeviceContext_Unmap(g_d3d_context,
                              (ID3D11Resource*)g_d3d_frame_texture, 0u);
    return native_presenter_present(g_d3d_frame_view, 0.0f, 0.0f, 0.0f);
}

bool wiiu_window_gpu_clear(uint32_t surface_key, uint32_t width,
                           uint32_t height, uint8_t red, uint8_t green,
                           uint8_t blue, uint8_t alpha) {
    const float color[]={red/255.0f,green/255.0f,blue/255.0f,alpha/255.0f};
    return wiiu_window_gpu_clear_float(surface_key,width,height,color);
}
bool wiiu_window_gpu_clear_float(uint32_t surface_key,uint32_t width,
    uint32_t height,const float color[4]) {
    if(!color)return false;
    NativeGpuSurface* surface =
        native_gpu_ensure_surface(surface_key, width, height);
    if (!surface || !surface->render_target)
        return false;

    ID3D11ShaderResourceView* null_view = NULL;
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0u, 1u,
                                              &null_view);
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 1u,
                                            &surface->render_target, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(g_d3d_context,
                                               surface->render_target, color);
    surface->contents_valid = true;
    return true;
}

bool wiiu_window_gpu_upload_bgra(uint32_t surface_key, uint32_t width,
                                 uint32_t height, const uint8_t* pixels,
                                 uint32_t stride) {
    if (!pixels || stride < width * 4u)
        return false;
    NativeGpuSurface* surface =
        native_gpu_ensure_surface(surface_key, width, height);
    if (!surface || !surface->texture)
        return false;
    unsigned bytes,channels=native_float_layout(surface->format,&bytes);
    uint8_t* converted=NULL;
    if(channels) {
        size_t pitch=(size_t)width*(bytes?channels*bytes:4);
        if(pitch>UINT32_MAX || height>SIZE_MAX/pitch)return false;
        converted=malloc(pitch*height);if(!converted)return false;
        const unsigned order[4]={2,1,0,3};
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            if(!bytes) {
                uint32_t packed=0;
                for(unsigned c=0;c<3;++c) {
                    unsigned shift=c==2?5:4;
                    uint32_t h=native_unorm8_to_half(pixels[(size_t)y*stride+x*4+order[c]]);
                    h=(h+((1u<<(shift-1))-1)+((h>>shift)&1))>>shift;
                    packed|=h<<(c*11);
                }
                memcpy(converted+(size_t)y*pitch+x*4,&packed,4);continue;
            }
            for(unsigned c=0;c<channels;++c) {
                uint8_t value=pixels[(size_t)y*stride+x*4+order[c]];
                uint8_t* dest=converted+(size_t)y*pitch+(x*channels+c)*bytes;
                if(bytes==2){uint16_t h=native_unorm8_to_half(value);memcpy(dest,&h,2);}
                else {float f=value/255.0f;memcpy(dest,&f,4);}
            }
        }
        pixels=converted;stride=(uint32_t)pitch;
    }
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0u, NULL, NULL);
    ID3D11DeviceContext_UpdateSubresource(
        g_d3d_context, (ID3D11Resource*)surface->texture, 0u, NULL, pixels,
        stride, 0u);
    free(converted);
    surface->contents_valid = true;
    surface->use_serial = ++g_native_gpu_use_serial;
    return true;
}

static NativeGpuQuadVertex native_gpu_quad_vertex(
    const WiiUWindowGpuVertex* source, uint32_t target_width,
    uint32_t target_height) {
    NativeGpuQuadVertex result;
    memset(&result, 0, sizeof(result));
    result.x = source->x * 2.0f / (float)target_width - 1.0f;
    result.y = 1.0f - source->y * 2.0f / (float)target_height;
    result.u = source->u;
    result.v = source->v;
    result.red = source->red;
    result.green = source->green;
    result.blue = source->blue;
    result.alpha = source->alpha;
    return result;
}

static bool native_gpu_draw_view(
    NativeGpuSurface* target, uint32_t target_width, uint32_t target_height,
    ID3D11ShaderResourceView* source_view, ID3D11PixelShader* pixel_shader,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    if (!g_d3d_ready || !target || !target->contents_valid ||
        target->width != target_width || target->height != target_height ||
        !source_view || !pixel_shader || !vertices || quad_count == 0u ||
        quad_count > NATIVE_GPU_QUAD_BATCH_LIMIT ||
        !g_d3d_quad_vertex_buffer || !g_d3d_quad_rasterizer_state ||
        !g_d3d_quad_blend_state) {
        return false;
    }

    RECT scissor;
    if (scissor_valid) {
        if (scissor_x >= target_width || scissor_y >= target_height ||
            scissor_width == 0u || scissor_height == 0u) {
            return true;
        }
        uint64_t right = (uint64_t)scissor_x + scissor_width;
        uint64_t bottom = (uint64_t)scissor_y + scissor_height;
        scissor.left = (LONG)scissor_x;
        scissor.top = (LONG)scissor_y;
        scissor.right = (LONG)(right < target_width ? right : target_width);
        scissor.bottom =
            (LONG)(bottom < target_height ? bottom : target_height);
    } else {
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = (LONG)target_width;
        scissor.bottom = (LONG)target_height;
    }
    if (scissor.left >= scissor.right || scissor.top >= scissor.bottom)
        return true;

    const uint32_t order[] = {0u, 1u, 2u, 0u, 2u, 3u};
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT result = ID3D11DeviceContext_Map(
        g_d3d_context, (ID3D11Resource*)g_d3d_quad_vertex_buffer, 0u,
        D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
    if (FAILED(result))
        return false;
    NativeGpuQuadVertex* output = (NativeGpuQuadVertex*)mapped.pData;
    for (uint32_t quad = 0u; quad < quad_count; quad++) {
        for (uint32_t i = 0u; i < 6u; i++) {
            output[quad * 6u + i] = native_gpu_quad_vertex(
                &vertices[quad * 4u + order[i]], target_width, target_height);
        }
    }
    ID3D11DeviceContext_Unmap(g_d3d_context,
                              (ID3D11Resource*)g_d3d_quad_vertex_buffer,
                              0u);

    D3D11_VIEWPORT viewport;
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)target_width;
    viewport.Height = (FLOAT)target_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11ShaderResourceView* view = source_view;
    ID3D11SamplerState* sampler = g_d3d_sampler;
    UINT stride = (UINT)sizeof(NativeGpuQuadVertex);
    UINT offset = 0u;
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 1u,
                                            &target->render_target, NULL);
    ID3D11DeviceContext_RSSetState(g_d3d_context,
                                   g_d3d_quad_rasterizer_state);
    ID3D11DeviceContext_RSSetViewports(g_d3d_context, 1u, &viewport);
    ID3D11DeviceContext_RSSetScissorRects(g_d3d_context, 1u, &scissor);
    ID3D11BlendState* blend_state = native_gpu_get_blend_state(blend_control);
    const FLOAT blend_factor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D11DeviceContext_OMSetBlendState(g_d3d_context, blend_state,
        blend_control && blend_control->constant_valid ? blend_control->constant :
        blend_factor, 0xFFFFFFFFu);
    ID3D11DeviceContext_IASetInputLayout(g_d3d_context,
                                          g_d3d_quad_input_layout);
    ID3D11DeviceContext_IASetVertexBuffers(
        g_d3d_context, 0u, 1u, &g_d3d_quad_vertex_buffer, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_d3d_context,
                                                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_d3d_context, g_d3d_quad_vertex_shader,
                                    NULL, 0u);
    ID3D11DeviceContext_PSSetShader(g_d3d_context, pixel_shader, NULL, 0u);
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0u, 1u, &view);
    ID3D11DeviceContext_PSSetSamplers(g_d3d_context, 0u, 1u, &sampler);
    ID3D11DeviceContext_Draw(g_d3d_context, quad_count * 6u, 0u);
    view = NULL;
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context, 0u, 1u, &view);
    target->contents_valid = true;
    target->use_serial = ++g_native_gpu_use_serial;
    return true;
}

static bool native_post_ensure(void) {
    if(g_d3d_post_vs && g_d3d_post_ps && g_d3d_post_layout &&
       g_d3d_post_vertices && g_d3d_post_constants) return true;
    if(!g_d3d_ready) return false;
    NATIVE_RELEASE(g_d3d_post_vs); NATIVE_RELEASE(g_d3d_post_ps);
    NATIVE_RELEASE(g_d3d_post_layout); NATIVE_RELEASE(g_d3d_post_vertices);
    NATIVE_RELEASE(g_d3d_post_constants);
    ID3DBlob* vs=NULL; ID3DBlob* ps=NULL;
    bool ok=false;
    if(!native_presenter_compile_shader("post_vs_main","vs_5_0",&vs) ||
       !native_presenter_compile_shader("post_ps_main","ps_4_0",&ps)) goto done;
    D3D11_INPUT_ELEMENT_DESC elements[1+WIIU_PIXEL_INPUTS] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,0,64,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32B32A32_FLOAT,0,80,D3D11_INPUT_PER_VERTEX_DATA,0}};
    for(unsigned i=0;i<WIIU_PIXEL_INPUTS;++i)elements[i+1]=(D3D11_INPUT_ELEMENT_DESC){
        "TEXCOORD",i,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16+i*16,D3D11_INPUT_PER_VERTEX_DATA,0};
    if(FAILED(ID3D11Device_CreateVertexShader(g_d3d_device,ID3D10Blob_GetBufferPointer(vs),
        ID3D10Blob_GetBufferSize(vs),NULL,&g_d3d_post_vs)) ||
       FAILED(ID3D11Device_CreatePixelShader(g_d3d_device,ID3D10Blob_GetBufferPointer(ps),
        ID3D10Blob_GetBufferSize(ps),NULL,&g_d3d_post_ps)) ||
       FAILED(ID3D11Device_CreateInputLayout(g_d3d_device,elements,1+WIIU_PIXEL_INPUTS,
        ID3D10Blob_GetBufferPointer(vs),ID3D10Blob_GetBufferSize(vs),&g_d3d_post_layout))) goto done;
    D3D11_BUFFER_DESC desc={0};
    desc.ByteWidth=sizeof(WiiUWindowPostVertex)*6144;
    desc.Usage=D3D11_USAGE_DYNAMIC; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(ID3D11Device_CreateBuffer(g_d3d_device,&desc,NULL,&g_d3d_post_vertices))) goto done;
    desc.ByteWidth=32; desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(ID3D11Device_CreateBuffer(g_d3d_device,&desc,NULL,&g_d3d_post_constants))) goto done;
    ok=true;
done:
    NATIVE_RELEASE(vs); NATIVE_RELEASE(ps);
    return ok;
}

/* The decoded effect textures currently contain only level zero. Preserve
   the game's XY addressing and point/bilinear filtering at that level. */
static ID3D11SamplerState* native_effect_sampler(const WiiUWindowEffectTexture* texture) {
    if(!texture || !texture->sampler_valid)return g_d3d_sampler;
    uint32_t word=texture->sampler_word0;
    for(unsigned i=0;i<64;++i)
        if(g_effect_samplers[i].state && g_effect_samplers[i].word==word)
            return g_effect_samplers[i].state;
    unsigned mag=(word>>9)&7,min=(word>>12)&7,border=(word>>22)&3;
    if((mag!=0 && mag!=1 && mag!=4 && mag!=5) ||
       (min!=0 && min!=1 && min!=4 && min!=5) || border==3)return NULL;
    static const D3D11_TEXTURE_ADDRESS_MODE address[8]={D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_MIRROR,D3D11_TEXTURE_ADDRESS_CLAMP,D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,
        0,0,D3D11_TEXTURE_ADDRESS_BORDER,0};
    D3D11_SAMPLER_DESC d={0};
    d.AddressU=address[word&7];d.AddressV=address[(word>>3)&7];
    d.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    if(!d.AddressU || !d.AddressV)return NULL;
    /* Anisotropic filtering requires a mip chain; retain its requested
       point/bilinear base filter until mipmapped decoding is supported. */
    d.Filter=(D3D11_FILTER)(((min&1)?0x10:0)|((mag&1)?4:0));
    d.MaxAnisotropy=1;d.ComparisonFunc=D3D11_COMPARISON_NEVER;
    d.MinLOD=d.MaxLOD=0;
    if(border==2)d.BorderColor[0]=d.BorderColor[1]=d.BorderColor[2]=1;
    d.BorderColor[3]=border?1:0;
    unsigned slot=64;
    for(unsigned i=0;i<64;++i)if(!g_effect_samplers[i].state){slot=i;break;}
    if(slot==64)return NULL; /* Never invalidate a sampler selected for this draw. */
    ID3D11SamplerState* state=NULL;
    if(FAILED(ID3D11Device_CreateSamplerState(g_d3d_device,&d,&state)))return NULL;
    g_effect_samplers[slot].word=word;g_effect_samplers[slot].state=state;
    return state;
}

static ID3D11PixelShader* native_scene_shader(const WiiULattePixelProgram* program) {
    const char* code=program->hlsl;
    unsigned slot=128;
    for(unsigned i=0;i<128;++i) {
        if(g_scene_shaders[i].code && g_scene_shaders[i].key==program->key && !strcmp(g_scene_shaders[i].code,code)) {
            g_scene_shaders[i].used=++g_scene_shader_serial;return g_scene_shaders[i].shader;
        }
        if(!g_scene_shaders[i].code && slot==128)slot=i;
    }
    if(slot==128) {
        slot=0;for(unsigned i=1;i<128;++i)if(g_scene_shaders[i].used<g_scene_shaders[slot].used)slot=i;
        NATIVE_RELEASE(g_scene_shaders[slot].shader);free(g_scene_shaders[slot].code);g_scene_shaders[slot].code=NULL;
    }
    char* copy=malloc(strlen(code)+1);if(!copy)return NULL;
    strcpy(copy,code);g_scene_shaders[slot].code=copy;
    g_scene_shaders[slot].key=program->key;g_scene_shaders[slot].used=++g_scene_shader_serial;
    ID3DBlob *blob=NULL,*errors=NULL;
    HRESULT hr=D3DCompile(code,strlen(code),"latte_scene.hlsl",NULL,NULL,"main","ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS,0,&blob,&errors);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreatePixelShader(g_d3d_device,ID3D10Blob_GetBufferPointer(blob),
        ID3D10Blob_GetBufferSize(blob),NULL,&g_scene_shaders[slot].shader);
    if(FAILED(hr))fprintf(stderr,"window: scene shader compile failed %08lX: %.*s\n",(unsigned long)hr,
        errors?(int)ID3D10Blob_GetBufferSize(errors):0,errors?(const char*)ID3D10Blob_GetBufferPointer(errors):"");
    NATIVE_RELEASE(blob);NATIVE_RELEASE(errors);
    return g_scene_shaders[slot].shader;
}

static bool native_draw_postprocess(uint32_t target_key,uint32_t width,
    uint32_t height,uint32_t source_key,const uint8_t* pixels,
    uint32_t texture_width,uint32_t texture_height,uint64_t texture_serial,
    const WiiUWindowPostVertex* vertices,uint32_t count,uint32_t mode,
    uint32_t component_map,const float rect[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor_valid,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh,
    const WiiUWindowEffectTexture* textures,const WiiUWindowSceneMaterial* scene) {
    if(!vertices || !rect || !count || count>6144 || count%3 || mode>9 ||
       (source_key && source_key==target_key)) return false;
    NativeGpuSurface* target=native_gpu_find_surface(target_key);
    if(!target || !target->contents_valid || target->width!=width || target->height!=height ||
       !native_post_ensure()) return false;
    ID3D11ShaderResourceView* view=NULL;
    if(scene) { /* Resources are selected by the translated shader below. */
    } else if(source_key) {
        NativeGpuSurface* source=native_gpu_find_surface(source_key);
        if(!source || !source->contents_valid) return false;
        view=source->view;
    } else {
        if(!pixels) return false;
        NativeGpuTexture* texture=native_gpu_ensure_texture(pixels,texture_width,texture_height,texture_serial);
        if(!texture) return false;
        view=texture->view;
    }
    ID3D11ShaderResourceView* views[32]={view};
    unsigned extra=scene?0:mode>=7?2:mode==4?1:0;
    for(unsigned i=0;i<extra;++i) {
        if(!textures || !textures[i+1].pixels) return false;
        const WiiUWindowEffectTexture* input=&textures[i+1];
        NativeGpuTexture* texture=native_gpu_ensure_texture(input->pixels,input->width,input->height,input->serial);
        if(!texture) return false;
        views[i+1]=texture->view;
    }
    ID3D11SamplerState* samplers[16]={g_d3d_sampler,g_d3d_sampler,g_d3d_sampler};
    for(unsigned i=0;i<=extra;++i) {
        samplers[i]=native_effect_sampler(textures?&textures[i]:NULL);
        if(!samplers[i])return false;
    }
    uint32_t constants[8]={0};
    for(uint32_t c=0;c<4;++c) {
        constants[c]=(component_map>>((3-c)*8))&255;
        if(constants[c]>5) return false;
    }
    constants[4]=mode;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11PixelShader* pixel_shader=g_d3d_post_ps;
    ID3D11RenderTargetView* render_targets[8]={target->render_target};
    NativeGpuSurface* scene_targets[8]={target};
    unsigned render_count=1;
    ID3D11BlendState* draw_blend=native_gpu_get_blend_state(blend);
    if(scene) {
        if(!scene->program || !(pixel_shader=native_scene_shader(scene->program)))return false;
        D3D11_BLEND_DESC desc={0};desc.IndependentBlendEnable=TRUE;
        for(unsigned i=0;i<8;++i) {
            render_targets[i]=NULL;scene_targets[i]=NULL;
            if(!(scene->program->target_mask&(1u<<i)))continue;
            uint32_t key=scene->targets[i];if(!i && !key)key=target_key;
            NativeGpuSurface* surface=native_gpu_find_surface(key);
            if(!surface || !surface->contents_valid || surface->width!=width || surface->height!=height)return false;
            for(unsigned j=0;j<i;++j)if(scene_targets[j]==surface)return false;
            scene_targets[i]=surface;render_targets[i]=surface->render_target;render_count=i+1;
            D3D11_BLEND_DESC single={0};
            const WiiUWindowGpuBlendControl* control=scene->targets[i]?&scene->blends[i]:blend;
            ID3D11BlendState* state=native_gpu_get_blend_state(control);
            if(!state)return false;
            ID3D11BlendState_GetDesc(state,&single);desc.RenderTarget[i]=single.RenderTarget[0];
        }
        unsigned slot=64;
        for(unsigned i=0;i<64;++i) {
            if(g_scene_blends[i].state && !memcmp(&g_scene_blends[i].desc,&desc,sizeof(desc))) {slot=i;break;}
        }
        if(slot==64)for(unsigned i=0;i<64;++i)if(!g_scene_blends[i].state) {
            if(FAILED(ID3D11Device_CreateBlendState(g_d3d_device,&desc,&g_scene_blends[i].state)))return false;
            g_scene_blends[i].desc=desc;slot=i;break;
        }
        if(slot==64)return false;
        draw_blend=g_scene_blends[slot].state;
        for(unsigned i=0;i<16;++i) {
            if(scene->program->block_mask&(1u<<i)) {
                uint32_t size=scene->uniform_block_sizes[i];
                if(!scene->uniform_blocks[i] || !size || size%16 || size>1048576)return false;
                if(g_scene_blocks[i].size!=size) {
                    NATIVE_RELEASE(g_scene_blocks[i].view);NATIVE_RELEASE(g_scene_blocks[i].buffer);
                    g_scene_blocks[i].size=0;
                    D3D11_BUFFER_DESC block_desc={0};block_desc.ByteWidth=size;
                    block_desc.Usage=D3D11_USAGE_DEFAULT;block_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                    block_desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;block_desc.StructureByteStride=16;
                    if(FAILED(ID3D11Device_CreateBuffer(g_d3d_device,&block_desc,NULL,&g_scene_blocks[i].buffer)))return false;
                    D3D11_SHADER_RESOURCE_VIEW_DESC srv={0};srv.Format=DXGI_FORMAT_UNKNOWN;
                    srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv.Buffer.NumElements=size/16;
                    if(FAILED(ID3D11Device_CreateShaderResourceView(g_d3d_device,
                        (ID3D11Resource*)g_scene_blocks[i].buffer,&srv,&g_scene_blocks[i].view)))return false;
                    g_scene_blocks[i].size=size;
                }
                ID3D11DeviceContext_UpdateSubresource(g_d3d_context,(ID3D11Resource*)g_scene_blocks[i].buffer,
                    0,NULL,scene->uniform_blocks[i],0,0);
                views[16+i]=g_scene_blocks[i].view;
            }
            if(scene->program->texture_mask&(1u<<i)) {
                uint32_t key=scene->source_surfaces[i];
                for(unsigned j=0;j<8;++j)if(scene_targets[j] && key &&
                    scene_targets[j]==native_gpu_find_surface(key))return false;
                if(key) {
                    NativeGpuSurface* source=native_gpu_find_surface(key);
                    if(!source || !source->contents_valid)return false;
                    views[i]=source->view;
                } else {
                    const WiiUWindowEffectTexture* input=&scene->textures[i];
                    if(!input->pixels)return false;
                    NativeGpuTexture* texture=native_gpu_ensure_texture(input->pixels,input->width,input->height,input->serial);
                    if(!texture)return false;
                    views[i]=texture->view;
                }
            }
            if(scene->program->sampler_mask&(1u<<i)) {
                samplers[i]=native_effect_sampler(&scene->textures[i]);
                if(!samplers[i])return false;
            }
        }
        if(!g_d3d_scene_constants) {
            D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(scene->constants);
            desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if(FAILED(ID3D11Device_CreateBuffer(g_d3d_device,&desc,NULL,&g_d3d_scene_constants)))return false;
        }
        if(FAILED(ID3D11DeviceContext_Map(g_d3d_context,(ID3D11Resource*)g_d3d_scene_constants,
            0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        memcpy(mapped.pData,scene->constants,sizeof(scene->constants));
        ID3D11DeviceContext_Unmap(g_d3d_context,(ID3D11Resource*)g_d3d_scene_constants,0);
    }
    if(FAILED(ID3D11DeviceContext_Map(g_d3d_context,(ID3D11Resource*)g_d3d_post_constants,
        0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
    memcpy(mapped.pData,constants,sizeof(constants));
    ID3D11DeviceContext_Unmap(g_d3d_context,(ID3D11Resource*)g_d3d_post_constants,0);
    if(FAILED(ID3D11DeviceContext_Map(g_d3d_context,(ID3D11Resource*)g_d3d_post_vertices,
        0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
    memcpy(mapped.pData,vertices,(size_t)count*sizeof(*vertices));
    ID3D11DeviceContext_Unmap(g_d3d_context,(ID3D11Resource*)g_d3d_post_vertices,0);
    RECT scissor={0,0,(LONG)width,(LONG)height};
    if(scissor_valid) {
        if(sx>=width || sy>=height || !sw || !sh) return true;
        scissor.left=(LONG)sx; scissor.top=(LONG)sy;
        scissor.right=(LONG)((uint64_t)sx+sw<width?sx+sw:width);
        scissor.bottom=(LONG)((uint64_t)sy+sh<height?sy+sh:height);
    }
    D3D11_VIEWPORT vp={rect[0],rect[1],rect[2],rect[3],0,1};
    UINT stride=sizeof(*vertices),offset=0;
    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context,render_count,render_targets,NULL);
    ID3D11DeviceContext_OMSetDepthStencilState(g_d3d_context,NULL,0);
    ID3D11DeviceContext_RSSetState(g_d3d_context,g_d3d_quad_rasterizer_state);
    ID3D11DeviceContext_RSSetViewports(g_d3d_context,1,&vp);
    ID3D11DeviceContext_RSSetScissorRects(g_d3d_context,1,&scissor);
    const FLOAT factor[4]={1,1,1,1};
    ID3D11DeviceContext_OMSetBlendState(g_d3d_context,draw_blend,
        blend && blend->constant_valid?blend->constant:factor,0xffffffff);
    ID3D11DeviceContext_IASetInputLayout(g_d3d_context,g_d3d_post_layout);
    ID3D11DeviceContext_IASetVertexBuffers(g_d3d_context,0,1,&g_d3d_post_vertices,&stride,&offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_d3d_context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_d3d_context,g_d3d_post_vs,NULL,0);
    ID3D11DeviceContext_PSSetShader(g_d3d_context,pixel_shader,NULL,0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_d3d_context,0,1,&g_d3d_post_constants);
    if(scene)ID3D11DeviceContext_PSSetConstantBuffers(g_d3d_context,1,1,&g_d3d_scene_constants);
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context,0,scene?32:3,views);
    ID3D11DeviceContext_PSSetSamplers(g_d3d_context,0,scene?16:3,samplers);
    ID3D11DeviceContext_Draw(g_d3d_context,count,0);
    memset(views,0,sizeof(views));
    ID3D11DeviceContext_PSSetShaderResources(g_d3d_context,0,scene?32:3,views);
    target->use_serial=++g_native_gpu_use_serial;
    if(scene)for(unsigned i=0;i<8;++i)if(scene_targets[i])scene_targets[i]->use_serial=++g_native_gpu_use_serial;
    return true;
}

bool wiiu_window_gpu_draw_postprocess(uint32_t target,uint32_t width,uint32_t height,
    uint32_t source,const uint8_t* pixels,uint32_t tw,uint32_t th,uint64_t serial,
    const WiiUWindowPostVertex* vertices,uint32_t count,uint32_t mode,uint32_t map,
    const float rect[4],const WiiUWindowGpuBlendControl* blend,bool scissor,
    uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    if(mode>3)return false;
    return native_draw_postprocess(target,width,height,source,pixels,tw,th,serial,
        vertices,count,mode,map,rect,blend,scissor,sx,sy,sw,sh,NULL,NULL);
}
bool wiiu_window_gpu_draw_effect(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowEffectTexture textures[3],const WiiUWindowPostVertex* vertices,
    uint32_t count,uint32_t mode,const float rect[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    if(!textures || mode<4 || mode>8)return false;
    return native_draw_postprocess(target,width,height,0,textures[0].pixels,
        textures[0].width,textures[0].height,textures[0].serial,vertices,count,mode,
        0x00010203,rect,blend,scissor,sx,sy,sw,sh,textures,NULL);
}

bool wiiu_window_gpu_draw_scene(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowSceneMaterial* material,const WiiUWindowPostVertex* vertices,
    uint32_t count,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    if(!material)return false;
    return native_draw_postprocess(target,width,height,0,NULL,0,0,0,vertices,count,9,
        0x00010203,viewport,blend,scissor,sx,sy,sw,sh,NULL,material);
}

bool wiiu_window_gpu_draw_bgra(
    uint32_t surface_key, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(surface_key);
    if (!texture_pixels)
        return false;
    NativeGpuTexture* texture = native_gpu_ensure_texture(
        texture_pixels, texture_width, texture_height, texture_serial);
    if (!texture || !texture->view)
        return false;
    return native_gpu_draw_view(
        target, target_width, target_height, texture->view,
        g_d3d_quad_pixel_shader, vertices, 1u, blend_control, scissor_valid,
        scissor_x,
        scissor_y, scissor_width, scissor_height);
}

bool wiiu_window_gpu_draw_bgra_quads(
    uint32_t surface_key, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(surface_key);
    if (!texture_pixels)
        return false;
    NativeGpuTexture* texture = native_gpu_ensure_texture(
        texture_pixels, texture_width, texture_height, texture_serial);
    if (!texture || !texture->view)
        return false;
    return native_gpu_draw_view(
        target, target_width, target_height, texture->view,
        g_d3d_quad_pixel_shader, vertices, quad_count, blend_control,
        scissor_valid, scissor_x, scissor_y, scissor_width, scissor_height);
}

bool wiiu_window_gpu_draw_bgra_color(
    uint32_t surface_key, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(surface_key);
    if (!texture_pixels)
        return false;
    NativeGpuTexture* texture = native_gpu_ensure_texture(
        texture_pixels, texture_width, texture_height, texture_serial);
    if (!texture || !texture->view)
        return false;
    return native_gpu_draw_view(
        target, target_width, target_height, texture->view,
        g_d3d_surface_pixel_shader, vertices, 1u, blend_control, scissor_valid,
        scissor_x,
        scissor_y, scissor_width, scissor_height);
}

bool wiiu_window_gpu_draw_material(
    uint32_t surface_key, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(surface_key);
    if (!texture_pixels || !material || !g_d3d_material_buffer) return false;
    NativeGpuTexture* texture = native_gpu_ensure_texture(
        texture_pixels, texture_width, texture_height, texture_serial);
    if (!texture || !texture->view) return false;
    ID3D11DeviceContext_UpdateSubresource(g_d3d_context,
        (ID3D11Resource*)g_d3d_material_buffer,0,NULL,material,0,0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_d3d_context,0,1,&g_d3d_material_buffer);
    return native_gpu_draw_view(target,target_width,target_height,texture->view,
        g_d3d_material_pixel_shader,vertices,quad_count,blend_control,scissor_valid,
        scissor_x,scissor_y,scissor_width,scissor_height);
}

bool wiiu_window_gpu_draw_surface(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(target_surface);
    NativeGpuSurface* source = native_gpu_find_surface(source_surface);
    if (!source || source == target || !source->contents_valid ||
        !source->view) {
        return false;
    }
    return native_gpu_draw_view(
        target, target_width, target_height, source->view,
        g_d3d_surface_pixel_shader, vertices, 1u, blend_control, scissor_valid,
        scissor_x,
        scissor_y, scissor_width, scissor_height);
}

bool wiiu_window_gpu_draw_surface_material(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height) {
    NativeGpuSurface* target = native_gpu_find_surface(target_surface);
    NativeGpuSurface* source = native_gpu_find_surface(source_surface);
    if (!source || source == target || !source->contents_valid || !source->view ||
        !material || !g_d3d_material_buffer) return false;
    ID3D11DeviceContext_UpdateSubresource(g_d3d_context,
        (ID3D11Resource*)g_d3d_material_buffer,0,NULL,material,0,0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_d3d_context,0,1,&g_d3d_material_buffer);
    return native_gpu_draw_view(target,target_width,target_height,source->view,
        g_d3d_material_pixel_shader,vertices,1u,blend_control,scissor_valid,
        scissor_x,scissor_y,scissor_width,scissor_height);
}

bool wiiu_window_gpu_finish(void) {
    if(!g_d3d_ready) return true;
    D3D11_QUERY_DESC desc={D3D11_QUERY_EVENT,0};
    ID3D11Query* query=NULL;
    if(FAILED(ID3D11Device_CreateQuery(g_d3d_device,&desc,&query))) return false;
    ID3D11DeviceContext_End(g_d3d_context,(ID3D11Asynchronous*)query);
    ID3D11DeviceContext_Flush(g_d3d_context);
    ULONGLONG started=GetTickCount64();
    HRESULT result;
    do {
        result=ID3D11DeviceContext_GetData(g_d3d_context,(ID3D11Asynchronous*)query,
            NULL,0,0);
        if(result!=S_FALSE) break;
        if(GetTickCount64()-started>=5000u) break;
        Sleep(1);
    } while(true);
    ID3D11Query_Release(query);
    return result==S_OK;
}

static bool native_gpu_readback(uint32_t surface_key, uint8_t* pixels,
                              uint32_t width, uint32_t height,
                              uint32_t stride,bool floating) {
    NativeGpuSurface* surface = native_gpu_find_surface(surface_key);
    if (!g_d3d_ready || !surface || !surface->contents_valid || !pixels ||
        surface->width != width || surface->height != height ||
        stride < width * (floating?16u:4u)) {
        return false;
    }
    if (!surface->readback_texture) {
        D3D11_TEXTURE2D_DESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1u;
        desc.ArraySize = 1u;
        desc.Format = surface->format;
        desc.SampleDesc.Count = 1u;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(ID3D11Device_CreateTexture2D(
                g_d3d_device, &desc, NULL, &surface->readback_texture))) {
            return false;
        }
    }

    ID3D11DeviceContext_OMSetRenderTargets(g_d3d_context, 0u, NULL, NULL);
    ID3D11DeviceContext_CopyResource(
        g_d3d_context, (ID3D11Resource*)surface->readback_texture,
        (ID3D11Resource*)surface->texture);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT result = ID3D11DeviceContext_Map(
        g_d3d_context, (ID3D11Resource*)surface->readback_texture, 0u,
        D3D11_MAP_READ, 0u, &mapped);
    if (FAILED(result))
        return false;
    unsigned bytes,channels=native_float_layout(surface->format,&bytes);
    for(uint32_t y=0;y<height;++y) {
        const uint8_t* row=(const uint8_t*)mapped.pData+(size_t)y*mapped.RowPitch;
        if(!channels && !floating){memcpy(pixels+(size_t)y*stride,row,width*4u);continue;}
        for(uint32_t x=0;x<width;++x) {
            float rgba[4]={0,0,0,1};
            if(!channels){rgba[0]=row[x*4+2]/255.0f;rgba[1]=row[x*4+1]/255.0f;
                rgba[2]=row[x*4]/255.0f;rgba[3]=row[x*4+3]/255.0f;}
            else if(!bytes) {
                uint32_t packed;memcpy(&packed,row+x*4,4);
                rgba[0]=native_half_to_float((uint16_t)((packed&2047)<<4));
                rgba[1]=native_half_to_float((uint16_t)(((packed>>11)&2047)<<4));
                rgba[2]=native_half_to_float((uint16_t)((packed>>22)<<5));
            } else for(unsigned c=0;c<channels;++c) {
                const uint8_t* p=row+(x*channels+c)*bytes;
                if(bytes==2){uint16_t h;memcpy(&h,p,2);rgba[c]=native_half_to_float(h);}
                else memcpy(&rgba[c],p,4);
            }
            if(floating)memcpy(pixels+(size_t)y*stride+x*16,rgba,16);
            else {const unsigned order[4]={2,1,0,3};
                for(unsigned c=0;c<4;++c){float f=rgba[order[c]];
                    pixels[(size_t)y*stride+x*4+c]=!(f>0)?0:f>=1?255:(uint8_t)(f*255+.5f);}}
        }
    }
    ID3D11DeviceContext_Unmap(g_d3d_context,
                              (ID3D11Resource*)surface->readback_texture,
                              0u);
    surface->use_serial = ++g_native_gpu_use_serial;
    return true;
}
bool wiiu_window_gpu_readback(uint32_t key,uint8_t* pixels,uint32_t width,uint32_t height,uint32_t stride) {
    return native_gpu_readback(key,pixels,width,height,stride,false);
}
bool wiiu_window_gpu_readback_float(uint32_t key,float* pixels,uint32_t width,uint32_t height) {
    if(width>UINT32_MAX/16)return false;
    return native_gpu_readback(key,(uint8_t*)pixels,width,height,width*16,true);
}

bool wiiu_window_gpu_present(uint32_t surface_key, uint32_t width,
                             uint32_t height) {
    NativeGpuSurface* surface = native_gpu_find_surface(surface_key);
    if (!surface || !surface->contents_valid || surface->width != width ||
        surface->height != height || !surface->view) {
        return false;
    }

    g_frame_width = width;
    g_frame_height = height;
    g_has_pixels = false;
    g_has_frame = true;
    accept_detailed_frame();
    wiiu_window_show("GX2SwapScanBuffers");
    if (!native_presenter_present(surface->view, 0.0f, 0.0f, 0.0f))
        return false;
    g_has_pixels = true;
    surface->use_serial = ++g_native_gpu_use_serial;
    return true;
}

void wiiu_window_gpu_invalidate(uint32_t surface_key) {
    NativeGpuSurface* surface = native_gpu_find_surface(surface_key);
    if (surface)
        surface->contents_valid = false;
}

static bool native_presenter_initialize(void) {
    if (g_d3d_ready)
        return true;
    if (!g_window)
        return false;

    DXGI_SWAP_CHAIN_DESC swap_desc;
    memset(&swap_desc, 0, sizeof(swap_desc));
    swap_desc.BufferCount = 2u;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.OutputWindow = g_window;
    swap_desc.SampleDesc.Count = 1u;
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level = 0;
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0u, NULL, 0u, D3D11_SDK_VERSION,
        &swap_desc, &g_d3d_swap_chain, &g_d3d_device, &feature_level,
        &g_d3d_context);
    if (FAILED(result)) {
        fprintf(stderr, "window: native D3D11 hardware presenter unavailable "
                        "(0x%08lX); using GDI fallback\n",
                (unsigned long)result);
        native_presenter_shutdown();
        return false;
    }
    if (!native_presenter_create_pipeline() ||
        !native_presenter_create_backbuffer()) {
        native_presenter_shutdown();
        return false;
    }
    g_d3d_ready = true;
    fprintf(stderr,
            "window: native D3D11 hardware presenter ready "
            "(feature-level=0x%X)\n",
            (unsigned)feature_level);
    return true;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        uint32_t button = key_button(wparam);
        bool newly_pressed = button != 0u &&
                            (g_keyboard_held & button) == 0u;
        g_keyboard_held |= button;
        refresh_input_held();
        if (newly_pressed) {
            if (g_input_log_count++ < 24u) {
                fprintf(stderr,
                        "input: native key down button=%08X held=%08X "
                        "pressed=%08X\n",
                        button, g_input_held | button, g_input_pressed);
            }
        }
        update_stick();
        return button != 0u ? 0 : DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        uint32_t button = key_button(wparam);
        bool was_held = button != 0u && (g_keyboard_held & button) != 0u;
        g_keyboard_held &= ~button;
        refresh_input_held();
        if (was_held) {
            if (g_input_log_count++ < 24u) {
                fprintf(stderr,
                        "input: native key up button=%08X held=%08X "
                        "released=%08X\n",
                        button, g_input_held & ~button, g_input_released);
            }
        }
        update_stick();
        return button != 0u ? 0 : DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    case WM_LBUTTONDOWN:
        if (update_touch_position(hwnd, lparam)) {
            g_touch_held = true;
            g_touch_latched = true;
            g_touch_latch_observed = false;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g_touch_held)
            update_touch_position(hwnd, lparam);
        return 0;
    case WM_LBUTTONUP:
        if (g_touch_held)
            update_touch_position(hwnd, lparam);
        g_touch_held = false;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        return 0;
    case WM_KILLFOCUS:
        g_keyboard_held = 0u;
        refresh_input_held();
        update_stick();
        g_touch_held = false;
        g_touch_latched = false;
        g_touch_latch_observed = false;
        return 0;
    case WM_CAPTURECHANGED:
        g_touch_held = false;
        return 0;
    case WM_SIZE:
        if (g_d3d_ready)
            g_d3d_resize_pending = true;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        g_window_closed = true;
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        if (g_d3d_ready) {
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);

        if (g_has_frame && g_frame_width != 0 && g_frame_height != 0) {
            RECT frame_rect;
            if (!frame_rect_for_client(&rect, &frame_rect)) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            LONG client_width = rect.right - rect.left;
            LONG client_height = rect.bottom - rect.top;
            LONG frame_width = frame_rect.right - frame_rect.left;
            LONG frame_height = frame_rect.bottom - frame_rect.top;
            if (g_has_pixels && g_frame_pixels) {
                BITMAPINFO info;
                memset(&info, 0, sizeof(info));
                info.bmiHeader.biSize = sizeof(info.bmiHeader);
                info.bmiHeader.biWidth = (LONG)g_frame_width;
                info.bmiHeader.biHeight = -(LONG)g_frame_height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                SetStretchBltMode(dc, HALFTONE);
                StretchDIBits(dc, frame_rect.left, frame_rect.top,
                              frame_width, frame_height, 0, 0,
                              (int)g_frame_width, (int)g_frame_height,
                              g_frame_pixels, &info, DIB_RGB_COLORS, SRCCOPY);
            } else {
                HBRUSH frame = CreateSolidBrush(
                    RGB(g_frame_red, g_frame_green, g_frame_blue));
                FillRect(dc, &frame_rect, frame);
                DeleteObject(frame);
            }

            HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
            RECT border;
            if (frame_rect.top > rect.top) {
                border = rect;
                border.bottom = frame_rect.top;
                FillRect(dc, &border, bg);
            }
            if (frame_rect.bottom < rect.bottom) {
                border = rect;
                border.top = frame_rect.bottom;
                FillRect(dc, &border, bg);
            }
            if (frame_rect.left > rect.left) {
                border = rect;
                border.top = frame_rect.top;
                border.right = frame_rect.left;
                border.bottom = frame_rect.bottom;
                FillRect(dc, &border, bg);
            }
            if (frame_rect.right < rect.right) {
                border = rect;
                border.left = frame_rect.right;
                border.top = frame_rect.top;
                border.bottom = frame_rect.bottom;
                FillRect(dc, &border, bg);
            }
            DeleteObject(bg);
            EndPaint(hwnd, &ps);
            return 0;
        }

        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &rect, bg);
        DeleteObject(bg);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(226, 232, 240));
        TextOutA(dc, 32, 28, "BOTW Recomp", 11);

        SetTextColor(dc, RGB(125, 211, 252));
        TextOutA(dc, 32, 56, "GX2 boot path reached", 22);

        SetTextColor(dc, RGB(148, 163, 184));
        TextOutA(dc, 32, 88, g_reason, (int)strlen(g_reason));

        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static bool ensure_window_class(void) {
    if (g_class_registered)
        return true;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "BOTWRecompWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassA(&wc))
        return false;

    g_class_registered = true;
    return true;
}

void wiiu_window_pump(void) {
    /* The scheduler calls this every 32 guest dispatches, and input/GX2
       entrypoints call it too. Polling USER32 for every shader bind used to
       dominate otherwise CPU-only loading. Four milliseconds bounds input
       latency without making millions of empty PeekMessage system calls. */
    static LARGE_INTEGER frequency, last_pump_tick;
    LARGE_INTEGER now;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    if (now.QuadPart - last_pump_tick.QuadPart < frequency.QuadPart / 250)
        return;
    last_pump_tick = now;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    poll_xinput();
}

bool wiiu_window_is_closed(void) {
    return g_window_closed;
}

void wiiu_window_advance_input_frame(void) {
    /* A guest can poll input after a present, so retire only observed edges. */
    if (g_input_edges_observed) {
        g_input_pressed = 0u;
        g_input_released = 0u;
        g_input_edges_observed = false;
    }
    if (g_touch_latch_observed) {
        g_touch_latched = false;
        g_touch_latch_observed = false;
    }
}

static BOOL CALLBACK choose_secondary_monitor(HMONITOR monitor, HDC dc,
                                               LPRECT bounds, LPARAM data) {
    (void)dc; (void)bounds;
    MONITORINFO info = {0};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoA(monitor, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY)) {
        *(RECT*)data = info.rcWork;
        return FALSE;
    }
    return TRUE;
}

void wiiu_window_show(const char* reason) {
    if (g_window_closed)
        return;

    if (reason && reason[0] != 0)
        snprintf(g_reason, sizeof(g_reason), "%s", reason);

    if (!g_window) {
        if (!ensure_window_class())
            return;

        int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1280, height = 720;
        const char* monitor = getenv("BOTW_MONITOR");
        if (monitor && strcmp(monitor, "secondary") == 0) {
            RECT work = {0};
            EnumDisplayMonitors(NULL, NULL, choose_secondary_monitor, (LPARAM)&work);
            if (work.right > work.left && work.bottom > work.top) {
                if (width > work.right - work.left) width = work.right - work.left;
                if (height > work.bottom - work.top) height = work.bottom - work.top;
                x = work.left + (work.right - work.left - width) / 2;
                y = work.top + (work.bottom - work.top - height) / 2;
                fprintf(stderr, "window: secondary display work=%ld,%ld %ldx%ld window=%d,%d %dx%d\n",
                        work.left, work.top, work.right-work.left, work.bottom-work.top,
                        x, y, width, height);
            }
        }
        g_window = CreateWindowExA(
            0, "BOTWRecompWindow", "The Legend of Zelda: BOTW Recomp",
            WS_OVERLAPPEDWINDOW, x, y, width, height,
            NULL, NULL, GetModuleHandleA(NULL), NULL);
        if (!g_window)
            return;

        const char* headless = getenv("BOTW_HEADLESS");
        bool hide_window = headless && headless[0] != 0 &&
                           strcmp(headless, "0") != 0;
        g_window_hidden = hide_window;
        ShowWindow(g_window, hide_window ? SW_HIDE : SW_SHOW);
        /* The first ShowWindow can honor a launcher's STARTUPINFO (for
           example a hidden diagnostic console) instead of our argument.
           Apply the explicit game-window policy after consuming that hint. */
        ShowWindow(g_window, hide_window ? SW_HIDE : SW_SHOW);
        native_presenter_initialize();
        if (!hide_window)
            UpdateWindow(g_window);
        fprintf(stderr, "window: opened BOTW GX2 shell (%s%s)\n", g_reason,
                hide_window ? ", hidden" : "");
    } else if (!g_d3d_ready && !g_window_hidden) {
        InvalidateRect(g_window, NULL, FALSE);
    }

}

void wiiu_window_present_solid(uint32_t width, uint32_t height, uint8_t red,
                               uint8_t green, uint8_t blue, uint8_t alpha) {
    (void)alpha;
    if (width == 0 || height == 0)
        return;
    if (!accept_uniform_frame())
        return;
    g_frame_width = width;
    g_frame_height = height;
    g_frame_red = red;
    g_frame_green = green;
    g_frame_blue = blue;
    g_has_pixels = false;
    g_has_frame = true;
    wiiu_window_show("GX2SwapScanBuffers");
    if (g_d3d_ready &&
        !native_presenter_present(NULL, (float)red / 255.0f,
                                  (float)green / 255.0f,
                                  (float)blue / 255.0f) &&
        g_window) {
        InvalidateRect(g_window, NULL, FALSE);
    }
}

void wiiu_window_present_bgra(uint32_t width, uint32_t height,
                              const uint8_t* pixels, uint32_t stride) {
    if (!pixels || width == 0 || height == 0 || stride < width * 4u)
        return;
    if (height > SIZE_MAX / stride)
        return;

    if (frame_is_nearly_uniform(pixels, width, height, stride)) {
        if (!accept_uniform_frame())
            return;
    } else {
        accept_detailed_frame();
    }

    g_frame_width = width;
    g_frame_height = height;
    g_has_pixels = false;
    g_has_frame = true;
    wiiu_window_show("GX2SwapScanBuffers");
    if (g_d3d_ready && native_presenter_present_bgra(width, height, pixels,
                                                      stride)) {
        g_has_pixels = true;
        return;
    }

    size_t required = (size_t)width * height * 4u;
    if (required > g_frame_capacity) {
        uint8_t* replacement = (uint8_t*)realloc(g_frame_pixels, required);
        if (!replacement)
            return;
        g_frame_pixels = replacement;
        g_frame_capacity = required;
    }

    for (uint32_t y = 0; y < height; y++) {
        memcpy(g_frame_pixels + (size_t)y * width * 4u,
               pixels + (size_t)y * stride, (size_t)width * 4u);
    }
    g_has_pixels = true;
    wiiu_window_show("GX2SwapScanBuffers");
}

void wiiu_window_read_input(uint32_t* held, uint32_t* pressed,
                            uint32_t* released, float* left_x,
                            float* left_y) {
    wiiu_window_pump();
    update_stick();
    if (held)
        *held = g_input_held;
    if (pressed)
        *pressed = g_input_pressed;
    if (released)
        *released = g_input_released;
    if (g_input_pressed != 0u || g_input_released != 0u)
        g_input_edges_observed = true;
    if (left_x)
        *left_x = g_left_stick_x;
    if (left_y)
        *left_y = g_left_stick_y;
}

void wiiu_window_read_touch(uint16_t* raw_x, uint16_t* raw_y,
                            uint32_t* touch) {
    wiiu_window_pump();
    if (raw_x)
        *raw_x = g_touch_x;
    if (raw_y)
        *raw_y = g_touch_y;
    if (touch)
        *touch = (g_touch_held || g_touch_latched) ? 1u : 0u;
    if (g_touch_latched)
        g_touch_latch_observed = true;
}

#else
bool wiiu_window_gpu_configure(uint32_t s,uint32_t w,uint32_t h,uint32_t f) {
    (void)s;(void)w;(void)h;(void)f;return false;
}
bool wiiu_window_gpu_clear_float(uint32_t s,uint32_t w,uint32_t h,const float c[4]) {
    (void)s;(void)w;(void)h;(void)c;return false;
}
bool wiiu_window_gpu_readback_float(uint32_t s,float* p,uint32_t w,uint32_t h) {
    (void)s;(void)p;(void)w;(void)h;return false;
}

bool wiiu_window_gpu_draw_material(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    (void)surface; (void)target_width; (void)target_height; (void)texture_pixels;
    (void)texture_width; (void)texture_height; (void)texture_serial;
    (void)vertices; (void)quad_count; (void)material; (void)blend_control;
    (void)scissor_valid; (void)scissor_x; (void)scissor_y;
    (void)scissor_width; (void)scissor_height;
    return false;
}

void wiiu_window_pump(void) {
}

bool wiiu_window_is_closed(void) {
    return false;
}

void wiiu_window_advance_input_frame(void) {
}

void wiiu_window_show(const char* reason) {
    (void)reason;
}

void wiiu_window_present_solid(uint32_t width, uint32_t height, uint8_t red,
                               uint8_t green, uint8_t blue, uint8_t alpha) {
    (void)width;
    (void)height;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}

void wiiu_window_present_bgra(uint32_t width, uint32_t height,
                              const uint8_t* pixels, uint32_t stride) {
    (void)width;
    (void)height;
    (void)pixels;
    (void)stride;
}

bool wiiu_window_gpu_clear(uint32_t surface, uint32_t width, uint32_t height,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t alpha) {
    (void)surface;
    (void)width;
    (void)height;
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    return false;
}

bool wiiu_window_gpu_upload_bgra(uint32_t surface, uint32_t width,
                                 uint32_t height, const uint8_t* pixels,
                                 uint32_t stride) {
    (void)surface;
    (void)width;
    (void)height;
    (void)pixels;
    (void)stride;
    return false;
}

bool wiiu_window_gpu_draw_bgra(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    (void)surface;
    (void)target_width;
    (void)target_height;
    (void)texture_pixels;
    (void)texture_width;
    (void)texture_height;
    (void)texture_serial;
    (void)vertices;
    (void)blend_control;
    (void)scissor_valid;
    (void)scissor_x;
    (void)scissor_y;
    (void)scissor_width;
    (void)scissor_height;
    return false;
}

bool wiiu_window_gpu_draw_bgra_quads(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex* vertices, uint32_t quad_count,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    (void)surface;
    (void)target_width;
    (void)target_height;
    (void)texture_pixels;
    (void)texture_width;
    (void)texture_height;
    (void)texture_serial;
    (void)vertices;
    (void)quad_count;
    (void)blend_control;
    (void)scissor_valid;
    (void)scissor_x;
    (void)scissor_y;
    (void)scissor_width;
    (void)scissor_height;
    return false;
}

bool wiiu_window_gpu_draw_bgra_color(
    uint32_t surface, uint32_t target_width, uint32_t target_height,
    const uint8_t* texture_pixels, uint32_t texture_width,
    uint32_t texture_height, uint64_t texture_serial,
    const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    (void)surface;
    (void)target_width;
    (void)target_height;
    (void)texture_pixels;
    (void)texture_width;
    (void)texture_height;
    (void)texture_serial;
    (void)vertices;
    (void)blend_control;
    (void)scissor_valid;
    (void)scissor_x;
    (void)scissor_y;
    (void)scissor_width;
    (void)scissor_height;
    return false;
}

bool wiiu_window_gpu_draw_surface(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_width,
    uint32_t scissor_height) {
    (void)target_surface;
    (void)target_width;
    (void)target_height;
    (void)source_surface;
    (void)vertices;
    (void)blend_control;
    (void)scissor_valid;
    (void)scissor_x;
    (void)scissor_y;
    (void)scissor_width;
    (void)scissor_height;
    return false;
}

bool wiiu_window_gpu_draw_surface_material(
    uint32_t target_surface, uint32_t target_width, uint32_t target_height,
    uint32_t source_surface, const WiiUWindowGpuVertex vertices[4],
    const WiiUWindowGpuMaterial* material,
    const WiiUWindowGpuBlendControl* blend_control, bool scissor_valid,
    uint32_t scissor_x, uint32_t scissor_y,
    uint32_t scissor_width, uint32_t scissor_height) {
    (void)target_surface; (void)target_width; (void)target_height;
    (void)source_surface; (void)vertices; (void)material; (void)blend_control;
    (void)scissor_valid; (void)scissor_x; (void)scissor_y;
    (void)scissor_width; (void)scissor_height;
    return false;
}

bool wiiu_window_gpu_draw_postprocess(uint32_t target,uint32_t width,
    uint32_t height,uint32_t source,const uint8_t* pixels,uint32_t tw,uint32_t th,
    uint64_t serial,const WiiUWindowPostVertex* vertices,uint32_t count,uint32_t mode,
    uint32_t map,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    (void)target;(void)width;(void)height;(void)source;(void)pixels;(void)tw;(void)th;
    (void)serial;(void)vertices;(void)count;(void)mode;(void)map;(void)viewport;
    (void)blend;(void)scissor;(void)sx;(void)sy;(void)sw;(void)sh;
    return false;
}
bool wiiu_window_gpu_draw_effect(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowEffectTexture textures[3],const WiiUWindowPostVertex* vertices,
    uint32_t count,uint32_t mode,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    (void)target;(void)width;(void)height;(void)textures;(void)vertices;(void)count;
    (void)mode;(void)viewport;(void)blend;(void)scissor;(void)sx;(void)sy;(void)sw;(void)sh;
    return false;
}
bool wiiu_window_gpu_finish(void) { return true; }
bool wiiu_window_gpu_draw_scene(uint32_t target,uint32_t width,uint32_t height,
    const WiiUWindowSceneMaterial* material,const WiiUWindowPostVertex* vertices,
    uint32_t count,const float viewport[4],const WiiUWindowGpuBlendControl* blend,
    bool scissor,uint32_t sx,uint32_t sy,uint32_t sw,uint32_t sh) {
    (void)target;(void)width;(void)height;(void)material;(void)vertices;(void)count;
    (void)viewport;(void)blend;(void)scissor;(void)sx;(void)sy;(void)sw;(void)sh;
    return false;
}

bool wiiu_window_gpu_readback(uint32_t surface, uint8_t* pixels,
                              uint32_t width, uint32_t height,
                              uint32_t stride) {
    (void)surface;
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    return false;
}

bool wiiu_window_gpu_present(uint32_t surface, uint32_t width,
                             uint32_t height) {
    (void)surface;
    (void)width;
    (void)height;
    return false;
}

void wiiu_window_gpu_invalidate(uint32_t surface) {
    (void)surface;
}

void wiiu_window_read_input(uint32_t* held, uint32_t* pressed,
                            uint32_t* released, float* left_x,
                            float* left_y) {
    if (held)
        *held = 0u;
    if (pressed)
        *pressed = 0u;
    if (released)
        *released = 0u;
    if (left_x)
        *left_x = 0.0f;
    if (left_y)
        *left_y = 0.0f;
}

void wiiu_window_read_touch(uint16_t* raw_x, uint16_t* raw_y,
                            uint32_t* touch) {
    if (raw_x)
        *raw_x = 0u;
    if (raw_y)
        *raw_y = 0u;
    if (touch)
        *touch = 0u;
}

#endif
