// SDL 3 for the host, or the part of SDL 3's API the host uses, on SDL 2 (VF_SDL2: the Nintendo
// Switch's homebrew SDL, devkitPro's switch-sdl2, is SDL 2; SDL 3's Switch port is for licensed
// developers only). Without VF_SDL2 this is simply <SDL3/SDL.h>, so the desktop builds are as they were.
//
// The host keeps its SDL 3 code: SDL 3's names are mapped onto SDL 2's here (macros last, after the
// wrappers that call SDL 2's own functions), with SDL 3's meanings: functions that return true on
// success, gamepads, the event fields the host reads, audio streams (on SDL 2's queued audio), the
// renderer's logical presentation. A gamepad's "ID" here is SDL 2's device index (what SDL 2 opens
// one with); the host only passes it back to these functions.
#pragma once

#ifndef VF_SDL2
#include <SDL3/SDL.h>
#else

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#ifdef __SWITCH__
#include "switch_platform.h"   // VF_SWITCH_DIR
#endif

// ---- events: SDL 3's layout of the fields the host reads ----
struct VF_SDL3_Event
{
	Uint32 type = 0;
	struct { SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN; bool repeat = false, down = false; } key;
	struct { Uint8 button = 0; SDL_JoystickID which = 0; bool down = false; } gbutton;
	struct { Uint8 axis = 0; Sint16 value = 0; SDL_JoystickID which = 0; } gaxis;
	struct { float x = 0, y = 0; } motion;
	struct { Uint8 button = 0; float x = 0, y = 0; bool down = false; } button;
	struct { float x = 0, y = 0; } wheel;
	SDL_Event native{};   // SDL 2's own
};

// SDL 2 has no event of its own for these (its window events carry them): numbers of ours
enum : Uint32 {
	VF_EVENT_WINDOW_PIXEL_SIZE_CHANGED = 0x9001,
	VF_EVENT_WINDOW_ENTER_FULLSCREEN = 0x9002,
	VF_EVENT_WINDOW_LEAVE_FULLSCREEN = 0x9003,
};

enum SDL_RendererLogicalPresentation { SDL_LOGICAL_PRESENTATION_DISABLED, SDL_LOGICAL_PRESENTATION_LETTERBOX };

struct VF_SDL3_AudioSpec { SDL_AudioFormat format; int channels; int freq; };
struct VF_SDL3_AudioStream { SDL_AudioDeviceID dev = 0; float gain = 1.0f; std::vector<Sint16> tmp; };

enum SDL_PathType { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY, SDL_PATHTYPE_OTHER };
struct SDL_PathInfo { SDL_PathType type = SDL_PATHTYPE_NONE; };
// properties: only for the 3D's own GPU textures (the GPU builds'), which SDL 2 has none of
typedef Uint32 SDL_PropertiesID;

struct SDL_DialogFileFilter { const char *name; const char *pattern; };
typedef void (*SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);

namespace vfsdl {

inline bool ok(int r) { return r == 0; }

// -- events --
// SDL 2 gives a mouse event (a touch, on the Switch) in the renderer's logical coordinates while
// one is set; SDL 3 in the window's, which the menu expects: back to the window's
inline void mouse_to_window(Uint32 window_id, int x, int y, float &wx, float &wy)
{
	wx = float(x); wy = float(y);
	SDL_Renderer *r = SDL_GetRenderer(SDL_GetWindowFromID(window_id));
	int lw = 0, lh = 0;
	if (r) SDL_RenderGetLogicalSize(r, &lw, &lh);
	if (r && lw && lh) {
		int ix = 0, iy = 0;
		SDL_RenderLogicalToWindow(r, float(x), float(y), &ix, &iy);
		wx = float(ix); wy = float(iy);
	}
}
inline void from_sdl2(const SDL_Event &n, VF_SDL3_Event &e)
{
	e = VF_SDL3_Event{};
	e.native = n;
	e.type = n.type;
	switch (n.type) {
	case SDL_KEYDOWN: case SDL_KEYUP:
		e.key.scancode = n.key.keysym.scancode;
		e.key.repeat = n.key.repeat != 0;
		e.key.down = n.key.state == SDL_PRESSED;
		break;
	case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
		e.gbutton.button = n.cbutton.button;
		e.gbutton.which = n.cbutton.which;
		e.gbutton.down = n.cbutton.state == SDL_PRESSED;
		break;
	case SDL_CONTROLLERAXISMOTION:
		e.gaxis.axis = n.caxis.axis;
		e.gaxis.value = n.caxis.value;
		e.gaxis.which = n.caxis.which;
		break;
	case SDL_MOUSEMOTION:
		mouse_to_window(n.motion.windowID, n.motion.x, n.motion.y, e.motion.x, e.motion.y);
		break;
	case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
		e.button.button = n.button.button;
		mouse_to_window(n.button.windowID, n.button.x, n.button.y, e.button.x, e.button.y);
		e.button.down = n.button.state == SDL_PRESSED;
		break;
	case SDL_MOUSEWHEEL:
		e.wheel.x = float(n.wheel.x); e.wheel.y = float(n.wheel.y);
		break;
	case SDL_WINDOWEVENT:
		if (n.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) e.type = VF_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
		break;
	default: break;
	}
}

inline bool poll_event(VF_SDL3_Event *e)
{
	SDL_Event n;
	if (!SDL_PollEvent(&n)) return false;
	if (e) from_sdl2(n, *e);
	return true;
}

// the host pushes keys and the renderer's resets (its tests)
inline bool push_event(VF_SDL3_Event *e)
{
	SDL_Event n{};
	n.type = e->type;
	if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
		n.key.keysym.scancode = e->key.scancode;
		n.key.keysym.sym = SDL_GetKeyFromScancode(e->key.scancode);
		n.key.state = e->key.down ? SDL_PRESSED : SDL_RELEASED;
		n.key.repeat = e->key.repeat;
	} else if (e->type >= 0x9000) return false;
	return SDL_PushEvent(&n) == 1;
}

// -- time --
inline Uint64 ticks_ns()
{
	static const auto t0 = std::chrono::steady_clock::now();
	return Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}
inline void delay_ns(Uint64 ns) { std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }

// -- paths --
#ifdef __SWITCH__
// The program's folder and the player's data folder are one: sdmc:/switch/vf1-recomp-nx/ (the .nro's
// own folder, where the roms/ folder, settings, saves and log.txt go)
inline const char *base_path() { return VF_SWITCH_DIR "/"; }
inline char *pref_path(const char *, const char *) { mkdir(VF_SWITCH_DIR, 0777); return SDL_strdup(base_path()); }
#else
inline const char *base_path() { static char *b = SDL_GetBasePath(); return b; }
inline char *pref_path(const char *org, const char *app) { return SDL_GetPrefPath(org ? org : "", app); }
#endif
inline bool path_info(const char *p, SDL_PathInfo *info)
{
	struct stat st;
	if (stat(p, &st) != 0) return false;
	if (info) info->type = S_ISREG(st.st_mode) ? SDL_PATHTYPE_FILE : S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_OTHER;
	return true;
}

// -- windows and displays --
inline SDL_Window *create_window(const char *title, int w, int h, Uint32 flags)
{
	if (flags & SDL_WINDOW_FULLSCREEN) flags = (flags & ~Uint32(SDL_WINDOW_FULLSCREEN)) | SDL_WINDOW_FULLSCREEN_DESKTOP;
	return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
}
inline bool set_window_fullscreen(SDL_Window *w, bool on) { return SDL_SetWindowFullscreen(w, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0; }
inline const SDL_DisplayMode *current_display_mode(int display)
{
	static SDL_DisplayMode m;
	return SDL_GetCurrentDisplayMode(display < 0 ? 0 : display, &m) == 0 ? &m : nullptr;
}
inline float window_pixel_density(SDL_Window *w)
{
	int pw = 0, ph = 0, ww = 0, wh = 0;
	SDL_GetWindowSizeInPixels(w, &pw, &ph);
	SDL_GetWindowSize(w, &ww, &wh);
	return ww > 0 ? float(pw) / float(ww) : 1.0f;
}
inline bool show_message_box(Uint32 flags, const char *title, const char *msg, SDL_Window *w)
{
#ifdef __SWITCH__
	(void)flags; (void)title; (void)msg; (void)w;
	return false;   // (main.cpp's message_box shows them on the Switch)
#else
	return SDL_ShowSimpleMessageBox(flags, title, msg, w) == 0;
#endif
}
inline bool show_cursor(bool on) { return SDL_ShowCursor(on ? SDL_ENABLE : SDL_DISABLE) >= 0; }
inline void show_open_file_dialog(SDL_DialogFileCallback cb, void *ud, SDL_Window *, const SDL_DialogFileFilter *, int, const char *, bool)
{
	cb(ud, nullptr, -1);   // no file picker here
}

// -- the renderer --
inline SDL_Renderer *create_renderer(SDL_Window *w, const char *name)
{
	if (name) SDL_SetHint(SDL_HINT_RENDER_DRIVER, name);
	return SDL_CreateRenderer(w, -1, 0);   // (the best there is, as SDL 3 picks: accelerated first)
}
inline const char *renderer_name(SDL_Renderer *r)
{
	SDL_RendererInfo info;
	return SDL_GetRendererInfo(r, &info) == 0 ? info.name : nullptr;
}
inline SDL_Renderer *renderer_properties(SDL_Renderer *r) { return r; }
inline Sint64 number_property(SDL_Renderer *r, const char *name, Sint64 def)
{
	SDL_RendererInfo info;
	if (r && !strcmp(name, "SDL.renderer.max_texture_size") && SDL_GetRendererInfo(r, &info) == 0)
		return std::min(info.max_texture_width, info.max_texture_height);
	return def;
}
inline bool get_render_vsync(SDL_Renderer *r, int *v)
{
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(r, &info) != 0) return false;
	if (v) *v = (info.flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
	return true;
}
inline bool set_logical_presentation(SDL_Renderer *r, int w, int h, SDL_RendererLogicalPresentation mode)
{
	if (mode == SDL_LOGICAL_PRESENTATION_DISABLED) w = h = 0;
	return SDL_RenderSetLogicalSize(r, w, h) == 0;
}
inline bool get_logical_presentation(SDL_Renderer *r, int *w, int *h, SDL_RendererLogicalPresentation *mode)
{
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(r, &lw, &lh);
	if (w) *w = lw;
	if (h) *h = lh;
	if (mode) *mode = lw && lh ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED;
	return true;
}
// where the logical picture lands in the output's pixels (SDL 2's letterbox: the largest that fits, centred)
inline bool logical_presentation_rect(SDL_Renderer *r, SDL_FRect *out)
{
	int lw = 0, lh = 0, ow = 0, oh = 0;
	SDL_RenderGetLogicalSize(r, &lw, &lh);
	if (SDL_GetRendererOutputSize(r, &ow, &oh) != 0) return false;
	if (!lw || !lh) { *out = SDL_FRect{ 0, 0, float(ow), float(oh) }; return true; }
	float s = std::min(float(ow) / float(lw), float(oh) / float(lh));
	if (SDL_RenderGetIntegerScale(r)) s = std::max(1.0f, std::floor(s));
	const float w = lw * s, h = lh * s;
	*out = SDL_FRect{ std::floor((ow - w) / 2), std::floor((oh - h) / 2), w, h };
	return true;
}
inline bool output_size(SDL_Renderer *r, int *w, int *h) { return SDL_GetRendererOutputSize(r, w, h) == 0; }

// a part of a texture: SDL 2's copy takes whole texels for the source, so a source with fractions
// is drawn as two triangles with the texture's own coordinates
inline bool render_texture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst)
{
	if (!src) return SDL_RenderCopyF(r, t, nullptr, dst) == 0;
	const SDL_Rect is{ int(src->x), int(src->y), int(src->w), int(src->h) };
	if (float(is.x) == src->x && float(is.y) == src->y && float(is.w) == src->w && float(is.h) == src->h)
		return SDL_RenderCopyF(r, t, &is, dst) == 0;
	int tw = 0, th = 0;
	if (SDL_QueryTexture(t, nullptr, nullptr, &tw, &th) != 0 || tw <= 0 || th <= 0) return false;
	SDL_FRect d;
	if (dst) d = *dst;
	else {
		int ow = 0, oh = 0;
		SDL_RenderGetLogicalSize(r, &ow, &oh);
		if (!ow || !oh) SDL_GetRendererOutputSize(r, &ow, &oh);
		d = SDL_FRect{ 0, 0, float(ow), float(oh) };
	}
	const float u0 = src->x / tw, v0 = src->y / th, u1 = (src->x + src->w) / tw, v1 = (src->y + src->h) / th;
	const SDL_Color c{ 255, 255, 255, 255 };
	const SDL_Vertex v[4] = {
		{ { d.x, d.y }, c, { u0, v0 } }, { { d.x + d.w, d.y }, c, { u1, v0 } },
		{ { d.x + d.w, d.y + d.h }, c, { u1, v1 } }, { { d.x, d.y + d.h }, c, { u0, v1 } } };
	const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	return SDL_RenderGeometry(r, t, v, 4, idx, 6) == 0;
}
inline bool set_clip_rect(SDL_Renderer *r, const SDL_Rect *c) { return SDL_RenderSetClipRect(r, c) == 0; }
inline bool fill_rect(SDL_Renderer *r, const SDL_FRect *f) { return SDL_RenderFillRectF(r, f) == 0; }

// the render target's (or the output's) pixels, as a new surface
inline SDL_Surface *render_read_pixels(SDL_Renderer *r, const SDL_Rect *rect)
{
	int w = 0, h = 0;
	if (rect) { w = rect->w; h = rect->h; }
	else if (SDL_Texture *t = SDL_GetRenderTarget(r)) SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
	else SDL_GetRendererOutputSize(r, &w, &h);
	if (w <= 0 || h <= 0) return nullptr;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return nullptr;
	if (SDL_RenderReadPixels(r, rect, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) != 0) { SDL_FreeSurface(s); return nullptr; }
	return s;
}
inline bool set_texture_scale_mode(SDL_Texture *t, int mode) { return SDL_SetTextureScaleMode(t, SDL_ScaleMode(mode)) == 0; }

// -- gamepads (an "ID" is SDL 2's device index) --
inline SDL_JoystickID *get_gamepads(int *count)
{
	std::vector<SDL_JoystickID> v;
	for (int i = 0, n = SDL_NumJoysticks(); i < n; i++)
		if (SDL_IsGameController(i)) v.push_back(i);
	SDL_JoystickID *out = static_cast<SDL_JoystickID *>(SDL_malloc(sizeof(SDL_JoystickID) * (v.size() + 1)));
	for (size_t i = 0; i < v.size(); i++) out[i] = v[i];
	out[v.size()] = 0;
	if (count) *count = int(v.size());
	return out;
}
inline SDL_JoystickID gamepad_id(SDL_GameController *g)
{
	const SDL_JoystickID inst = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g));
	for (int i = 0, n = SDL_NumJoysticks(); i < n; i++)
		if (SDL_JoystickGetDeviceInstanceID(i) == inst) return i;
	return -1;
}

// -- audio: SDL 3's stream on SDL 2's queued audio --
inline VF_SDL3_AudioStream *open_audio_device_stream(int, const VF_SDL3_AudioSpec *spec, void *, void *)
{
	SDL_AudioSpec want{}, have{};
	want.freq = spec->freq;
	want.format = spec->format;
	want.channels = Uint8(spec->channels);
	want.samples = 1024;
	const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // SDL 2 converts to the device's own
	if (!dev) return nullptr;
	auto *s = new VF_SDL3_AudioStream;
	s->dev = dev;
	return s;   // paused, as SDL 3 opens it
}
inline bool put_audio_stream_data(VF_SDL3_AudioStream *s, const void *data, int len)
{
	if (!s || len <= 0) return s != nullptr;
	if (s->gain == 1.0f) return SDL_QueueAudio(s->dev, data, Uint32(len)) == 0;
	const size_t n = size_t(len) / 2;
	s->tmp.resize(n);
	const Sint16 *in = static_cast<const Sint16 *>(data);
	for (size_t i = 0; i < n; i++) s->tmp[i] = Sint16(std::lround(std::clamp(in[i] * s->gain, -32768.0f, 32767.0f)));
	return SDL_QueueAudio(s->dev, s->tmp.data(), Uint32(n * 2)) == 0;
}
inline void destroy_audio_stream(VF_SDL3_AudioStream *s) { if (s) { SDL_CloseAudioDevice(s->dev); delete s; } }
inline int audio_stream_queued(VF_SDL3_AudioStream *s) { return s ? int(SDL_GetQueuedAudioSize(s->dev)) : 0; }
inline bool set_audio_stream_gain(VF_SDL3_AudioStream *s, float g) { if (!s) return false; s->gain = g; return true; }
inline bool pause_audio(VF_SDL3_AudioStream *s, bool pause) { if (!s) return false; SDL_PauseAudioDevice(s->dev, pause ? 1 : 0); return true; }

// -- files (the GPU 3D's crash marker; that build isn't SDL 2's, but kept compiling) --
inline FILE *io_from_file(const char *p, const char *mode) { return fopen(p, mode); }
inline bool close_io(FILE *f) { return f && fclose(f) == 0; }

} // namespace vfsdl

// ---- SDL 3's names ----
#define SDL_Event VF_SDL3_Event
#define SDL_PollEvent(e) vfsdl::poll_event(e)
#define SDL_PushEvent(e) vfsdl::push_event(e)
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define SDL_EVENT_GAMEPAD_ADDED SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED SDL_CONTROLLERDEVICEREMOVED
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_EVENT_GAMEPAD_BUTTON_UP SDL_CONTROLLERBUTTONUP
#define SDL_EVENT_GAMEPAD_AXIS_MOTION SDL_CONTROLLERAXISMOTION
#define SDL_EVENT_RENDER_TARGETS_RESET SDL_RENDER_TARGETS_RESET
#define SDL_EVENT_RENDER_DEVICE_RESET SDL_RENDER_DEVICE_RESET
#define SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED VF_EVENT_WINDOW_PIXEL_SIZE_CHANGED
#define SDL_EVENT_WINDOW_ENTER_FULLSCREEN VF_EVENT_WINDOW_ENTER_FULLSCREEN
#define SDL_EVENT_WINDOW_LEAVE_FULLSCREEN VF_EVENT_WINDOW_LEAVE_FULLSCREEN

#define SDL_GetTicksNS() vfsdl::ticks_ns()
#define SDL_GetNumLogicalCPUCores() SDL_GetCPUCount()
#define SDL_DelayNS(ns) vfsdl::delay_ns(ns)

#define SDL_Init(f) vfsdl::ok(SDL_Init(f))
#define SDL_InitSubSystem(f) vfsdl::ok(SDL_InitSubSystem(f))
#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER
#define SDL_SetError(...) (SDL_SetError(__VA_ARGS__), false)
#define SDL_GetBasePath() vfsdl::base_path()
#define SDL_GetPrefPath(o, a) vfsdl::pref_path(o, a)
#define SDL_GetPathInfo(p, i) vfsdl::path_info(p, i)
#define SDL_CreateProperties() SDL_PropertiesID(1)
#define SDL_DestroyProperties(p) ((void)(p))
#define SDL_SetNumberProperty(p, n, v) ((void)(p), (void)(v), false)
#define SDL_SetPointerProperty(p, n, v) ((void)(p), (void)(v), false)
#define SDL_CreateTextureWithProperties(r, p) ((void)(r), (void)(p), (SDL_Texture *)nullptr)
#define SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER "SDL.texture.create.format"
#define SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER "SDL.texture.create.access"
#define SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER "SDL.texture.create.width"
#define SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER "SDL.texture.create.height"
#define SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER "SDL.texture.create.gpu.texture"
#define SDL_RemovePath(p) (remove(p) == 0)
#define SDL_IOStream FILE
#define SDL_IOFromFile(p, m) vfsdl::io_from_file(p, m)
#define SDL_CloseIO(f) vfsdl::close_io(f)
#define SDL_ShowSimpleMessageBox(f, t, m, w) vfsdl::show_message_box(f, t, m, w)
#define SDL_ShowOpenFileDialog vfsdl::show_open_file_dialog

#define SDL_WINDOW_HIGH_PIXEL_DENSITY SDL_WINDOW_ALLOW_HIGHDPI
#define SDL_WINDOW_OCCLUDED 0
#define SDL_CreateWindow(t, w, h, f) vfsdl::create_window(t, w, h, f)
#define SDL_SetWindowFullscreen(w, on) vfsdl::set_window_fullscreen(w, on)
#define SDL_SyncWindow(w) ((void)(w), true)
#define SDL_GetPrimaryDisplay() 0
#define SDL_GetDisplayForWindow(w) SDL_GetWindowDisplayIndex(w)
#define SDL_GetDisplayUsableBounds(d, r) vfsdl::ok(SDL_GetDisplayUsableBounds(d, r))
#define SDL_GetCurrentDisplayMode(d) vfsdl::current_display_mode(d)
#define SDL_GetWindowPixelDensity(w) vfsdl::window_pixel_density(w)
#define SDL_HideCursor() vfsdl::show_cursor(false)
#define SDL_ShowCursor() vfsdl::show_cursor(true)
#define SDL_GetKeyboardState(n) reinterpret_cast<const bool *>(SDL_GetKeyboardState(n))

#define SDL_CreateRenderer(w, n) vfsdl::create_renderer(w, n)
#define SDL_GetRendererName(r) vfsdl::renderer_name(r)
#define SDL_GetRendererProperties(r) vfsdl::renderer_properties(r)
#define SDL_GetNumberProperty(p, n, d) vfsdl::number_property(p, n, d)
#define SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER "SDL.renderer.max_texture_size"
#define SDL_SetRenderVSync(r, v) vfsdl::ok(SDL_RenderSetVSync(r, v))
#define SDL_GetRenderVSync(r, v) vfsdl::get_render_vsync(r, v)
#define SDL_SetRenderLogicalPresentation(r, w, h, m) vfsdl::set_logical_presentation(r, w, h, m)
#define SDL_GetRenderLogicalPresentation(r, w, h, m) vfsdl::get_logical_presentation(r, w, h, m)
#define SDL_GetRenderLogicalPresentationRect(r, f) vfsdl::logical_presentation_rect(r, f)
#define SDL_GetCurrentRenderOutputSize(r, w, h) vfsdl::output_size(r, w, h)
#define SDL_RenderTexture(r, t, s, d) vfsdl::render_texture(r, t, s, d)
#define SDL_RenderFillRect(r, f) vfsdl::fill_rect(r, f)
#define SDL_SetRenderClipRect(r, c) vfsdl::set_clip_rect(r, c)
#define SDL_RenderReadPixels(r, rc) vfsdl::render_read_pixels(r, rc)
#define SDL_SetRenderTarget(r, t) vfsdl::ok(SDL_SetRenderTarget(r, t))
#define SDL_LockTexture(t, r, p, pitch) vfsdl::ok(SDL_LockTexture(t, r, p, pitch))
#define SDL_SetTextureBlendMode(t, b) vfsdl::ok(SDL_SetTextureBlendMode(t, b))
#define SDL_SetTextureScaleMode(t, m) vfsdl::set_texture_scale_mode(t, m)
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#define SDL_SCALEMODE_LINEAR SDL_ScaleModeLinear
#define SDL_ConvertSurface(s, f) SDL_ConvertSurfaceFormat(s, f, 0)
#define SDL_DestroySurface(s) SDL_FreeSurface(s)

#define SDL_Gamepad SDL_GameController
#define SDL_GamepadButton SDL_GameControllerButton
#define SDL_GetGamepads(n) vfsdl::get_gamepads(n)
#define SDL_OpenGamepad(id) SDL_GameControllerOpen(id)
#define SDL_CloseGamepad(g) SDL_GameControllerClose(g)
#define SDL_GetGamepadID(g) vfsdl::gamepad_id(g)
#define SDL_GetGamepadGUIDForID(id) SDL_JoystickGetDeviceGUID(id)
#define SDL_GetGamepadName(g) SDL_GameControllerName(g)
#define SDL_GetGamepadButton(g, b) (SDL_GameControllerGetButton(g, SDL_GameControllerButton(b)) != 0)
#define SDL_GetGamepadAxis(g, a) SDL_GameControllerGetAxis(g, SDL_GameControllerAxis(a))
#define SDL_GetGamepadStringForButton(b) SDL_GameControllerGetStringForButton(b)
#define SDL_GetGamepadButtonFromString(s) SDL_GameControllerGetButtonFromString(s)
#define SDL_GAMEPAD_BUTTON_INVALID SDL_CONTROLLER_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_SOUTH SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_WEST SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_BACK SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_GUIDE SDL_CONTROLLER_BUTTON_GUIDE
#define SDL_GAMEPAD_BUTTON_START SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LEFT_STICK SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_TOUCHPAD SDL_CONTROLLER_BUTTON_TOUCHPAD
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_AXIS_LEFTX SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT

#define SDL_AudioSpec VF_SDL3_AudioSpec
#define SDL_AudioStream VF_SDL3_AudioStream
#define SDL_AUDIO_S16 AUDIO_S16SYS
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0
#define SDL_OpenAudioDeviceStream(d, s, cb, ud) vfsdl::open_audio_device_stream(d, s, cb, ud)
#define SDL_PutAudioStreamData(s, d, n) vfsdl::put_audio_stream_data(s, d, n)
#define SDL_GetAudioStreamQueued(s) vfsdl::audio_stream_queued(s)
#define SDL_DestroyAudioStream(s) vfsdl::destroy_audio_stream(s)
#define SDL_SetAudioStreamGain(s, g) vfsdl::set_audio_stream_gain(s, g)
#define SDL_PauseAudioStreamDevice(s) vfsdl::pause_audio(s, true)
#define SDL_ResumeAudioStreamDevice(s) vfsdl::pause_audio(s, false)

#endif // VF_SDL2
