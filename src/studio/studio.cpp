// studio.cpp - Vectrex Colour Studio: the PC's colour editor for the cabinet's Vectrex emulator.
//
// A window (SDL2 + Dear ImGui) with the game list (this PC's and, connected, the cabinet's), the game
// running in the middle (paused to paint), and the tools: what to colour (WHAT), the palette, the
// brightness, the profile and its rules, undo. A picked line (click), lines in a box (drag), more lines
// (Shift or Ctrl) are coloured with a click on the palette. The profiles are the cabinet's own files
// (render/colors.h); the rules are made by the cabinet's colour editor's own code (platform/coloredit.h),
// so a profile made here is the same as one made there.
//
// The cabinet: over SSH (Windows' own OpenSSH, no console window). The password is handed to ssh by
// this program itself (it is its own SSH_ASKPASS); a key file is used when there. Connected, the
// cabinet's games are listed (a game only there is fetched with its profiles), its picture settings
// are used for the preview, and a profile is sent there (and can be chosen there for the game).
//
// The core opens one cartridge per run (core/vx_core.h): another game starts the program again
// (--game PATH). Windows only.
//
// Everything on the screen is in English; every control has a tooltip.

#ifndef _WIN32
#error "Vectrex Colour Studio is a Windows program"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

extern "C" {
#include "core/vx_core.h"
#include "platform/coloredit.h"
#include "platform/config.h"
#include "platform/roms.h"
#include "render/colors.h"
#include "render/draw.h"
#include <vectorpie/vp_paths.h>
}

#include <algorithm>
#include <atomic>
#include <deque>
#include <cctype>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ------------------------------------------------------------------------------------------------
// small helpers

static std::string asset(const char *rel)
{
    char p[1100];
    vp_asset_path(rel, p, sizeof p);
    return p;
}

static bool file_there(const std::string &p)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

static std::string read_file(const std::string &p)
{
    std::string s;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return s;
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, k);
    fclose(f);
    return s;
}

static bool write_file(const std::string &p, const std::string &data)
{
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

static std::string base_name(const std::string &p)
{
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

static void tip(const char *t)                   // the hover help of the control just drawn
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(t);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ------------------------------------------------------------------------------------------------
// settings: the program's own (studio.ini next to it) and the emulator's (as main.c load_config)

struct Studio {
    char host[128] = "192.168.0.122";
    char user[64] = "pi";
    char key[512] = "";
    int  monitor_look = 0;          // the preview with a glow as on the vector monitor (off: clear lines)
    int  show_free = 0;             // lines no rule colours, in magenta
    int  cabinet_look = 1;          // the cabinet's picture settings, when known
};
static Studio S;

// the controls: what plays the game and steps the picture, each on a key and a gamepad input (set in
// Settings > Controls, kept in studio.ini). A gamepad input: a button (0..), or an axis direction
// (PAD_AXIS + axis * 2 + 1 for the plus side); -1 none. The left stick always steers too.
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_1, B_2, B_3, B_4, B_PAUSE, B_BACK, B_STEP, B_COUNT };
enum { PAD_AXIS = 100 };
struct Bind { const char *id, *label; int key, pad, key0, pad0; };
static Bind BIND[B_COUNT] = {
    { "up",    "Stick up",          SDL_SCANCODE_UP,     SDL_CONTROLLER_BUTTON_DPAD_UP,       0, 0 },
    { "down",  "Stick down",        SDL_SCANCODE_DOWN,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,     0, 0 },
    { "left",  "Stick left",        SDL_SCANCODE_LEFT,   SDL_CONTROLLER_BUTTON_DPAD_LEFT,     0, 0 },
    { "right", "Stick right",       SDL_SCANCODE_RIGHT,  SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    0, 0 },
    { "b1",    "Button 1",          SDL_SCANCODE_A,      SDL_CONTROLLER_BUTTON_A,             0, 0 },
    { "b2",    "Button 2",          SDL_SCANCODE_S,      SDL_CONTROLLER_BUTTON_B,             0, 0 },
    { "b3",    "Button 3",          SDL_SCANCODE_D,      SDL_CONTROLLER_BUTTON_X,             0, 0 },
    { "b4",    "Button 4",          SDL_SCANCODE_F,      SDL_CONTROLLER_BUTTON_Y,             0, 0 },
    { "pause", "Play / pause",      SDL_SCANCODE_SPACE,  SDL_CONTROLLER_BUTTON_START,         0, 0 },
    { "back",  "One frame back",    SDL_SCANCODE_COMMA,  SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  0, 0 },
    { "step",  "One frame on",      SDL_SCANCODE_PERIOD, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0, 0 },
};
static SDL_GameController *g_pad = nullptr;
static int g_bind_wait = -1, g_bind_col = 0;    // waiting for an input for this action (0 key, 1 gamepad)
static bool g_show_controls = false;

static void binds_defaults_keep()                // (the defaults remembered once, for "Reset")
{
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < B_COUNT; i++) { BIND[i].key0 = BIND[i].key; BIND[i].pad0 = BIND[i].pad; }
}

static std::string key_name(int sc)
{
    if (sc <= 0) return "-";
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    return n && *n ? n : "key " + std::to_string(sc);
}

static std::string pad_name(int code)
{
    if (code < 0) return "-";
    if (code >= PAD_AXIS) {
        int a = (code - PAD_AXIS) / 2, plus = (code - PAD_AXIS) % 2;
        static const char *AX[] = { "Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "Left trigger", "Right trigger" };
        std::string n = a < 6 ? AX[a] : "Axis " + std::to_string(a);
        if (a == 1 || a == 3) return n.substr(0, n.size() - 2) + (plus ? " down" : " up");
        if (a == 0 || a == 2) return n.substr(0, n.size() - 2) + (plus ? " right" : " left");
        return n;
    }
    static const char *BN[] = { "A", "B", "X", "Y", "Back", "Guide", "Start", "Left stick press", "Right stick press",
                                "LB", "RB", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Misc", "Paddle 1",
                                "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad" };
    return code < (int)(sizeof BN / sizeof BN[0]) ? BN[code] : "Button " + std::to_string(code);
}

// how far an action is held now: 0..1 (an axis gives its share)
static float bind_held(int b, const Uint8 *k)
{
    float v = 0;
    if (BIND[b].key > 0 && k[BIND[b].key]) v = 1;
    int c = BIND[b].pad;
    if (g_pad && c >= 0) {
        if (c >= PAD_AXIS) {
            int a = (c - PAD_AXIS) / 2, plus = (c - PAD_AXIS) % 2;
            int x = SDL_GameControllerGetAxis(g_pad, (SDL_GameControllerAxis)a);
            float f = (plus ? x : -x) / 32767.0f;
            if (f > 0.25f && f > v) v = f > 1 ? 1 : f;
        } else if (SDL_GameControllerGetButton(g_pad, (SDL_GameControllerButton)c)) v = 1;
    }
    return v;
}

// pressed this frame (once), or repeated while held (frame steps)
static bool bind_pressed(int b, const Uint8 *k, double now, bool repeat)
{
    static bool was[B_COUNT];
    static double since[B_COUNT], last[B_COUNT];
    bool on = bind_held(b, k) > 0.5f, fire = false;
    if (on && !was[b]) { fire = true; since[b] = last[b] = now; }
    else if (on && repeat && now - since[b] > 0.4 && now - last[b] > 0.06) { fire = true; last[b] = now; }
    was[b] = on;
    return fire;
}

static std::string studio_ini() { return std::string(vp_package_dir()) + "/studio.ini"; }

static void studio_load()
{
    binds_defaults_keep();
    const char *home = getenv("USERPROFILE");
    if (home) snprintf(S.key, sizeof S.key, "%s\\.ssh\\id_ed25519_vectorpie", home);
    FILE *f = fopen(studio_ini().c_str(), "r");
    if (!f) return;
    char line[700];
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '=');
        if (!e) continue;
        *e++ = 0;
        char *n = strchr(e, '\n'); if (n) *n = 0;
        char *r = strchr(e, '\r'); if (r) *r = 0;
        if (!strcmp(line, "host")) snprintf(S.host, sizeof S.host, "%s", e);
        else if (!strcmp(line, "user")) snprintf(S.user, sizeof S.user, "%s", e);
        else if (!strcmp(line, "key")) snprintf(S.key, sizeof S.key, "%s", e);
        else if (!strcmp(line, "glow")) S.monitor_look = atoi(e);
        else if (!strcmp(line, "show_free")) S.show_free = atoi(e);
        else if (!strcmp(line, "cabinet_look")) S.cabinet_look = atoi(e);
        else for (int i = 0; i < B_COUNT; i++) {
            if (!strncmp(line, "key_", 4) && !strcmp(line + 4, BIND[i].id)) BIND[i].key = atoi(e);
            if (!strncmp(line, "pad_", 4) && !strcmp(line + 4, BIND[i].id)) BIND[i].pad = atoi(e);
        }
    }
    fclose(f);
}

static void studio_save()
{
    FILE *f = fopen(studio_ini().c_str(), "w");
    if (!f) return;
    fprintf(f, "host=%s\nuser=%s\nkey=%s\nglow=%d\nshow_free=%d\ncabinet_look=%d\n",
            S.host, S.user, S.key, S.monitor_look, S.show_free, S.cabinet_look);
    for (int i = 0; i < B_COUNT; i++) fprintf(f, "key_%s=%d\npad_%s=%d\n", BIND[i].id, BIND[i].key, BIND[i].id, BIND[i].pad);
    fclose(f);
}

// the settings a game runs with: all games' (the package's, the owner's) with the game's own on top;
// dir: where the owner's are (this PC's save/, or the cabinet's copy)
static void load_config(vx_config *c, const char *id, const std::string &dir)
{
    vx_config_defaults(c);
    vx_config_load(c, asset("vectrex.ini").c_str());
    vx_config_load(c, (dir + "/vectrex.ini").c_str());
    if (id && *id) vx_config_load(c, (dir + "/games/" + id + ".ini").c_str());
}

// ------------------------------------------------------------------------------------------------
// the cabinet: ssh.exe, run without a window; the password through SSH_ASKPASS (this program)

static const char *CAB_DIR = "/persistent/games/vectrex";

struct Job {                                    // one ssh run in the background
    std::thread th;
    std::atomic<int> state{0};                  // 0 idle, 1 running, 2 done
    int code = 0;
    std::string out, what;
};

static std::string g_password;                 // (only in memory, never written)

static int run_cmd(const std::string &cmd, const std::string *in, std::string *out);

static std::string openssh(const char *exe)     // (Windows' own OpenSSH, with the options both use)
{
    std::string p = std::string(getenv("SystemRoot") ? getenv("SystemRoot") : "C:\\Windows") + "\\System32\\OpenSSH\\" + exe;
    std::string c = "\"" + p + "\" -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new -o NumberOfPasswordPrompts=1 ";
    if (S.key[0] && file_there(S.key)) c += "-i \"" + std::string(S.key) + "\" ";
    return c;
}

// a command on the cabinet. (Its standard input is not used for files: Windows' ssh.exe sometimes cuts
// what is piped into it short; files go with run_scp.)
static int run_ssh(const std::string &remote, const std::string *in, std::string *out)
{
    return run_cmd(openssh("ssh.exe") + std::string(S.user) + "@" + S.host + " \"" + remote + "\"", in, out);
}

// files of this PC to a folder (or one file to a path) on the cabinet
static int run_scp(const std::vector<std::string> &files, const std::string &remote, std::string *out)
{
    std::string c = openssh("scp.exe") + "-q ";
    for (auto &f : files) c += "\"" + f + "\" ";
    c += std::string(S.user) + "@" + S.host + ":\"" + remote + "\"";
    return run_cmd(c, nullptr, out);
}

static std::string first_line(const std::string &o)
{
    size_t a = o.find_first_not_of(" \r\n");
    if (a == std::string::npos) return "no answer";
    size_t b = o.find_first_of("\r\n", a);
    return o.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

static std::string base64(const std::string &d)
{
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < d.size(); i += 3) {
        unsigned v = (unsigned char)d[i] << 16 | (i + 1 < d.size() ? (unsigned char)d[i + 1] << 8 : 0) | (i + 2 < d.size() ? (unsigned char)d[i + 2] : 0);
        o += T[v >> 18 & 63]; o += T[v >> 12 & 63];
        o += i + 1 < d.size() ? T[v >> 6 & 63] : '=';
        o += i + 2 < d.size() ? T[v & 63] : '=';
    }
    return o;
}

static std::string temp_file(const std::string &name, const std::string &data)   // (a file of this PC for scp)
{
    char t[MAX_PATH];
    GetTempPathA(sizeof t, t);
    std::string p = std::string(t) + "vx_studio_" + name;
    write_file(p, data);
    return p;
}

static int run_cmd(const std::string &cmd, const std::string *in, std::string *out)
{
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof self);
    SetEnvironmentVariableA("SSH_ASKPASS", self);
    SetEnvironmentVariableA("SSH_ASKPASS_REQUIRE", "force");
    SetEnvironmentVariableA("VX_STUDIO_ASKPASS", "1");
    SetEnvironmentVariableA("VX_STUDIO_PASS", g_password.c_str());
    SetEnvironmentVariableA("DISPLAY", "studio");

    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE out_r, out_w, in_r = nullptr, in_w = nullptr;
    CreatePipe(&out_r, &out_w, &sa, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&in_r, &in_w, &sa, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    si.hStdInput = in_r;
    PROCESS_INFORMATION pi = {};
    std::vector<char> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    BOOL ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableA("VX_STUDIO_PASS", nullptr);
    SetEnvironmentVariableA("VX_STUDIO_ASKPASS", nullptr);
    CloseHandle(out_w);
    CloseHandle(in_r);
    if (!ok) {
        CloseHandle(out_r); CloseHandle(in_w);
        if (out) *out = "Windows' ssh.exe could not be started (OpenSSH client missing?)";
        return -1;
    }
    std::thread feeder([&] {                    // (stdin and stdout at the same time: no dead lock)
        if (in) { DWORD w; const char *p = in->data(); size_t left = in->size();
                  while (left && WriteFile(in_w, p, (DWORD)(left > 65536 ? 65536 : left), &w, nullptr) && w) { p += w; left -= w; } }
        CloseHandle(in_w);
    });
    std::string o;
    char buf[8192];
    DWORD k;
    while (ReadFile(out_r, buf, sizeof buf, &k, nullptr) && k) o.append(buf, k);
    feeder.join();
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(out_r);
    if (out) *out = o;
    return (int)code;
}

struct CabGame { std::string path, file, id; uint32_t crc = 0; int profiles = 0; int setting = -1; bool here = false; bool original = false; };
struct CabOriginal { std::string file; std::vector<uint32_t> crc; };
struct Cabinet {
    bool connected = false;
    std::string status = "Not connected";
    std::vector<CabGame> games;
    std::vector<std::string> profiles;          // file names in its save/colors
    std::vector<CabOriginal> originals;         // its colors/ (the ORIGINAL profiles) with their checksums
};
static Cabinet C;
static Job J;
static std::mutex g_mx;

static void job_start(const std::string &what, std::function<void(Job &)> f);

// ------------------------------------------------------------------------------------------------
// the game

static vx_rom_list g_roms;
static std::string g_rom_dir;                   // (the settings' rom_dir: for reading the list again)
struct RomColours { bool original = false; int own = 0; std::string file; uint32_t crc = 0; };
static std::vector<RomColours> g_rom_colours;   // per game of g_roms: its ORIGINAL, how many own profiles
static std::string g_cart;                      // its file ("" none yet)
static char g_id[64] = "";
static std::string g_title = "No game";
static vx_config g_cfg;                         // the settings it runs with on this PC
static vx_config g_view;                        // the picture's settings for the preview (the cabinet's)
static bool g_view_cab = false;
static vx_colors *g_colors;                     // the profile being edited
static int g_setting = 1;                       // the profile shown (color_profile)
static int g_target = 0, g_target_new = 1;      // the own profile it is saved to
static bool g_dirty = false;
static bool g_running = false, g_paused = true, g_tagged = true;
static vx_coloredit *g_ed;                      // the editor's state (pieces, picks, rules being made)
static bool g_ed_ok = false;                    // the editor is cut for the frame shown
static std::vector<vx_colors> g_undo, g_redo;
static std::string g_msg;                       // the status line
static double g_msg_t = 0;

static void say(const std::string &m) { g_msg = m; g_msg_t = SDL_GetTicks() / 1000.0; }

// The pictures of the last frames (the core runs forwards only): Previous frame steps back through
// them, Next frame forwards and then on with the game. The colours' RAM conditions are the game's now.
static const size_t HIST_MAX = 250;            // (5 seconds)
static std::deque<std::vector<vx_line>> g_hist;
static int g_hist_pos = -1;                     // the frame shown: -1 the newest, else its place in g_hist
static long g_frame = 0;                        // frames run
static std::deque<std::vector<uint8_t>> g_ram;  // the game's RAM ($C800-$CBFF) at each kept frame (g_hist's)

// Colour by game part: a byte (with a mask) of the RAM that tells the game's parts apart (a level, a
// section of the course). Found by comparing two parts the user remembered; kept per game.
struct PartByte { int addr = -1, mask = 0xFF; };
static PartByte g_part;
static std::vector<std::vector<uint8_t>> g_partA, g_partB;   // the RAM of the frames remembered
struct PartCand { int addr, mask, va, vb; };
static std::vector<PartCand> g_part_cands;
static bool g_only_part = false;                // apply colours with "only in this part"
static int g_sel_rule = -1;                     // a rule picked in the list: a colour click changes it
static bool g_xr = false;                       // advanced: any vector list in a range
static char g_xr_lo[8] = "0000", g_xr_hi[8] = "00FF";
static bool g_ur = false;                       // advanced: any object (U) in a range
static char g_ur_lo[8] = "0000", g_ur_hi[8] = "FFFF";
static bool g_len = false;                      // advanced: only lines of a length
static float g_len_lo = 3.0f, g_len_hi = 100.0f;   // (% of the picture's height)
static bool g_area = false;                     // advanced: only where the picked lines are now
static bool g_len_picked = true;                // the length range from the picked lines (else the numbers)
static bool g_demo_adv = false;
static int g_demo_tab = -1;                     // (development: --demo-connect [cabinet]: connected at the start, a tab shown)                 // (development: --demo-len / --demo-area open Advanced)

static int cur_lines(const vx_line **l)        // the picture shown
{
    if (g_hist_pos >= 0 && g_hist_pos < (int)g_hist.size()) { *l = g_hist[g_hist_pos].data(); return (int)g_hist[g_hist_pos].size(); }
    return vx_lines(l);
}

static void run_frame(const vx_pad *p1, const vx_pad *p2)   // one frame of the game, kept for stepping back
{
    vx_set_input(p1, p2);
    vx_run_frame();
    vx_colors_frame(g_colors);
    g_frame++;
    const vx_line *l;
    int n = vx_lines(&l);
    g_hist.emplace_back(l, l + n);
    std::vector<uint8_t> ram(0x400);
    for (int i = 0; i < 0x400; i++) ram[i] = vx_peek_ram((uint16_t)(0xC800 + i));
    g_ram.push_back(std::move(ram));
    if (g_hist.size() > HIST_MAX) { g_hist.pop_front(); g_ram.pop_front(); }
    g_hist_pos = -1;
}

// the RAM of the frame shown (a kept one, or the game's now)
static uint8_t shown_ram(int addr)
{
    if (g_hist_pos >= 0 && g_hist_pos < (int)g_ram.size()) return g_ram[g_hist_pos][addr - 0xC800];
    return vx_peek_ram((uint16_t)addr);
}

// the part byte per game: studio_parts.ini next to the program ("id=ADDR,MASK", hex)
static std::string parts_ini() { return std::string(vp_package_dir()) + "/studio_parts.ini"; }
static void part_load()
{
    g_part = PartByte();
    FILE *f = fopen(parts_ini().c_str(), "r");
    if (!f) return;
    char ln[256];
    size_t L = strlen(g_id);
    while (fgets(ln, sizeof ln, f))
        if (!strncmp(ln, g_id, L) && ln[L] == '=') {
            unsigned a = 0, m = 0xFF;
            if (sscanf(ln + L + 1, "%x,%x", &a, &m) >= 1) { g_part.addr = (int)a; g_part.mask = (int)m; }
        }
    fclose(f);
}
static void part_save()
{
    std::string keep, mine = std::string(g_id) + "=";
    FILE *f = fopen(parts_ini().c_str(), "r");
    if (f) { char ln[256]; while (fgets(ln, sizeof ln, f)) if (strncmp(ln, mine.c_str(), mine.size())) keep += ln; fclose(f); }
    f = fopen(parts_ini().c_str(), "w");
    if (!f) return;
    fputs(keep.c_str(), f);
    if (g_part.addr >= 0) fprintf(f, "%s=%04X,%02X\n", g_id, (unsigned)g_part.addr, (unsigned)g_part.mask);
    fclose(f);
}

// the last second (up to 50 kept frames) up to the one shown
static std::vector<std::vector<uint8_t>> remember_part()
{
    std::vector<std::vector<uint8_t>> r;
    int end = g_hist_pos >= 0 ? g_hist_pos : (int)g_ram.size() - 1;
    for (int i = end; i >= 0 && end - i < 50; i--) r.push_back(g_ram[i]);
    return r;
}

// the bytes (with a mask) that stay the same in each part remembered and differ between them
static void part_candidates()
{
    g_part_cands.clear();
    static const int MASKS[] = { 0xFF, 0xF0, 0xE0, 0xC0, 0x80 };
    for (int a = 0; a < 0x400; a++) {
        for (int m : MASKS) {
            auto same = [&](const std::vector<std::vector<uint8_t>> &fr, int *v) {
                if (fr.empty()) return false;
                *v = fr[0][a] & m;
                for (auto &x : fr) if ((x[a] & m) != *v) return false;
                return true;
            };
            int va, vb;
            if (same(g_partA, &va) && same(g_partB, &vb) && va != vb) { g_part_cands.push_back({ 0xC800 + a, m, va, vb }); break; }
        }
    }
    // the likeliest first: a whole byte before a masked one, small values (a level's number) before large
    auto score = [](const PartCand &c) { return (c.mask == 0xFF ? 0 : 1000) + std::max(c.va, c.vb); };
    std::stable_sort(g_part_cands.begin(), g_part_cands.end(), [&](const PartCand &x, const PartCand &y) { return score(x) < score(y); });
}

static long shown_frame() { return g_hist_pos < 0 ? g_frame : g_frame - (long)(g_hist.size() - 1 - (size_t)g_hist_pos); }

static std::string save_dir() { return vp_save_dir(); }

static void pick_view_settings()
{
    std::string cab = std::string(vp_package_dir()) + "/cabinet";
    g_view_cab = S.cabinet_look && file_there(cab + "/vectrex.ini");
    load_config(&g_view, g_id, g_view_cab ? cab : save_dir());
}

static void profile_target() { g_target = vx_coloredit_target(g_id, g_setting, 0, &g_target_new); }

static void load_profile(int setting)
{
    g_setting = setting;
    vx_colors_load_setting(g_colors, g_id, setting);
    profile_target();
    g_undo.clear(); g_redo.clear();
    g_dirty = false;
}

static void undo_mark()
{
    g_undo.push_back(*g_colors);
    if (g_undo.size() > 60) g_undo.erase(g_undo.begin());
    g_redo.clear();
}

static bool same_name(const std::string &a, const std::string &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return tolower((unsigned char)x) == tolower((unsigned char)y); });
}
static bool same_game(int rom, const CabGame &cg)
{
    uint32_t c = rom < (int)g_rom_colours.size() ? g_rom_colours[(size_t)rom].crc : 0;
    if (c && cg.crc) return c == cg.crc;
    return same_name(base_name(g_roms.rom[rom].path), cg.file);
}
static int pc_index_of(const CabGame &cg)        // (-1: only on the cabinet)
{
    for (int i = 0; i < g_roms.n; i++) if (same_game(i, cg)) return i;
    return -1;
}
static bool on_cabinet(int rom)                  // (with the cabinet's list held: g_mx)
{
    for (auto &cg : C.games) if (same_game(rom, cg)) return true;
    return false;
}

// which games of this PC have colour profiles (ORIGINAL by checksum or name, own ones): for the list
static void rom_colours()
{
    g_rom_colours.assign((size_t)g_roms.n, RomColours());
    char id[64], p[1100];
    for (int i = 0; i < g_roms.n; i++) {
        vx_game_id(g_roms.rom[i].path, id, sizeof id);
        vx_colors_game(g_roms.rom[i].path);
        RomColours &r = g_rom_colours[(size_t)i];
        r.crc = vx_colors_game_crc();
        if (vx_colors_profile_path(id, 0, p, sizeof p) == 0) { r.original = true; r.file = base_name(p); }
        r.own = vx_colors_own_count(id);
    }
    vx_colors_game(g_cart.empty() ? nullptr : g_cart.c_str());   // (back to the game shown)
}

// a small mark before a game's name: filled, it has an ORIGINAL profile; a ring, only own ones
static void colour_mark(bool original, int own, const char *where = nullptr, ImU32 where_col = 0)
{
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImVec2 c = ImVec2(a.x + 8, (a.y + b.y) * 0.5f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (original) dl->AddCircleFilled(c, 4.5f, IM_COL32(90, 220, 120, 255));
    else if (own) dl->AddCircle(c, 4.5f, IM_COL32(90, 220, 120, 255), 0, 1.6f);
    float right = b.x - 4;
    if (where) {                                // (where the game is: both, PC only, cabinet only)
        ImVec2 sz = ImGui::CalcTextSize(where);
        dl->AddText(ImVec2(right - sz.x, a.y + (b.y - a.y - sz.y) * 0.5f), where_col, where);
        right -= sz.x + 10;
    }
    if (own) {
        std::string t = "+" + std::to_string(own);
        ImVec2 sz = ImGui::CalcTextSize(t.c_str());
        dl->AddText(ImVec2(right - sz.x, a.y + (b.y - a.y - sz.y) * 0.5f), IM_COL32(150, 200, 160, 255), t.c_str());
    }
}

static bool save_profile()
{
    if (!g_cart.size()) return false;
    std::string d = save_dir();
    CreateDirectoryA(d.c_str(), nullptr);
    CreateDirectoryA((d + "/colors").c_str(), nullptr);
    char p[1100];
    vx_colors_profile_path(g_id, g_target, p, sizeof p);
    if (vx_colors_save(g_colors, p, g_title.c_str()) != 0) { say("Could not write " + std::string(p)); return false; }
    // the game shows it from now on (its own setting, as the emulator's editor does)
    vx_config all, game;
    load_config(&all, nullptr, d);
    load_config(&game, g_id, d);
    game.color_profile = g_target + 1;
    CreateDirectoryA((d + "/games").c_str(), nullptr);
    vx_config_save_diff(&game, &all, (d + "/games/" + g_id + ".ini").c_str());
    g_setting = g_target + 1;
    profile_target();
    g_dirty = false;
    say("Saved as PROFILE " + std::to_string(g_setting - 1) + "  (" + base_name(p) + ")");
    rom_colours();
    return true;
}

// the editor cut for the frame now shown (paused): the pieces; WHAT, the colour and the brightness kept
static void editor_cut()
{
    const vx_line *l;
    int n = cur_lines(&l);
    int scope = g_ed->scope, color = g_ed->color;
    float gain = g_ed->gain;
    bool had = g_ed->open;
    if (vx_coloredit_open(g_ed, l, n) != 0) { g_ed_ok = false; g_tagged = false; return; }
    g_tagged = true;
    if (had) { g_ed->scope = scope; g_ed->color = color; g_ed->gain = gain; }
    else g_ed->scope = VX_CE_KIND;              // (the studio starts with SAME KIND: click, colour, done)
    g_ed->panel = 1;                            // (the studio's tools are always there)
    g_ed->ncand = 0;
    g_ed_ok = true;
}

static int g_leave = 0;                         // asked: 1 quit, 2 another game (g_leave_cart)
static std::string g_leave_cart;
static bool g_quit = false;

static void restart_with(const std::string &cart);
// leave for another game ("" quit): with unsaved changes the "Save changes?" window asks first
static void leave_to(const std::string &cart)
{
    if (g_dirty) { g_leave = cart.empty() ? 1 : 2; g_leave_cart = cart; return; }
    if (cart.empty()) g_quit = true; else restart_with(cart);
}

// one frame on (the frames kept first, then the game runs one more) or back (d -1)
static void step_frame(int d)
{
    vx_pad p1 = { 0, 0, 0 }, p2 = { 0, 0, 0 };
    if (d < 0) {
        if (g_hist.size() < 2) { say("No frame before this one is kept"); return; }
        if (g_hist_pos < 0) g_hist_pos = (int)g_hist.size() - 2;
        else if (g_hist_pos > 0) g_hist_pos--;
        else { say("The oldest frame kept (250 frames back at most)"); return; }
    } else if (g_hist_pos >= 0) {
        if (++g_hist_pos >= (int)g_hist.size() - 1) g_hist_pos = -1;
    } else run_frame(&p1, &p2);
    editor_cut();
}

static void restart_with(const std::string &cart)
{
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, sizeof self);
    std::string cl = "\"" + std::string(self) + "\" --game \"" + cart + "\"";
    std::vector<char> b(cl.begin(), cl.end()); b.push_back(0);
    STARTUPINFOA si = {}; si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(nullptr, b.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        exit(0);
    }
    say("Could not start the game");
}

// ------------------------------------------------------------------------------------------------
// the cabinet's jobs

static void job_start(const std::string &what, std::function<void(Job &)> f)
{
    if (J.state == 1) return;
    if (J.th.joinable()) J.th.join();
    J.what = what;
    J.state = 1;
    J.th = std::thread([f] { f(J); J.state = 2; });
}

static const char *LIST_GAMES =
    "if command -v python3 >/dev/null; then python3 - <<'VX_END'\n"
    "import os, zlib\n"
    "for d in ('/usr/local/share/advance/image/vectrex', '/persistent/games/rom/vectrex'):\n"
    "    if not os.path.isdir(d): continue\n"
    "    for f in sorted(os.listdir(d)):\n"
    "        p = os.path.join(d, f)\n"
    "        if os.path.isfile(p) and not f.startswith('.') and os.path.splitext(f)[1].lower() in ('.bin', '.vec', '.gam', '.rom', '.um2', '.pit'):\n"   /* (cartridges only, as roms.c) */
    "            b = open(p, 'rb').read(4 << 20).rstrip(b'\\xff\\x00')\n"   /* (as vx_cart_crc: without the padding) */
    "            print('G %08X %s' % ((zlib.crc32(b) & 0xffffffff) if b else 0, p))\n"
    "VX_END\n"
    "else for d in /usr/local/share/advance/image/vectrex /persistent/games/rom/vectrex; do "
    "[ -d $d ] && for f in $d/*.bin $d/*.BIN $d/*.vec $d/*.VEC $d/*.gam $d/*.rom $d/*.um2 $d/*.pit; do [ -f \"$f\" ] && echo \"G 00000000 $f\"; done; done; fi\n";
static const char *LIST_CMD =
    "for f in /persistent/games/vectrex/save/colors/*.ini; do [ -f $f ] && echo P ${f##*/}; done; "
    "for f in /persistent/games/vectrex/colors/*.ini; do [ -f $f ] && echo O ${f##*/} $(sed -n 's/^crc *= *//p' $f | tr ',' ' '); done; "
    "for f in /persistent/games/vectrex/save/games/*.ini; do [ -f $f ] && echo S ${f##*/} $(sed -n 's/^color_profile *= *//p' $f); done; "
    "echo OK";

static void parse_list(const std::string &o)
{
    std::lock_guard<std::mutex> g(g_mx);
    C.games.clear(); C.profiles.clear(); C.originals.clear();
    std::vector<std::pair<std::string, int>> settings;
    size_t a = 0;
    while (a < o.size()) {
        size_t b = o.find('\n', a);
        std::string ln = o.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        a = b == std::string::npos ? o.size() : b + 1;
        if (ln.size() < 3) continue;
        std::string v = ln.substr(2);
        if (ln[0] == 'G') {
            CabGame g;
            g.crc = (uint32_t)strtoul(v.c_str(), nullptr, 16);
            g.path = v.size() > 9 ? v.substr(9) : v; g.file = base_name(g.path);
            char id[64]; vx_game_id(g.file.c_str(), id, sizeof id); g.id = id;
            for (auto &x : C.games) if (x.id == g.id) { x = g; goto next; }   // (the later folder wins, as there)
            C.games.push_back(g);
        } else if (ln[0] == 'P') C.profiles.push_back(v);
        else if (ln[0] == 'O') {
            CabOriginal o;
            size_t sp = v.find(' ');
            o.file = v.substr(0, sp);
            for (size_t q = sp; q != std::string::npos && q < v.size();) {
                while (q < v.size() && v[q] == ' ') q++;
                if (q >= v.size()) break;
                o.crc.push_back((uint32_t)strtoul(v.c_str() + q, nullptr, 16));
                q = v.find(' ', q);
            }
            C.originals.push_back(o);
        }
        else if (ln[0] == 'S') {
            size_t sp = v.find(' ');
            std::string f = v.substr(0, sp);
            int s = sp == std::string::npos ? -1 : atoi(v.c_str() + sp + 1);
            if (f.size() > 4) settings.push_back({ f.substr(0, f.size() - 4), s });
        }
        next:;
    }
    for (auto &g : C.games) {
        for (auto &p : C.profiles) if (p == g.id + ".ini" || (p.rfind(g.id + ".", 0) == 0 && p.size() > g.id.size() + 5 &&
                                                              isdigit((unsigned char)p[g.id.size() + 1]))) g.profiles++;
        for (auto &s : settings) if (s.first == g.id) g.setting = s.second;
        uint32_t crc = g.crc;
        for (int i = 0; i < g_roms.n; i++) if (base_name(g_roms.rom[i].path) == g.file) { g.here = true; if (!crc) crc = vx_cart_crc(g_roms.rom[i].path); }
        for (auto &o : C.originals) {           // (as the emulator finds ORIGINAL: by the checksum, else by the name)
            if (o.file == g.id + ".ini") g.original = true;
            for (uint32_t c : o.crc) if (crc && c == crc) g.original = true;
        }
    }
}

// the cabinet's list: games (with checksums), profiles, settings
static int list_cabinet(std::string *o)
{
    std::string sc = std::string(LIST_GAMES) + LIST_CMD + "\n";
    return run_ssh("echo " + base64(sc) + " | base64 -d | sh", nullptr, o);
}

static void cabinet_connect()
{
    job_start("Connecting", [](Job &j) {
        std::string o;
        j.code = list_cabinet(&o);
        j.out = o;
        if (j.code == 0 && o.find("OK") != std::string::npos) {
            parse_list(o);
            // its picture settings, for the preview
            std::string cab = std::string(vp_package_dir()) + "/cabinet";
            CreateDirectoryA(cab.c_str(), nullptr);
            CreateDirectoryA((cab + "/games").c_str(), nullptr);
            std::string v;
            if (run_ssh(std::string("cat ") + CAB_DIR + "/save/vectrex.ini 2>/dev/null; true", nullptr, &v) == 0)
                write_file(cab + "/vectrex.ini", v.empty() ? std::string("; (the cabinet has its defaults)\n") : v);
            std::string tarball;
            if (run_ssh(std::string("cd ") + CAB_DIR + "/save/games 2>/dev/null && for f in *.ini; do [ -f $f ] && echo \\\"== $f\\\" && cat $f; done; true",
                        nullptr, &tarball) == 0) {
                // "== name" then its lines
                size_t a = 0; FILE *f = nullptr;
                while (a < tarball.size()) {
                    size_t b = tarball.find('\n', a);
                    std::string ln = tarball.substr(a, b == std::string::npos ? std::string::npos : b - a + 1);
                    a = b == std::string::npos ? tarball.size() : b + 1;
                    if (ln.rfind("== ", 0) == 0) {
                        if (f) fclose(f);
                        std::string nm = ln.substr(3); while (!nm.empty() && (nm.back() == '\n' || nm.back() == '\r')) nm.pop_back();
                        f = fopen((cab + "/games/" + nm).c_str(), "wb");
                    } else if (f) fwrite(ln.data(), 1, ln.size(), f);
                }
                if (f) fclose(f);
            }
            std::lock_guard<std::mutex> g(g_mx);
            C.connected = true;
            C.status = "Connected to " + std::string(S.user) + "@" + S.host + ": " + std::to_string(C.games.size()) + " games";
        } else {
            std::lock_guard<std::mutex> g(g_mx);
            C.connected = false;
            std::string why = o.find("Permission denied") != std::string::npos ? "wrong user name or password"
                            : o.find("timed out") != std::string::npos || o.find("Connection refused") != std::string::npos ||
                              o.find("No route") != std::string::npos ? "the cabinet does not answer (is it on?)" : "see the details";
            C.status = "Not connected: " + why;
        }
    });
}

// fetch a game that is only on the cabinet: its cartridge into roms/, its profiles (those this PC lacks)
static void cabinet_fetch(const CabGame &g)
{
    CabGame cg = g;
    job_start("Fetching " + cg.file, [cg](Job &j) {
        std::string data;
        j.code = run_ssh("cat '" + cg.path + "'", nullptr, &data);
        if (j.code != 0 || data.empty()) { j.out = "Could not fetch " + cg.file; return; }
        std::string roms = std::string(vp_package_dir()) + "/roms";
        CreateDirectoryA(roms.c_str(), nullptr);
        write_file(roms + "/" + cg.file, data);
        std::string d = save_dir() + "/colors";
        CreateDirectoryA(save_dir().c_str(), nullptr);
        CreateDirectoryA(d.c_str(), nullptr);
        std::vector<std::string> mine;
        { std::lock_guard<std::mutex> lk(g_mx); for (auto &p : C.profiles) if (p == cg.id + ".ini" || p.rfind(cg.id + ".", 0) == 0) mine.push_back(p); }
        int got = 0;
        for (auto &p : mine) {
            if (file_there(d + "/" + p)) continue;          // (the PC's own is kept)
            std::string t;
            if (run_ssh(std::string("cat ") + CAB_DIR + "/save/colors/" + p, nullptr, &t) == 0) { write_file(d + "/" + p, t); got++; }
        }
        j.out = "FETCHED " + roms + "/" + cg.file;
        (void)got;
    });
}

// send the profile (saved first) to the cabinet; use: choose it there for the game
static void cabinet_send(bool use)
{
    if (g_dirty && !save_profile()) return;
    char p[1100];
    if (g_setting < 2 || vx_colors_profile_path(g_id, g_setting - 1, p, sizeof p) != 0) { say("Colour something and save it first: ORIGINAL is the cabinet's already"); return; }
    std::string file = base_name(p), data = read_file(p), id = g_id;
    int setting = g_setting;
    std::string local = p;
    job_start("Sending " + file, [file, data, id, setting, use, local](Job &j) {
        std::string d = std::string(CAB_DIR) + "/save/colors", o0;
        if (run_ssh("mkdir -p " + d + " && rm -f " + d + "/.incoming", nullptr, &o0) != 0 ||
            run_scp({ local }, d + "/.incoming", &o0) != 0) { j.code = 1; j.out = "not sent: " + first_line(o0); return; }
        std::string r = "d=" + d + "; f=" + file + "; s=old-$(date +%Y%m%d-%H%M%S); "
                        "[ \"$(stat -c %s $d/.incoming 2>/dev/null)\" = " + std::to_string(data.size()) + " ] && "
                        "if [ -f $d/$f ] && ! cmp -s $d/.incoming $d/$f; then mkdir -p $d/$s && cp -p $d/$f $d/$s/; fi; "
                        "mv -f $d/.incoming $d/$f";
        if (use) r += "; g=" + std::string(CAB_DIR) + "/save/games/" + id + ".ini; mkdir -p " + CAB_DIR + "/save/games; "
                      "if grep -q '^color_profile' $g 2>/dev/null; then sed -i 's/^color_profile.*/color_profile = " +
                      std::to_string(setting) + "/' $g; else echo 'color_profile = " + std::to_string(setting) + "' >> $g; fi";
        r += " && echo SENT; rm -f $d/.incoming";
        std::string o;
        j.code = run_ssh(r, nullptr, &o);
        j.out = o.find("SENT") != std::string::npos ? o : "not sent completely: " + first_line(o);
    });
}

// copy games: this PC's cartridges into the cabinet's /persistent/games/rom/vectrex, the cabinet's into
// this PC's roms/ (with the cabinet's profiles this PC lacks); a file of the same name there stays
static const char *CAB_ROMS = "/persistent/games/rom/vectrex";

static bool plain_name(const std::string &n) { return !n.empty() && n.find_first_of("'\"$`\\/") == std::string::npos; }

static void copy_to_cabinet(const std::vector<std::string> &paths)
{
    if (paths.empty()) { say("Nothing to copy: the cabinet has these games"); return; }
    job_start("Copying to the cabinet", [paths](Job &j) {
        int done = 0, there = 0, failed = 0;
        std::string why;
        const std::string d = CAB_ROMS, in = std::string(CAB_ROMS) + "/.vx_incoming";
        std::vector<std::string> ok;
        for (auto &p : paths) {
            if (plain_name(base_name(p)) && file_there(p)) ok.push_back(p);
            else { failed++; why = "a file name the copy cannot take: " + base_name(p); }
        }
        for (size_t k = 0; k < ok.size(); k += 30) {           // (30 at a time: the command line's length)
            std::vector<std::string> part(ok.begin() + (long)k, ok.begin() + (long)std::min(ok.size(), k + 30));
            std::string o;
            // (an older version's half-copied ".incoming" goes too)
            if (run_ssh("mkdir -p " + d + " && rm -f " + d + "/.incoming && rm -rf " + in + " && mkdir " + in, nullptr, &o) != 0) {
                failed += (int)part.size(); why = first_line(o); continue;
            }
            if (run_scp(part, in + "/", &o) != 0) why = "scp: " + first_line(o);   // (some may have come: checked below)
            std::string r = "d=" + d + "; i=" + in + "; ";
            for (auto &p : part) {
                std::string n = base_name(p);
                r += "if [ \"$(stat -c %s \\\"$i/" + n + "\\\" 2>/dev/null)\" != " + std::to_string(read_file(p).size()) + " ]; then echo FAILED; "
                     "elif [ -e \\\"$d/" + n + "\\\" ]; then echo EXISTS; "
                     "else mv \\\"$i/" + n + "\\\" \\\"$d/" + n + "\\\" && echo COPIED; fi; ";
            }
            r += "rm -rf $i";
            run_ssh(r, nullptr, &o);
            int c = 0, e = 0, f = 0;
            for (size_t a2 = 0; (a2 = o.find_first_of("CEF", a2)) != std::string::npos; a2++) {
                if (!o.compare(a2, 6, "COPIED")) c++; else if (!o.compare(a2, 6, "EXISTS")) e++; else if (!o.compare(a2, 6, "FAILED")) f++;
            }
            done += c; there += e;
            failed += (int)part.size() - c - e;
            if (f && why.empty()) why = "incomplete on the cabinet";
        }
        std::string l;
        if (list_cabinet(&l) == 0) parse_list(l);
        j.out = "COPIED " + std::to_string(done) + " game(s) to the cabinet" + (there ? ", " + std::to_string(there) + " there already" : "") +
                (failed ? ", " + std::to_string(failed) + " failed (" + why + ")" : "") + ". The cabinet's game list shows them at its next start.";
    });
}

static void copy_to_pc(const std::vector<CabGame> &games)
{
    if (games.empty()) { say("Nothing to copy: this PC has these games"); return; }
    std::vector<std::string> profs;
    { std::lock_guard<std::mutex> lk(g_mx); profs = C.profiles; }
    job_start("Copying to this PC", [games, profs](Job &j) {
        std::string roms = std::string(vp_package_dir()) + "/roms", d = save_dir() + "/colors";
        CreateDirectoryA(roms.c_str(), nullptr);
        CreateDirectoryA(save_dir().c_str(), nullptr);
        CreateDirectoryA(d.c_str(), nullptr);
        int done = 0, there = 0, failed = 0, got = 0;
        for (auto &cg : games) {
            if (!plain_name(cg.file) || cg.path.find('\'') != std::string::npos) { failed++; continue; }
            if (file_there(roms + "/" + cg.file)) { there++; continue; }
            std::string data;
            if (run_ssh("cat '" + cg.path + "'", nullptr, &data) != 0 || data.empty()) { failed++; continue; }
            write_file(roms + "/" + cg.file, data);
            done++;
            for (auto &p : profs) {             // (its own profiles from the cabinet, those this PC lacks)
                if (!(p == cg.id + ".ini" || p.rfind(cg.id + ".", 0) == 0) || file_there(d + "/" + p)) continue;
                std::string t;
                if (run_ssh(std::string("cat ") + CAB_DIR + "/save/colors/" + p, nullptr, &t) == 0) { write_file(d + "/" + p, t); got++; }
            }
        }
        j.out = "COPIED " + std::to_string(done) + " game(s) to this PC" + (got ? " with " + std::to_string(got) + " own profile(s)" : "") +
                (there ? ", " + std::to_string(there) + " there already" : "") + (failed ? ", " + std::to_string(failed) + " failed" : "");
    });
}

// put this PC's colour profiles on the cabinet: every ORIGINAL (colors/: a changed one there is kept in
// colors/old-DATE/ first), and the own ones (save/colors/) the cabinet has not got (one there with the
// same name and other rules stays: it may be the owner's work on the cabinet)
static bool g_show_sync = false;

static std::string sync_script(int *nfiles)
{
    std::string script = "D=" + std::string(CAB_DIR) + "; s=old-$(date +%Y%m%d-%H%M%S); mkdir -p $D/colors $D/save/colors\n"
        "o() { if [ -f \"$D/colors/$1\" ] && cmp -s $D/.in \"$D/colors/$1\"; then echo SAME $1; else "
        "if [ -f \"$D/colors/$1\" ]; then mkdir -p $D/colors/$s && cp -p \"$D/colors/$1\" $D/colors/$s/; fi; "
        "cp $D/.in \"$D/colors/$1\"; echo PUT $1; fi; rm -f $D/.in; }\n"
        "p() { if [ -f \"$D/save/colors/$1\" ]; then if cmp -s $D/.in \"$D/save/colors/$1\"; then echo SAME $1; else echo KEPT $1; fi; "
        "else cp $D/.in \"$D/save/colors/$1\"; echo PUT $1; fi; rm -f $D/.in; }\n";
    int files = 0;
    for (int pass = 0; pass < 2; pass++) {
        std::string dir = pass ? save_dir() + "/colors" : std::string(vp_package_dir()) + "/colors";
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "/*.ini").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string name = fd.cFileName, data = read_file(dir + "/" + name);
            if (data.empty() || name.find_first_of(" '\"$`\\") != std::string::npos) continue;
            if (data.back() != '\n') data += '\n';
            script += "cat > $D/.in <<'VX_END_OF_PROFILE'\n" + data + "VX_END_OF_PROFILE\n" + (pass ? "p " : "o ") + name + "\n";
            files++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    script += "echo SYNCED\n";
    *nfiles = files;
    return script;
}

static void cabinet_sync()
{
    int files = 0;
    std::string script = sync_script(&files);
    if (!files) { say("No colour profiles on this PC"); return; }
    std::string local = temp_file("sync.sh", script);
    size_t size = script.size();
    job_start("Syncing colour profiles", [local, size](Job &j) {
        std::string o;
        if (run_scp({ local }, "/tmp/vx_studio_sync.sh", &o) != 0) { DeleteFileA(local.c_str()); j.code = 1; j.out = "SYNC FAILED scp: " + first_line(o); return; }
        DeleteFileA(local.c_str());
        j.code = run_ssh("f=/tmp/vx_studio_sync.sh; if [ \"$(stat -c %s $f)\" = " + std::to_string(size) + " ]; then sh $f; else echo INCOMPLETE; fi; rm -f $f", nullptr, &o);
        int put = 0, same = 0, kept = 0;
        std::string kept_names;
        size_t a = 0;
        while ((a = o.find_first_of("PSK", a)) != std::string::npos) {
            if (!o.compare(a, 4, "PUT ")) put++;
            else if (!o.compare(a, 5, "SAME ")) same++;
            else if (!o.compare(a, 5, "KEPT ")) { kept++; size_t e = o.find('\n', a); kept_names += " " + o.substr(a + 5, e == std::string::npos ? std::string::npos : e - a - 5); }
            size_t e = o.find('\n', a);
            a = e == std::string::npos ? o.size() : e + 1;
        }
        if (o.find("SYNCED") == std::string::npos) { j.out = "SYNC FAILED " + o.substr(0, 200); return; }
        std::string m = "SYNC " + std::to_string(put) + " sent, " + std::to_string(same) + " the same there";
        if (kept) m += ", " + std::to_string(kept) + " kept as they are there (other rules):" + kept_names;
        std::string l;                          // (the list again: the marks)
        if (list_cabinet(&l) == 0) parse_list(l);
        j.out = m;
    });
}

static void sync_window()
{
    if (!g_show_sync) return;
    ImGui::OpenPopup("Sync colour profiles");
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28, 0));
    if (ImGui::BeginPopupModal("Sync colour profiles", &g_show_sync)) {
        ImGui::TextWrapped("Puts this PC's colour profiles on the cabinet:");
        ImGui::BulletText("Every ORIGINAL profile. One that is different there is kept in colors/old-DATE first.");
        ImGui::BulletText("Your own profiles that the cabinet has not got. Own profiles there with other rules stay as they are.");
        ImGui::TextWrapped("The cabinet's emulator takes them when a game starts next.");
        bool busy = J.state == 1;
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button(busy ? "Syncing..." : "Sync now", ImVec2(140, 0))) { cabinet_sync(); g_show_sync = false; }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) g_show_sync = false;
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------------------------------------
// the picture

struct View {                                   // the board (0..4095, y up) on the screen
    ImVec2 o, size;                             // the board's square stretched to the tube, zoomed
    float zoom = 1.0f;
    ImVec2 pan = { 0, 0 };
    ImVec2 to(float bx, float by) const { return ImVec2(o.x + bx / 4096.0f * size.x, o.y + (4095.0f - by) / 4096.0f * size.y); }
    void from(ImVec2 s, int *bx, int *by) const {
        *bx = (int)((s.x - o.x) / size.x * 4096.0f);
        *by = 4095 - (int)((s.y - o.y) / size.y * 4096.0f);
    }
};
static View V;

static ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

static int line_at(const vx_line *l, int n, ImVec2 m, float max_px)
{
    int best = -1;
    float bd = max_px * max_px;
    for (int i = 0; i < n; i++) {
        if (!l[i].z) continue;
        float x0, y0, x1, y1;
        vx_map(&g_cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
        vx_map(&g_cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
        ImVec2 a = V.to(x0, y0), b = V.to(x1, y1);
        float dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
        float t = len > 0 ? ((m.x - a.x) * dx + (m.y - a.y) * dy) / len : 0.0f;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float ex = a.x + t * dx - m.x, ey = a.y + t * dy - m.y, d = ex * ex + ey * ey;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int g_hover_rule = -1;                   // the rule under the mouse in the rule list

static bool any_match(const vx_rule *c, int nc, const vx_line *l);
static int preview_cands(const vx_rule **out);

static void draw_picture(ImDrawList *dl, const vx_line *l, int n, double now)
{
    (void)now;
    bool edit = g_paused && g_ed_ok;
    // the tube's outline
    dl->AddRect(V.to(0, 4095), V.to(4095, 0), col(40, 50, 70));
    // what stands out: the rule under the mouse, else the lines picked and what a colour click would change
    // (with the options' conditions); the rest is dimmed while there is any
    static std::vector<unsigned char> mark;
    mark.assign((size_t)n, 0);
    bool any = false;
    if (edit) {
        const vx_rule *pv; int npv = preview_cands(&pv);
        bool hov_on = g_hover_rule >= 0 && g_hover_rule < g_colors->n;
        for (int i = 0; i < n; i++) {
            if (!l[i].z) continue;
            bool m = hov_on ? vx_rule_matches(&g_colors->rule[g_hover_rule], &l[i])
                            : (npv > 0 && any_match(pv, npv, &l[i])) || (g_ed->nmsel && i < VX_CE_MAXLINES && g_ed->msel[i]);
            if (m) { mark[(size_t)i] = 1; any = true; }
        }
    }
    float th = 1.3f * (V.zoom > 2 ? 1.5f : 1.0f);
    for (int pass = 0; pass < (S.monitor_look && !any ? 2 : 1); pass++) {   // (the glow only without a pick)
        for (int i = 0; i < n; i++) {
            uint8_t r, g, b;
            if (!vx_line_color(&g_view, g_colors, &l[i], &r, &g, &b)) continue;
            const vx_rule *u = vx_colors_find(g_colors, &l[i]);
            if (S.show_free && !u) { r = 255; g = 0; b = 255; }
            float x0, y0, x1, y1;
            vx_map(&g_cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
            vx_map(&g_cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
            ImVec2 a = V.to(x0, y0), c = V.to(x1, y1);
            if (a.x == c.x && a.y == c.y) c.x += 1.0f;  // (a dot)
            if (pass == 0 && S.monitor_look && !any) { dl->AddLine(a, c, col(r, g, b, 40), th * 4.0f); continue; }
            if (any && !mark[(size_t)i]) { dl->AddLine(a, c, col(r, g, b, 60), th); continue; }   // (dimmed)
            if (any) continue;                  // (the marked ones: on top, below)
            dl->AddLine(a, c, col(r, g, b), th);
        }
    }
    if (!any) return;
    // the marked lines: full colour, a little thicker, with a thin dark edge so that they stand apart
    for (int i = 0; i < n; i++) {
        if (!mark[(size_t)i]) continue;
        uint8_t r, g, b;
        if (!vx_line_color(&g_view, g_colors, &l[i], &r, &g, &b)) { r = g = b = 255; }
        int lum = (r * 3 + g * 6 + b) / 10;
        if (lum < 140) { int k = 140 - lum; r = (uint8_t)std::min(255, r + k); g = (uint8_t)std::min(255, g + k); b = (uint8_t)std::min(255, b + k); }
        float x0, y0, x1, y1;
        vx_map(&g_cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
        vx_map(&g_cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
        ImVec2 a = V.to(x0, y0), c = V.to(x1, y1);
        if (a.x == c.x && a.y == c.y) c.x += 1.0f;
        dl->AddLine(a, c, col(0, 0, 0), th * 2.0f + 2.0f);
        dl->AddLine(a, c, col(r, g, b), th * 2.0f);
    }
}

// ------------------------------------------------------------------------------------------------
// the panels

static const char *SCOPE_LABEL[VX_CE_SCOPES] = { "Shape", "Line", "Object", "Group", "Code", "Routine", "Whole figure", "Same kind" };
static const char *SCOPE_TIP[VX_CE_SCOPES] = {
    "ALL OF THIS SHAPE (F4)\nThe picked shape wherever the game draws it: every mine of this kind, every copy of a word.",
    "THIS LINE (F5)\nOnly the picked line(s) of the shape, wherever the shape is drawn. Pick single lines with a click; hold Shift (or Ctrl) and click more lines, or drag a box, then click a colour: each picked line gets it.",
    "THIS OBJECT (F6)\nEverything the game draws for this object: the ship, its flame, its explosion.",
    "THIS GROUP (F7)\nThis kind of object near the picked one: the mines, but not the shots drawn by the same code.",
    "ALL BY THIS CODE (F8)\nEverything one place in the game's code draws. The way for games that draw without shapes.",
    "ALL OF THIS ROUTINE (F9)\nEverything drawn the same way: all text, all dots.",
    "SMART (F10)\nA click picks everything connected to the line: the whole figure, even when the game draws it in several parts. "
    "Shift+click adds (or takes away) one single line. The colour then goes to each shape picked whole (all its copies in the game) "
    "and to the other picked lines one by one.",
    "SAME KIND (F11)\nThe easy way: click a line, click a colour, done. Everything that looks like it gets the colour, "
    "wherever and whenever the game draws it: one coin makes all the coins, one wall all walls like it, one enemy all enemies of its kind. "
    "The yellow glow shows beforehand what will change. Shift+click more lines to add their kinds too."
};

// whether one of the rules colours the line (their RAM conditions aside)
static bool any_match(const vx_rule *c, int nc, const vx_line *l)
{
    for (int k = 0; k < nc; k++) if (vx_rule_matches(&c[k], l)) return true;
    return false;
}

static bool extras_on() { return (g_only_part && g_part.addr >= 0) || g_xr || g_ur || g_len || g_area; }

// the options' conditions (game part, Advanced) added to the rules a colour click makes: the same for
// the click and for the preview, so the picture shows beforehand what the click will colour
static void add_extras(vx_rule *c, int nc, const vx_line *l, int n)
{
    if (g_only_part && g_part.addr >= 0) {   // only while the game is in this part
        int v = shown_ram(g_part.addr) & g_part.mask;
        for (int k = 0; k < nc; k++) {
            vx_rule *r = &c[k];
            if (r->ncond >= VX_RULE_CONDS) continue;
            vx_cond *q = &r->cond[r->ncond++];
            memset(q, 0, sizeof *q);
            q->kind = VX_C_RAM; q->addr = (uint16_t)g_part.addr; q->mask = (uint8_t)g_part.mask; q->val = (uint8_t)v;
            size_t L = strlen(r->name);
            snprintf(r->name + L, sizeof r->name - L, "_p%X", v);
        }
    }
    if (g_xr) {                     // advanced: any vector list in the range
        unsigned lo = (unsigned)strtoul(g_xr_lo, nullptr, 16), hi = (unsigned)strtoul(g_xr_hi, nullptr, 16);
        for (int k = 0; k < nc; k++) {
            vx_rule *r = &c[k];
            bool have = false;
            for (int q = 0; q < r->ncond; q++) if (r->cond[q].kind == VX_C_X) { r->cond[q].lo = (uint16_t)lo; r->cond[q].hi = (uint16_t)hi; have = true; }
            if (!have && r->ncond < VX_RULE_CONDS) {
                vx_cond *q = &r->cond[r->ncond++];
                memset(q, 0, sizeof *q);
                q->kind = VX_C_X; q->lo = (uint16_t)lo; q->hi = (uint16_t)hi;
            }
        }
    }
    if (g_ur) {                                 // advanced: any object (U) in the range
        unsigned lo = (unsigned)strtoul(g_ur_lo, nullptr, 16), hi = (unsigned)strtoul(g_ur_hi, nullptr, 16);
        for (int k = 0; k < nc; k++) {
            vx_rule *r = &c[k];
            bool have = false;
            for (int q = 0; q < r->ncond; q++) if (r->cond[q].kind == VX_C_U) { r->cond[q].lo = (uint16_t)lo; r->cond[q].hi = (uint16_t)hi; have = true; }
            if (!have && r->ncond < VX_RULE_CONDS) {
                vx_cond *q = &r->cond[r->ncond++];
                memset(q, 0, sizeof *q);
                q->kind = VX_C_U; q->lo = (uint16_t)lo; q->hi = (uint16_t)hi;
            }
            size_t L = strlen(r->name);
            snprintf(r->name + L, sizeof r->name - L, "_u%04X", lo);
        }
    }
    if (g_len && g_len_picked) {    // the length from the picked lines: a box / Shift-click set,
        int i0 = 0, i1 = n - 1;                 // else the line clicked, else the chosen piece
        if (!g_ed->nmsel && g_ed->pick_line >= 0 && g_ed->pick_line < n) i0 = i1 = g_ed->pick_line;
        else if (!g_ed->nmsel && g_ed->sel >= 0 && g_ed->sel < g_ed->npieces) { i0 = g_ed->piece[g_ed->sel].first; i1 = g_ed->piece[g_ed->sel].last; }
        double mn = 1e9, mx = -1;
        for (int i = i0; i <= i1 && i < n; i++) {
            if (!l[i].z || (g_ed->nmsel && !g_ed->msel[i])) continue;
            double d = std::hypot((double)(l[i].x1 - l[i].x0), (double)(l[i].y1 - l[i].y0)) * 100.0 / VX_CANVAS_H;
            mn = std::min(mn, d); mx = std::max(mx, d);
        }
        if (mx >= 0) {                          // (with some room: three quarters to four thirds, at least 0.3 %)
            g_len_lo = (float)std::max(0.0, std::min(mn * 0.75, mn - 0.3));
            g_len_hi = (float)std::min(100.0, std::max(mx * 4.0 / 3.0, mx + 0.3));
        }
    }
    if (g_len) {                    // advanced: only lines of a length
        long lo = (long)(g_len_lo * VX_CANVAS_H / 100), hi = (long)(g_len_hi * VX_CANVAS_H / 100);
        if (hi > 65535) hi = 65535;
        if (lo > hi) lo = hi;
        for (int k = 0; k < nc; k++) {
            vx_rule *r = &c[k];
            if (r->ncond >= VX_RULE_CONDS) continue;
            vx_cond *q = &r->cond[r->ncond++];
            memset(q, 0, sizeof *q);
            q->kind = VX_C_LENGTH; q->lo = (uint16_t)lo; q->hi = (uint16_t)hi;
            size_t L = strlen(r->name);
            snprintf(r->name + L, sizeof r->name - L, "_len%d", (int)g_len_lo);
        }
    }
    if (g_area) {                   // advanced: only in the place of the picked lines (their box)
        long x0 = VX_CANVAS_W, x1 = 0, y0 = VX_CANVAS_H, y1 = 0;
        int i0 = 0, i1 = n - 1;                 // the lines picked: a box / Shift-click set, else the chosen piece
        if (!g_ed->nmsel && g_ed->sel >= 0 && g_ed->sel < g_ed->npieces) { i0 = g_ed->piece[g_ed->sel].first; i1 = g_ed->piece[g_ed->sel].last; }
        for (int i = i0; i <= i1 && i < n; i++) {
            if (!l[i].z || (g_ed->nmsel ? !g_ed->msel[i] : !any_match(c, nc, &l[i]))) continue;
            x0 = std::min(x0, std::min(l[i].x0, l[i].x1)); x1 = std::max(x1, std::max(l[i].x0, l[i].x1));
            y0 = std::min(y0, std::min(l[i].y0, l[i].y1)); y1 = std::max(y1, std::max(l[i].y0, l[i].y1));
        }
        const long m = VX_CANVAS_H / 100;         // (a little room: 1 %)
        x0 = std::max(0L, x0 - m); y0 = std::max(0L, y0 - m); x1 = std::min((long)VX_CANVAS_W, x1 + m); y1 = std::min((long)VX_CANVAS_H, y1 + m);
        for (int k = 0; k < nc && x1 >= x0; k++) {
            vx_rule *r = &c[k];
            if (r->ncond >= VX_RULE_CONDS) continue;
            vx_cond *q = &r->cond[r->ncond++];
            memset(q, 0, sizeof *q);
            q->kind = VX_C_AREA; q->lo = (uint16_t)x0; q->hi = (uint16_t)x1; q->lo2 = (uint16_t)y0; q->hi2 = (uint16_t)y1;
            size_t L = strlen(r->name);
            snprintf(r->name + L, sizeof r->name - L, "_at%ld_%ld", x0 * 100 / VX_CANVAS_W, y0 * 100 / VX_CANVAS_H);
        }
    }
}

// the rules a colour click would make now, with the options' conditions (the preview)
static int preview_cands(const vx_rule **out)
{
    static vx_rule pc[VX_CE_CANDS];
    *out = g_ed->cands;
    if (!g_ed->ncand || !extras_on()) return g_ed->ncand;
    const vx_line *l; int n = cur_lines(&l);
    memcpy(pc, g_ed->cands, sizeof pc[0] * (size_t)g_ed->ncand);
    add_extras(pc, g_ed->ncand, l, n);
    *out = pc;
    return g_ed->ncand;
}

static void apply_colour(int idx)               // a palette click: the picked lines get it at once
{
    if (g_sel_rule >= 0 && g_sel_rule < g_colors->n && idx >= 0) {   // a rule picked in the list: its colour
        undo_mark();
        uint8_t r, g, b;
        const char *nm = vx_ce_palette(idx, &r, &g, &b);
        vx_rule *u = &g_colors->rule[g_sel_rule];
        u->r = r; u->g = g; u->b = b;
        g_dirty = true;
        say(std::string(u->name) + " is " + nm + " now");
        return;
    }
    if (!g_ed_ok || (!g_ed->nmsel && g_ed->ncand == 0)) { say("Pick something first: click a line, or drag a box"); return; }
    undo_mark();
    const vx_line *l; int n = cur_lines(&l);
    g_ed->color = idx;
    vx_ce_refresh(g_ed, l, n);
    if (idx >= 0) add_extras(g_ed->cands, g_ed->ncand, l, n);
    if (g_ed->ncand) g_ed->cand = g_ed->cands[0];
    if (idx < 0) {
        int k = vx_ce_remove(g_ed, g_colors, l, n);
        say(k ? std::to_string(k) + " rule(s) removed" : std::string("No rule colours it"));
    } else if (vx_ce_take(g_ed, g_colors) != 0) say("The profile is full (" + std::to_string(VX_COLOR_RULES) + " rules)");
    else { uint8_t r, g, b; say(std::string(vx_ce_palette(idx, &r, &g, &b)) + ": " + std::to_string(g_ed->ncand) + " rule(s)"); }
    g_dirty = true;
}

static void set_scope(int s)
{
    if (!g_ed_ok) return;
    const vx_line *l; int n = cur_lines(&l);
    g_ed->scope = s;
    if (g_ed->ncand || g_ed->nmsel) vx_ce_refresh(g_ed, l, n);
}

static void panel_tools()
{
    ImGui::SeparatorText("What to colour");
    tip("WHAT: which lines a colour goes to. Click a line in the picture, then click a colour.");
    {                                           // the simple modes: big, side by side, the way to work
        static const int simple[2] = { VX_CE_KIND, VX_CE_SMART };
        static const char *sub[2] = { "all like it", "all that touches it" };
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2, bh = ImGui::GetFrameHeight() * 2.1f;
        for (int k = 0; k < 2; k++) {
            if (k) ImGui::SameLine();
            int sc = simple[k];
            bool on = g_ed_ok && g_ed->scope == sc;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            std::string lab = std::string(SCOPE_LABEL[sc]) + "\n" + sub[k] + "##simple" + std::to_string(k);
            if (ImGui::Button(lab.c_str(), ImVec2(bw, bh))) set_scope(sc);
            if (on) ImGui::PopStyleColor();
            tip(SCOPE_TIP[sc]);
        }
    }
    {                                           // the others: for people who know how games draw
        bool adv = g_ed_ok && g_ed->scope < VX_CE_SMART;
        if (adv) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (ImGui::CollapsingHeader("Advanced modes")) {
            for (int s = 0; s < VX_CE_SMART; s++) {
                if (s % 3) ImGui::SameLine();
                bool on = g_ed_ok && g_ed->scope == s;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(SCOPE_LABEL[s], ImVec2(ImGui::GetContentRegionAvail().x / (3 - s % 3) - (s % 3 < 2 ? 4 : 0), 0))) set_scope(s);
                if (on) ImGui::PopStyleColor();
                tip(SCOPE_TIP[s]);
            }
        } else tip("Shape, Line, Object, Group, Code, Routine: exactly which rule a colour makes. Not needed for most games: Same kind and Whole figure decide that themselves.");
    }
    if (g_ed_ok) {
        if (g_ed->nmsel) ImGui::Text("%d line(s) picked, %d line(s) will change", g_ed->nmsel, [] {
            const vx_line *l; int n = cur_lines(&l); int k = 0;
            const vx_rule *pv; int npv = preview_cands(&pv);
            for (int i = 0; i < n; i++) k += l[i].z && any_match(pv, npv, &l[i]);
            return k; }());
        else if (g_ed->ncand) ImGui::Text("%d line(s) will change (glowing): click a colour", [] {
            const vx_line *l; int n = cur_lines(&l); int k = 0;
            const vx_rule *pv; int npv = preview_cands(&pv);
            for (int i = 0; i < n; i++) k += l[i].z && any_match(pv, npv, &l[i]);
            return k; }());
        else ImGui::TextDisabled("Nothing picked: click a line or drag a box");
    }

    ImGui::SeparatorText("Colours");
    tip("Click a colour: the picked lines get it at once (Ctrl+Z undoes). The picture shows how it looks.");
    if (g_sel_rule >= 0 && g_sel_rule < g_colors->n) {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Changing rule %s: click a colour", g_colors->rule[g_sel_rule].name);
        ImGui::SameLine();
        if (ImGui::SmallButton("Done")) g_sel_rule = -1;
        tip("A rule is picked in the list: a colour click changes that rule's colour (its lines stay). Done or Esc: back to colouring what is picked in the picture.");
    }
    int np = vx_ce_palette_count();
    float sw = ImGui::GetFrameHeight() * 1.25f;
    int per = (int)((ImGui::GetContentRegionAvail().x + 4) / (sw + 4));
    if (per < 4) per = 4;
    for (int i = 0; i < np; i++) {
        uint8_t r, g, b;
        const char *name = vx_ce_palette(i, &r, &g, &b);
        if (i % per) ImGui::SameLine(0, 4);
        ImGui::PushID(i);
        bool cur = g_ed_ok && g_ed->color == i;
        if (cur) { ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f); ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 255)); }
        if (ImGui::ColorButton("##c", ImVec4(r / 255.f, g / 255.f, b / 255.f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(sw, sw))) apply_colour(i);
        if (cur) { ImGui::PopStyleColor(); ImGui::PopStyleVar(); }
        tip(name);
        ImGui::PopID();
    }
    if (ImGui::Button("No colour", ImVec2(-1, 0))) apply_colour(-1);
    tip("Removes the colour of the picked lines (Delete): they show the game's own colour again.");

    ImGui::SeparatorText("Brightness");
    if (g_ed_ok) {
        float gsl = g_ed->gain;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##gain", &gsl, 0.5f, 3.0f, "x %.1f")) g_ed->gain = gsl;
        if (ImGui::IsItemDeactivatedAfterEdit() && g_ed->color >= 0 && (g_ed->ncand || g_ed->nmsel)) apply_colour(g_ed->color);
        tip("How bright the picked lines are, on top of the game's own brightness. Let go of the slider to apply it.");
    }

    ImGui::SeparatorText("Colour by game part");
    tip("Give something a different colour in each part of the game: the ground per level or per section. The studio has to learn how the game tells its parts apart: remember two parts, it finds the byte.");
    if (g_part.addr >= 0) {
        ImGui::Checkbox("Only in this part of the game", &g_only_part);
        tip("The colours you click now count only while the game is in the part shown (the part byte has its value of now). Colour the ground once in every part, each time with this ticked.");
        if (g_part.mask == 0xFF) ImGui::TextDisabled("Part byte %04X: %02X here", g_part.addr, shown_ram(g_part.addr));
        else ImGui::TextDisabled("Part byte %04X & %02X: %02X here", g_part.addr, g_part.mask, shown_ram(g_part.addr) & g_part.mask);
    } else ImGui::TextDisabled("No part byte yet for this game");
    if (ImGui::Button("Remember part A", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 0))) { g_partA = remember_part(); if (!g_partB.empty()) part_candidates(); say("Part A remembered (the last second up to this frame)"); }
    tip("Play (or step) to a frame of one part of the game, e.g. level 1, then click. The second before it is remembered.");
    ImGui::SameLine();
    if (ImGui::Button("Remember part B", ImVec2(-1, 0))) { g_partB = remember_part(); if (!g_partA.empty()) part_candidates(); say("Part B remembered"); }
    tip("Then play on to another part, e.g. level 2, and click: the bytes that tell A and B apart are listed below.");
    if (!g_partA.empty() && !g_partB.empty()) {
        if (g_part_cands.empty()) ImGui::TextWrapped("No byte tells these two parts apart. Remember them again a bit later inside each part.");
        for (size_t i = 0; i < g_part_cands.size() && i < 8; i++) {
            const PartCand &c = g_part_cands[i];
            char t[96];
            if (c.mask == 0xFF) snprintf(t, sizeof t, "%04X: %02X in A, %02X in B", c.addr, c.va, c.vb);
            else snprintf(t, sizeof t, "%04X & %02X: %02X in A, %02X in B", c.addr, c.mask, c.va, c.vb);
            ImGui::PushID((int)i);
            if (ImGui::Selectable(t, g_part.addr == c.addr && g_part.mask == c.mask)) { g_part.addr = c.addr; g_part.mask = c.mask; part_save(); say("Part byte set"); }
            tip("Use this byte to tell the game's parts apart (kept for this game). The first ones are the likeliest; if colours then change at the wrong moment, try the next.");
            ImGui::PopID();
        }
    }

    if (g_demo_adv) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::CollapsingHeader("Advanced options")) {
        ImGui::Checkbox("Any vector list in a range", &g_xr);
        tip("For things the game draws with a changing list address (a gauge that grows and shrinks): the colours clicked now count for every list address in the range, with the same code.");
        if (g_xr) {
            ImGui::SetNextItemWidth(70); ImGui::InputText("from", g_xr_lo, sizeof g_xr_lo, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::InputText("to (hex)", g_xr_hi, sizeof g_xr_hi, ImGuiInputTextFlags_CharsHexadecimal);
        }
        ImGui::Checkbox("Any object in a range", &g_ur);
        tip("For figures a game draws row by row from tables (Berzerk Ultimate's robots and player): every picture of one kind lies in one range of the game's memory (the object, U). The colours clicked now count for every object in the range.");
        if (g_ur) {
            ImGui::SetNextItemWidth(70); ImGui::InputText("from##ur", g_ur_lo, sizeof g_ur_lo, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::InputText("to (hex)##ur", g_ur_hi, sizeof g_ur_hi, ImGuiInputTextFlags_CharsHexadecimal);
        }
        ImGui::Checkbox("Only where it is now", &g_area);
        tip("For fixed scenery the game draws with the same code as the rest (Clean Sweep: the vaults and the corner rooms): the colours clicked now count only for lines inside the place the picked lines take up now.");
        ImGui::Checkbox("Only lines of a length", &g_len);
        tip("For a game that draws different things with the same code, told apart only by their length (Clean Sweep: the short money dashes and the long wall lines): the colours clicked now count only for lines this long, in % of the picture's height.");
        if (g_len) {
            ImGui::Checkbox("Length from the picked lines", &g_len_picked);
            tip("On: click a line in the picture (or drag a box over some, Shift+click for more) and click a colour. The colour counts for every line about as long as the picked ones; the numbers below show the range taken. Off: the numbers below as you set them.");
            ImGui::SetNextItemWidth(-1);            // drag with the mouse (left/right), Ctrl+click or double-click to type
            ImGui::BeginDisabled(g_len_picked);     // (from the picked lines: shown, follows the pick)
            ImGui::DragFloatRange2("##len", &g_len_lo, &g_len_hi, 0.1f, 0.0f, 100.0f, "from %.1f %%", "to %.1f %%", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            tip("Drag left or right to change the length (% of the picture's height). Double-click to type a number.");
        }
    }
}

static void panel_profile()
{
    ImGui::SeparatorText("Profile");
    tip("A game can have several colour profiles: ORIGINAL (comes with the emulator) and your own PROFILE 1, 2, 3 ...");
    auto label = [](int s) -> std::string {
        std::string t = s == 0 ? "OFF" : s == 1 ? "ORIGINAL" : "PROFILE " + std::to_string(s - 1);
        char p[1100];
        if (s > 0 && vx_colors_profile_path(g_id, s - 1, p, sizeof p) == 0 && !vx_colors_file_fits(p)) t += "  (other version)";
        return t; };
    std::string cur = label(g_setting) + (g_dirty ? "  (changed)" : "");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##profile", cur.c_str())) {
        int top = vx_colors_own_count(g_id) + 1;
        for (int s = 1; s <= top; s++) {
            char p[1100];
            if (vx_colors_profile_path(g_id, s - 1, p, sizeof p) != 0) continue;
            if (ImGui::Selectable(label(s).c_str(), s == g_setting)) {
                if (g_dirty) say("Unsaved changes dropped");
                load_profile(s);
            }
        }
        ImGui::EndCombo();
    }
    tip("The profile shown and edited. Choosing another one drops unsaved changes. \"Other version\": the profile was made for a cartridge with another checksum (another version of the game), its colours may miss.");
    std::string where = "Saves to PROFILE " + std::to_string(g_target) + (g_target_new ? " (new)" : "");
    ImGui::TextDisabled("%s", where.c_str());
    if (ImGui::Button("Save", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 0))) save_profile();
    tip("Ctrl+S. ORIGINAL is never overwritten: your colours go to your own profile, the game then uses it.");
    ImGui::SameLine();
    if (ImGui::Button("Save as new", ImVec2(-1, 0))) {
        g_target = vx_coloredit_target(g_id, g_setting, 1, &g_target_new);
        save_profile();
    }
    tip("Keeps the profile shown as it is and saves your colours as a new PROFILE with the next number.");

    ImGui::SeparatorText("Rules");
    tip("What the profile colours, the first matching rule wins. Hover a rule to see its lines; Delete removes the rule under the mouse.");
    g_hover_rule = -1;
    if (ImGui::BeginChild("rules", ImVec2(-1, ImGui::GetContentRegionAvail().y - 4), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < g_colors->n; i++) {
            const vx_rule *r = &g_colors->rule[i];
            ImGui::PushID(i);
            ImGui::ColorButton("##rc", ImVec4(r->r / 255.f, r->g / 255.f, r->b / 255.f, 1), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine();
            char t[96];
            snprintf(t, sizeof t, "%s%s", r->name[0] ? r->name : "(rule)", r->gain != 1.0f ? " *" : "");
            if (ImGui::Selectable(t, g_sel_rule == i)) g_sel_rule = g_sel_rule == i ? -1 : i;
            if (ImGui::IsItemHovered()) {
                g_hover_rule = i;
                if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                    undo_mark();
                    memmove(&g_colors->rule[i], &g_colors->rule[i + 1], sizeof(vx_rule) * (size_t)(g_colors->n - i - 1));
                    g_colors->n--;
                    g_dirty = true;
                    g_sel_rule = -1;
                    ImGui::PopID();
                    break;
                }
            }
            tip("Hover: its lines blink in the picture. Click: pick the rule, then a colour click changes its colour. Delete: remove this rule.");
            ImGui::PopID();
        }
        if (!g_colors->n) ImGui::TextDisabled("No rules yet");
    }
    ImGui::EndChild();
}

static bool g_show_connect = false;

static void panel_games()
{
    if (ImGui::BeginTabBar("games")) {
        if (ImGui::BeginTabItem("This PC", nullptr, g_demo_tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            tip("The cartridges in the roms folder next to this program. Click one to colour it, right-click it to copy it to the cabinet.\n"
                "A green dot: it has an ORIGINAL colour profile. A ring: only own profiles (+N: how many own ones).\n"
                "Right: where the game is (when connected to the cabinet): both, PC only.");
            std::lock_guard<std::mutex> lk(g_mx);
            if (C.connected) {
                std::vector<std::string> miss;
                for (int i = 0; i < g_roms.n; i++) if (!on_cabinet(i)) miss.push_back(g_roms.rom[i].path);
                std::string b = "Copy " + std::to_string(miss.size()) + " missing game(s) to the cabinet";
                if (miss.empty()) ImGui::BeginDisabled();
                if (ImGui::Button(b.c_str(), ImVec2(-1, 0))) copy_to_cabinet(miss);
                if (miss.empty()) ImGui::EndDisabled();
                tip("Every game of this PC the cabinet has not got (by its checksum, else its file name) goes to the cabinet's /persistent/games/rom/vectrex. Nothing there is overwritten.");
            }
            if (ImGui::BeginChild("pc", ImVec2(-1, -1))) {
                for (int i = 0; i < g_roms.n; i++) {
                    bool cur = base_name(g_roms.rom[i].path) == base_name(g_cart);
                    std::string lab = std::string("     ") + g_roms.rom[i].name + "##pc" + std::to_string(i);
                    if (ImGui::Selectable(lab.c_str(), cur) && !cur) {
                        leave_to(g_roms.rom[i].path);
                    }
                    const RomColours rc = i < (int)g_rom_colours.size() ? g_rom_colours[(size_t)i] : RomColours();
                    bool both = C.connected && on_cabinet(i);
                    colour_mark(rc.original, rc.own, C.connected ? (both ? "both" : "PC only") : nullptr,
                                both ? IM_COL32(150, 150, 160, 255) : IM_COL32(110, 190, 255, 255));
                    if (ImGui::BeginPopupContextItem()) {
                        ImGui::TextDisabled("%s", g_roms.rom[i].name);
                        if (ImGui::MenuItem("Colour it", nullptr, false, !cur)) leave_to(g_roms.rom[i].path);
                        if (ImGui::MenuItem("Copy to the cabinet", nullptr, false, C.connected && !both)) copy_to_cabinet({ g_roms.rom[i].path });
                        if (!C.connected) ImGui::TextDisabled("(connect to the cabinet to copy)");
                        else if (both) ImGui::TextDisabled("(the cabinet has it)");
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        std::string t = base_name(g_roms.rom[i].path) + (C.connected ? (both ? "\nOn the cabinet too" : "\nOnly on this PC (right-click: copy it to the cabinet)") : "") + "\n" +
                                        (rc.original ? "ORIGINAL colour profile: " + rc.file : std::string("No ORIGINAL colour profile")) +
                                        (rc.own ? "\nOwn profiles: " + std::to_string(rc.own) : std::string(""));
                        ImGui::SetTooltip("%s", t.c_str());
                    }
                }
                if (!g_roms.n) ImGui::TextWrapped("No games yet. Connect to the cabinet and pick one there, or put cartridges in the roms folder.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cabinet", nullptr, g_demo_tab == 1 && C.connected ? ImGuiTabItemFlags_SetSelected : 0)) {
            tip("The games on the cabinet (when connected). Click one to colour it (it is fetched first if this PC has not got it), right-click it to copy it to this PC.\n"
                "A green dot: it has an ORIGINAL colour profile. Right: where the game is: both, cabinet only.");
            std::lock_guard<std::mutex> g(g_mx);
            if (!C.connected) {
                ImGui::TextWrapped("%s", C.status.c_str());
                if (ImGui::Button("Connect...", ImVec2(-1, 0))) g_show_connect = true;
                tip("Log in to the cabinet over the network.");
            } else if (ImGui::BeginChild("cab", ImVec2(-1, -1))) {
                if (ImGui::Button("Sync colour profiles...", ImVec2(-1, 0))) g_show_sync = true;
                tip("Puts this PC's colour profiles on the cabinet: every ORIGINAL, and the own ones the cabinet has not got.");
                std::vector<CabGame> miss;
                for (auto &cg : C.games) if (pc_index_of(cg) < 0) miss.push_back(cg);
                std::string b = "Copy " + std::to_string(miss.size()) + " missing game(s) to this PC";
                if (miss.empty()) ImGui::BeginDisabled();
                if (ImGui::Button(b.c_str(), ImVec2(-1, 0))) copy_to_pc(miss);
                if (miss.empty()) ImGui::EndDisabled();
                tip("Every game of the cabinet this PC has not got goes to the roms folder here, with the cabinet's own profiles of it. Nothing here is overwritten.");
                for (auto &cg : C.games) {
                    std::string t = "     " + cg.file + "##cab" + cg.path;
                    int pi = pc_index_of(cg);
                    if (ImGui::Selectable(t.c_str(), pi >= 0 && base_name(g_roms.rom[pi].path) == base_name(g_cart))) {
                        if (pi >= 0) leave_to(g_roms.rom[pi].path);
                        else cabinet_fetch(cg);
                    }
                    colour_mark(cg.original, cg.profiles, pi >= 0 ? "both" : "cabinet only",
                                pi >= 0 ? IM_COL32(150, 150, 160, 255) : IM_COL32(255, 180, 90, 255));
                    if (ImGui::BeginPopupContextItem()) {
                        ImGui::TextDisabled("%s", cg.file.c_str());
                        if (ImGui::MenuItem("Colour it")) { if (pi >= 0) leave_to(g_roms.rom[pi].path); else cabinet_fetch(cg); }
                        if (ImGui::MenuItem("Copy to this PC", nullptr, false, pi < 0)) copy_to_pc({ cg });
                        if (pi >= 0) ImGui::TextDisabled("(this PC has it)");
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s\n%s\n%s\n%s", cg.path.c_str(), pi >= 0 ? "On this PC too" : "Only on the cabinet (click: fetch and colour it; right-click: copy it)",
                                          cg.original ? "Has an ORIGINAL colour profile there" : "No ORIGINAL colour profile there",
                                          cg.setting >= 2 ? ("Uses PROFILE " + std::to_string(cg.setting - 1) + " there").c_str()
                                          : cg.setting == 0 ? "Profile OFF there" : "Uses ORIGINAL there");
                }
            }
            if (C.connected) ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void controls_window()
{
    if (!g_show_controls) { g_bind_wait = -1; return; }
    ImGui::OpenPopup("Controls");
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34, 0));
    if (ImGui::BeginPopupModal("Controls", &g_show_controls)) {
        ImGui::TextWrapped("Click a field, then press the key or the gamepad button (or move a stick) you want. Esc: keep it as it was.");
        ImGui::TextDisabled("%s", g_pad ? (std::string("Gamepad: ") + (SDL_GameControllerName(g_pad) ? SDL_GameControllerName(g_pad) : "connected")).c_str()
                                        : "No gamepad connected (plug one in: it is found at once)");
        if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Keyboard");
            ImGui::TableSetupColumn("Gamepad");
            ImGui::TableHeadersRow();
            for (int i = 0; i < B_COUNT; i++) {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(BIND[i].label);
                for (int c = 0; c < 2; c++) {
                    ImGui::TableNextColumn();
                    bool waiting = g_bind_wait == i && g_bind_col == c;
                    std::string t = waiting ? (c ? "Press a button..." : "Press a key...") : c ? pad_name(BIND[i].pad) : key_name(BIND[i].key);
                    ImGui::PushID(c);
                    if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(t.c_str(), ImVec2(-ImGui::GetFrameHeight() - 4, 0))) { g_bind_wait = i; g_bind_col = c; }
                    if (waiting) ImGui::PopStyleColor();
                    tip(c ? "Click, then press a gamepad button or move a stick that way." : "Click, then press a key.");
                    ImGui::SameLine();
                    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) { if (c) BIND[i].pad = -1; else BIND[i].key = 0; g_bind_wait = -1; studio_save(); }
                    tip("No input for this.");
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("The left stick of a gamepad always steers too.");
        if (ImGui::Button("Reset to the standard", ImVec2(200, 0))) {
            for (int i = 0; i < B_COUNT; i++) { BIND[i].key = BIND[i].key0; BIND[i].pad = BIND[i].pad0; }
            g_bind_wait = -1;
            studio_save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) { g_show_controls = false; g_bind_wait = -1; }
        ImGui::EndPopup();
    }
}

static void connect_window()
{
    if (!g_show_connect) return;
    ImGui::OpenPopup("Connect to the cabinet");
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28, 0));
    if (ImGui::BeginPopupModal("Connect to the cabinet", &g_show_connect)) {
        static char pass[128] = "";
        ImGui::TextWrapped("The cabinet must be on and in this network.");
        ImGui::InputText("Address", S.host, sizeof S.host);
        tip("The cabinet's network address (IP), e.g. 192.168.0.122.");
        ImGui::InputText("User", S.user, sizeof S.user);
        tip("The cabinet's user name: pi on a standard VectorPie.");
        ImGui::InputText("Password", pass, sizeof pass, ImGuiInputTextFlags_Password);
        tip("The user's password. Not needed with a key file. It is not saved anywhere.");
        ImGui::InputText("Key file", S.key, sizeof S.key);
        tip("Optional: an SSH key (a file) that logs in without a password. Used when the file is there.");
        bool busy = J.state == 1;
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button(busy ? "Connecting..." : "Connect", ImVec2(120, 0))) {
            g_password = pass;
            studio_save();
            cabinet_connect();
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) g_show_connect = false;
        {
            std::lock_guard<std::mutex> g(g_mx);
            ImGui::TextWrapped("%s", C.status.c_str());
            if (C.connected && !busy) g_show_connect = false;
        }
        if (J.state == 2 && !C.connected && !J.out.empty() && ImGui::CollapsingHeader("Details")) ImGui::TextWrapped("%s", J.out.c_str());
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------------------------------------

static void unpick()                             // nothing picked (Esc)
{
    g_ed->nmsel = 0;
    memset(g_ed->msel, 0, sizeof g_ed->msel);
    g_ed->ncand = 0;
}

static void undo()
{
    g_redo.push_back(*g_colors);
    *g_colors = g_undo.back();
    g_undo.pop_back();
    g_dirty = true;
}

static void redo()
{
    g_undo.push_back(*g_colors);
    *g_colors = g_redo.back();
    g_redo.pop_back();
    g_dirty = true;
}

// A job on the cabinet has ended (J.state 2): what it answered
static void job_finished()
{
    J.state = 0;
    if (J.th.joinable()) J.th.join();
    if (J.out.rfind("FETCHED ", 0) == 0) {
        std::string cart = J.out.substr(8);
        leave_to(cart);
    } else if (J.out.find("SENT") != std::string::npos) say("Sent to the cabinet. There: PICTURE, COLOUR PROFILE (or it is used already)");
    else if (J.what.rfind("Sending", 0) == 0 || J.what.rfind("Fetching", 0) == 0) say(J.what + " failed: " + (J.out.empty() ? "no answer" : J.out.substr(0, 120)));
    else if (J.what == "Connecting") { std::lock_guard<std::mutex> g(g_mx); say(C.status); pick_view_settings(); }
    else if (J.what.rfind("Copying", 0) == 0) {
        if (J.what == "Copying to this PC") { free(g_roms.rom); vx_roms_scan(&g_roms, g_rom_dir.c_str()); rom_colours(); }
        say(J.out.rfind("COPIED ", 0) == 0 ? J.out.substr(7) : "Copying failed: " + J.out);
    }
    else if (J.what.rfind("Syncing", 0) == 0) say(J.out.rfind("SYNC ", 0) == 0 ? "Colour profiles on the cabinet: " + J.out.substr(5) : "Sync failed: " + J.out);
}

static void menu_bar()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save profile", "Ctrl+S", false, g_running)) save_profile();
            if (ImGui::MenuItem("Quit")) leave_to("");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !g_undo.empty())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !g_redo.empty())) redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool b = S.monitor_look; if (ImGui::MenuItem("Monitor glow", nullptr, &b)) { S.monitor_look = b; studio_save(); }
            b = S.show_free; if (ImGui::MenuItem("Show uncoloured lines", nullptr, &b)) { S.show_free = b; studio_save(); }
            b = S.cabinet_look; if (ImGui::MenuItem("Use the cabinet's picture settings", nullptr, &b)) { S.cabinet_look = b; studio_save(); pick_view_settings(); }
            if (ImGui::MenuItem("Reset zoom", "Home")) { V.zoom = 1; V.pan = ImVec2(0, 0); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Controls...")) g_show_controls = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Cabinet")) {
            bool conn; { std::lock_guard<std::mutex> g(g_mx); conn = C.connected; }
            if (ImGui::MenuItem(conn ? "Connect again..." : "Connect...")) g_show_connect = true;
            if (ImGui::MenuItem("Send profile", nullptr, false, conn && g_running)) cabinet_send(false);
            if (ImGui::MenuItem("Send profile and use it there", nullptr, false, conn && g_running)) cabinet_send(true);
            if (ImGui::MenuItem("Sync colour profiles...", nullptr, false, conn)) g_show_sync = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Copy missing games to this PC", nullptr, false, conn)) {
                std::vector<CabGame> miss;
                { std::lock_guard<std::mutex> lk(g_mx); for (auto &cg : C.games) if (pc_index_of(cg) < 0) miss.push_back(cg); }
                copy_to_pc(miss);
            }
            if (ImGui::MenuItem("Copy missing games to the cabinet", nullptr, false, conn)) {
                std::vector<std::string> miss;
                { std::lock_guard<std::mutex> lk(g_mx); for (int i = 0; i < g_roms.n; i++) if (!on_cabinet(i)) miss.push_back(g_roms.rom[i].path); }
                copy_to_cabinet(miss);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextUnformatted(
                "1. Pick a game on the left.\n"
                "2. Play to the scene you want (arrows, A S D F = buttons 1-4, or a gamepad), then Space pauses.\n"
                "   Settings > Controls sets other keys and gamepad buttons.\n"
                "3. Click a line: Same kind picks everything like it. Shift+click adds more, or drag a box.\n"
                "4. Click a colour. Done.\n"
                "5. Save (Ctrl+S), then Cabinet > Send profile.\n\n"
                "Space play / pause     .  ,  one frame on / back\n"
                "F4-F11 WHAT            Delete  no colour\n"
                "Ctrl+Z / Ctrl+Y undo / redo     Esc  pick nothing\n"
                "Mouse wheel zoom       right drag  move the picture");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// The transport on the picture: play / pause, one frame back / on, restart
static void transport_buttons()
{
    if (g_running) {
        if (ImGui::Button(g_paused ? "Play" : "Pause", ImVec2(80, 0))) { g_paused = !g_paused; if (g_paused) editor_cut(); }
        tip("Space. Play the game to reach a scene (arrows, A S D F = buttons 1-4, or a gamepad), pause it to paint.");
        ImGui::SameLine();
        if (!g_paused) ImGui::BeginDisabled();
        if (ImGui::Button("Previous frame", ImVec2(130, 0))) step_frame(-1);
        tip("The comma key. One frame back (the last 250 frames are kept): to find the frame where a thing is drawn.");
        ImGui::SameLine();
        if (ImGui::Button("Next frame", ImVec2(110, 0))) step_frame(1);
        tip("The period key. One frame on: games draw some things only every other frame.");
        if (!g_paused) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Restart game", ImVec2(120, 0))) { vx_power_on(g_cfg.skip_bios); g_paused = false; g_hist.clear(); g_ram.clear(); g_hist_pos = -1; }
        tip("Starts the game from the beginning (the colours stay).");
        ImGui::SameLine();
        if (g_hist_pos >= 0) ImGui::TextDisabled("frame %ld (%ld back)%s", shown_frame(), g_frame - shown_frame(), g_view_cab ? "   cabinet's picture settings" : "");
        else ImGui::TextDisabled("frame %ld%s", g_frame, g_view_cab ? "   cabinet's picture settings" : "");
    }
}

// Leaving the game with unsaved changes (g_leave 1: quit, 2: another game): save them first?
static void leave_popup()
{
    if (g_leave) {                          // unsaved changes: save them first?
        ImGui::OpenPopup("Save changes?");
        if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("The colours of %s have unsaved changes.", g_title.c_str());
            ImGui::Text("Save them as PROFILE %d%s?", g_target, g_target_new ? " (new)" : "");
            int go = 0;
            if (ImGui::Button("Save", ImVec2(120, 0))) { if (save_profile()) go = 1; }
            ImGui::SetItemTooltip("Saves to your own profile (ORIGINAL is never overwritten), then goes on.");
            ImGui::SameLine();
            if (ImGui::Button("Don't save", ImVec2(120, 0))) { g_dirty = false; go = 1; }
            ImGui::SetItemTooltip("Drops the changes since the last save.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { g_leave = 0; ImGui::CloseCurrentPopup(); }
            ImGui::SetItemTooltip("Back to the colours.");
            if (go) {
                int k = g_leave; g_leave = 0;
                ImGui::CloseCurrentPopup();
                if (k == 1) g_quit = true; else restart_with(g_leave_cart);
            }
            ImGui::EndPopup();
        }
    }
}

int main(int argc, char **argv)
{
    // ssh asks this program for the password (SSH_ASKPASS): answer and leave
    if (getenv("VX_STUDIO_ASKPASS") && (argc < 2 || strncmp(argv[1], "--", 2) != 0)) {
        std::string p = getenv("VX_STUDIO_PASS") ? getenv("VX_STUDIO_PASS") : "";
        p += "\n";
        DWORD w;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), p.data(), (DWORD)p.size(), &w, nullptr);
        return 0;
    }
    studio_load();
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--game")) g_cart = argv[i + 1];
    // (development: --shot FILE.bmp N: the window as a picture at frame N, then leave; before it
    //  --demo-box X0 Y0 X1 Y1 (board units) picks the lines in that box, --demo-scope S, --demo-colour I)
    const char *shot = nullptr; int shot_at = 0, demo_box[4] = { -1, 0, 0, 0 }, demo_scope = -1, demo_colour = -2;
    for (int i = 1; i + 2 < argc; i++) if (!strcmp(argv[i], "--shot")) { shot = argv[i + 1]; shot_at = atoi(argv[i + 2]); }
    for (int i = 1; i + 4 < argc; i++) if (!strcmp(argv[i], "--demo-box")) for (int k = 0; k < 4; k++) demo_box[k] = atoi(argv[i + 1 + k]);
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--demo-scope")) demo_scope = atoi(argv[i + 1]);
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--demo-colour")) demo_colour = atoi(argv[i + 1]);
    const char *demo_clicks = nullptr;                       // (--demo-clicks "X,Y;X,Y": board units)
    int demo_back = 0;                                       // (--demo-back N: N frames back first)
    long demo_pa = -1, demo_pb = -1;                         // (--demo-part A B: parts remembered at frames A and B)
    for (int i = 1; i + 2 < argc; i++) if (!strcmp(argv[i], "--demo-part")) { demo_pa = atol(argv[i + 1]); demo_pb = atol(argv[i + 2]); }
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--demo-back")) demo_back = atoi(argv[i + 1]);
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--demo-clicks")) demo_clicks = argv[i + 1];
    bool demo_unpick = false;                                // (--demo-unpick: after the colour, nothing picked, as Esc)
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--demo-unpick")) demo_unpick = true;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--list-script")) {   // (development: the list's script into a file)
        write_file(argv[i + 1], std::string(LIST_GAMES) + LIST_CMD + "\n");
        return 0;
    }
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--sync-script")) {   // (development: the sync's script into a file)
        int k = 0;
        std::string sc = sync_script(&k);
        write_file(argv[i + 1], sc);
        return k ? 0 : 1;
    }
    for (int i = 1; i < argc; i++) {                         // (--demo-len / --demo-area: Advanced's options on)
        if (!strcmp(argv[i], "--demo-len")) g_len = g_demo_adv = true;
        if (!strcmp(argv[i], "--demo-area")) g_area = g_demo_adv = true;
        if (!strcmp(argv[i], "--demo-controls")) g_show_controls = true;   // (the Controls window open)
        if (!strcmp(argv[i], "--demo-connect")) g_demo_tab = i + 1 < argc && !strcmp(argv[i + 1], "cabinet") ? 1 : 0;
    }

    vx_config all;
    load_config(&all, nullptr, save_dir());
    g_rom_dir = all.rom_dir;
    vx_roms_scan(&g_roms, all.rom_dir);
    rom_colours();
    g_colors = new vx_colors();
    g_ed = new vx_coloredit();

    std::string bios = asset("system/bios.bin");
    char err[256] = "";
    if (!g_cart.empty()) {
        vx_game_id(g_cart.c_str(), g_id, sizeof g_id);
        vx_colors_game(g_cart.c_str());         // (its checksum: its profiles found by it, written into those saved)
        for (int i = 0; i < g_roms.n; i++) if (base_name(g_roms.rom[i].path) == base_name(g_cart)) g_title = g_roms.rom[i].name;
        if (g_title == "No game") g_title = base_name(g_cart);
    }
    load_config(&g_cfg, g_id, save_dir());
    if (!g_cart.empty() && vx_open(bios.c_str(), g_cart.c_str(), err, sizeof err) == 0) {
        g_running = true;
        vx_set_mains_hum(0);
        vx_power_on(g_cfg.skip_bios);
        load_profile(g_cfg.color_profile);
        part_load();                            // (this game's part byte, if found before)
        g_paused = false;
    } else if (!g_cart.empty()) say(std::string("Could not open the game: ") + err);
    pick_view_settings();

    if (g_demo_tab >= 0) cabinet_connect();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) return 1;
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    SDL_Window *win = SDL_CreateWindow("Vectrex Colour Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 920,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!win || !ren) return 1;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.FrameRounding = 4; st.WindowRounding = 0; st.GrabRounding = 4; st.ItemSpacing = ImVec2(8, 6);
    if (file_there("C:\\Windows\\Fonts\\segoeui.ttf")) io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);
    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++) if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);

    double next = SDL_GetTicks() / 1000.0;
    bool &quit = g_quit;
    bool want_shot = false;
    long &frame = g_frame;
    bool dragging = false, panning = false;
    ImVec2 drag0;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) { if (shot) quit = true; else leave_to(""); }
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad); pad = nullptr;
            }
            if (g_bind_wait >= 0) {             // (Settings > Controls: this input for the action)
                bool done = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) done = true;
                else if (ev.type == SDL_KEYDOWN && g_bind_col == 0) { BIND[g_bind_wait].key = ev.key.keysym.scancode; done = true; }
                else if (ev.type == SDL_CONTROLLERBUTTONDOWN && g_bind_col == 1) { BIND[g_bind_wait].pad = ev.cbutton.button; done = true; }
                else if (ev.type == SDL_CONTROLLERAXISMOTION && g_bind_col == 1 && abs(ev.caxis.value) > 20000) {
                    BIND[g_bind_wait].pad = PAD_AXIS + ev.caxis.axis * 2 + (ev.caxis.value > 0 ? 1 : 0); done = true;
                }
                if (done) { g_bind_wait = -1; studio_save(); }
            }
        }
        if (J.state == 2) job_finished();     // (a job on the cabinet has ended)

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        double now = SDL_GetTicks() / 1000.0;

        // the keyboard: the game's controls while it runs (not while typing), the tools' keys
        bool typing = io.WantTextInput;
        if (!typing) {
            const Uint8 *kb = SDL_GetKeyboardState(nullptr);
            bool listen = g_bind_wait < 0;          // (not while Settings > Controls waits for an input)
            if (listen && bind_pressed(B_PAUSE, kb, now, false) && g_running) { g_paused = !g_paused; if (g_paused) editor_cut(); }
            if (listen && bind_pressed(B_STEP, kb, now, true) && g_paused && g_running) step_frame(1);
            if (listen && bind_pressed(B_BACK, kb, now, true) && g_paused && g_running) step_frame(-1);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !g_undo.empty()) { undo(); say("Undone"); }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && !g_redo.empty()) { redo(); say("Redone"); }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) save_profile();
            for (int s = 0; s < VX_CE_SCOPES; s++) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_F4 + s))) set_scope(s);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && g_hover_rule < 0) apply_colour(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g_sel_rule = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && g_ed_ok) unpick();
        }
        vx_pad p1 = { 0, 0, 0 }, p2 = { 0, 0, 0 };
        g_pad = pad;
        if (!typing && !g_paused && g_bind_wait < 0) {
            const Uint8 *k = SDL_GetKeyboardState(nullptr);
            if (pad) {                          // (the left stick always steers)
                int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 256, ay = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 256;
                if (abs(ax) > 20) p1.x = ax < -128 ? -128 : ax > 127 ? 127 : ax;
                if (abs(ay) > 20) p1.y = ay < -128 ? -128 : ay > 127 ? 127 : ay;
            }
            float r = bind_held(B_RIGHT, k), lf = bind_held(B_LEFT, k), u = bind_held(B_UP, k), d = bind_held(B_DOWN, k);
            if (r > 0 || lf > 0) p1.x = (int)(r * 127) - (int)(lf * 128);
            if (u > 0 || d > 0) p1.y = (int)(u * 127) - (int)(d * 128);
            for (int b = 0; b < 4; b++) if (bind_held(B_1 + b, k) > 0.5f) p1.buttons |= 1u << b;
        }
        // the game: 50 frames a second while it runs
        if (g_running && !g_paused) {
            int k = 0;
            while (now >= next && k++ < 4) {
                run_frame(&p1, &p2);
                next += 0.02;
            }
            if (now > next + 0.2) next = now;
        } else next = now;
        if (demo_pa >= 0 && frame >= demo_pa && g_partA.empty()) g_partA = remember_part();
        if (demo_pb >= 0 && frame >= demo_pb && g_partB.empty()) {
            g_partB = remember_part();
            part_candidates();
            if (!g_part_cands.empty()) { g_part.addr = g_part_cands[0].addr; g_part.mask = g_part_cands[0].mask; g_only_part = true; }
        }
        if (shot && frame >= shot_at && !g_paused) {
            g_paused = true;
            editor_cut();
            for (int k = 0; k < demo_back; k++) step_frame(-1);
            if (g_ed_ok && (demo_box[0] >= 0 || demo_clicks)) {
                const vx_line *l; int n = cur_lines(&l);
                if (demo_scope >= 0) g_ed->scope = demo_scope;
                if (demo_box[0] >= 0)
                    vx_ce_mouse_pick(g_ed, g_colors, &g_cfg, l, n, demo_box[0], demo_box[1], demo_box[2], demo_box[3],
                                     demo_box[0] != demo_box[2] || demo_box[1] != demo_box[3], 0);
                for (const char *q = demo_clicks; q && *q;) {   // ("X,Y;X,Y;...": the first a click, the others Shift+clicks)
                    int x = atoi(q), y = 0;
                    const char *c = strchr(q, ',');
                    if (c) y = atoi(c + 1);
                    bool more = g_ed->ncand > 0 || g_ed->nmsel > 0;
                    vx_ce_mouse_pick(g_ed, g_colors, &g_cfg, l, n, x, y, x, y, 0, more);
                    g_ed->panel = 1;
                    const char *s = strchr(q, ';');
                    q = s ? s + 1 : nullptr;
                }
                g_ed->panel = 1;
                if (demo_scope >= 0) set_scope(demo_scope);
                if (demo_colour > -2) apply_colour(demo_colour);
                if (demo_unpick) unpick();   // (as Esc)
            }
        }

        // the layout: menu bar, games (left), picture (middle), tools (right), status line
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        float W = vp->WorkSize.x, H = vp->WorkSize.y;
        menu_bar();
        float top = vp->WorkPos.y, left_w = 270, right_w = 340, status_h = ImGui::GetFrameHeight() + 8;
        ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(0, top)); ImGui::SetNextWindowSize(ImVec2(left_w, H - status_h));
        ImGui::Begin("Games", nullptr, fixed);
        panel_games();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(W - right_w, top)); ImGui::SetNextWindowSize(ImVec2(right_w, H - status_h));
        ImGui::Begin("Tools", nullptr, fixed);
        if (!g_running) ImGui::TextWrapped("Pick a game on the left.");
        else if (!g_tagged) ImGui::TextWrapped("This game draws with its own processor (UVMC2 / PiTrex): it cannot be coloured.");
        else {
            if (!g_paused) ImGui::TextWrapped("Playing. Press Space (or click into the picture) to pause and paint.");
            if (!g_paused) ImGui::BeginDisabled();
            panel_tools();
            if (!g_paused) ImGui::EndDisabled();
            panel_profile();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(left_w, top)); ImGui::SetNextWindowSize(ImVec2(W - left_w - right_w, H - status_h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(g_title.c_str(), nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + av.x, p0.y + av.y), col(0, 0, 0));
            // the board stretched to the tube's shape, fitted, zoomed and moved
            float aspect = g_cfg.monitor == VX_MONITOR_VERTICAL ? 3.0f / 4.0f : 4.0f / 3.0f;
            float vw = av.x - 20, vh = av.y - 20;
            if (vw / vh > aspect) vw = vh * aspect; else vh = vw / aspect;
            V.size = ImVec2(vw * V.zoom, vh * V.zoom);
            V.o = ImVec2(p0.x + (av.x - V.size.x) * 0.5f + V.pan.x, p0.y + (av.y - V.size.y) * 0.5f + V.pan.y);
            ImGui::InvisibleButton("picture", av, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            bool hov = ImGui::IsItemHovered();
            // (the transport's buttons lie on the picture: a click on them is not a click into the picture,
            //  else Pause would pause here and its button play on at once)
            if (io.MousePos.y < p0.y + ImGui::GetFrameHeight() + 16) hov = false;
            dl->PushClipRect(p0, ImVec2(p0.x + av.x, p0.y + av.y), true);
            const vx_line *l; int n = cur_lines(&l);
            if (g_running) draw_picture(dl, l, n, now);
            else dl->AddText(ImVec2(p0.x + 20, p0.y + 20), col(160, 170, 200), "Pick a game on the left, or connect to the cabinet (Cabinet > Connect).");
            // the mouse: wheel zoom, right / middle drag move, left click / box pick
            ImVec2 m = io.MousePos;
            if (hov && io.MouseWheel != 0) {
                float z0 = V.zoom;
                V.zoom = std::max(1.0f, std::min(12.0f, V.zoom * (io.MouseWheel > 0 ? 1.2f : 1 / 1.2f)));
                ImVec2 c = ImVec2(p0.x + av.x * 0.5f + V.pan.x, p0.y + av.y * 0.5f + V.pan.y);
                V.pan.x += (m.x - c.x) * (1 - V.zoom / z0); V.pan.y += (m.y - c.y) * (1 - V.zoom / z0);
                if (V.zoom == 1) V.pan = ImVec2(0, 0);
            }
            if (!typing && ImGui::IsKeyPressed(ImGuiKey_Home)) { V.zoom = 1; V.pan = ImVec2(0, 0); }
            if (hov && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) panning = true;
            if (panning) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) { V.pan.x += io.MouseDelta.x; V.pan.y += io.MouseDelta.y; }
                else panning = false;
            }
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_running) {
                if (!g_paused) { g_paused = true; editor_cut(); }
                dragging = true; drag0 = m;
            }
            if (dragging && g_running && g_ed_ok) {
                bool box = fabsf(m.x - drag0.x) > 5 || fabsf(m.y - drag0.y) > 5;
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (box) {
                        dl->AddRect(drag0, m, col(255, 255, 255, 200), 0, 0, 1.5f);
                        dl->AddRectFilled(drag0, m, col(255, 255, 255, 25));
                    }
                } else {
                    dragging = false;
                    int ax, ay, bx, by;
                    V.from(drag0, &ax, &ay); V.from(m, &bx, &by);
                    bool more = io.KeyShift || io.KeyCtrl;   // (Shift or Ctrl: add to / take from the picked lines)
                    if (more && g_ed->ncand == 0 && g_ed->nmsel == 0) more = false;   // (nothing picked yet: a first pick)
                    if (!box) {                 // (the click: the line nearest within 12 px, the core's own test after)
                        int i = line_at(l, n, m, 12.0f);
                        if (i >= 0) {
                            float x0, y0, x1, y1;
                            vx_map(&g_cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
                            vx_map(&g_cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
                            ax = bx = (int)((x0 + x1) / 2); ay = by = (int)((y0 + y1) / 2);
                            vx_ce_mouse_pick(g_ed, g_colors, &g_cfg, l, n, ax, ay, bx, by, 0, more);
                            g_ed->panel = 1;
                        } else if (!more) unpick();
                    } else {
                        vx_ce_mouse_pick(g_ed, g_colors, &g_cfg, l, n, ax, ay, bx, by, 1, more);
                        g_ed->panel = 1;
                    }
                    if (g_ed->said[0]) say(g_ed->said);
                }
            } else if (dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
            // the line under the mouse
            if (hov && !dragging && g_paused && g_ed_ok) {
                int i = line_at(l, n, m, 12.0f);
                if (i >= 0) {
                    float x0, y0, x1, y1;
                    vx_map(&g_cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
                    vx_map(&g_cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
                    dl->AddLine(V.to(x0, y0), V.to(x1, y1), col(120, 200, 255, 200), 3.0f);
                    const vx_rule *u = vx_colors_find(g_colors, &l[i]);
                    ImGui::SetTooltip("Click: pick this line's shape\nDrag: pick the lines in a box\nShift+click: add this line (again: take it away)\n\n%s",
                                      u ? (std::string("Coloured by rule ") + u->name).c_str() : "No rule colours it yet");
                }
            }
            dl->PopClipRect();
            // the transport: play / pause / one frame
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 10, p0.y + 10));
            transport_buttons();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0, top + H - status_h)); ImGui::SetNextWindowSize(ImVec2(W, status_h));
        ImGui::Begin("status", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        {
            std::lock_guard<std::mutex> g(g_mx);
            ImGui::TextColored(C.connected ? ImVec4(0.5f, 1, 0.6f, 1) : ImVec4(0.6f, 0.6f, 0.7f, 1), "%s", C.connected ? "CABINET CONNECTED" : "cabinet not connected");
        }
        ImGui::SameLine(0, 30);
        if (J.state == 1) ImGui::Text("%s ...", J.what.c_str());
        else if (now - g_msg_t < 8) ImGui::TextUnformatted(g_msg.c_str());
        else ImGui::TextDisabled("Hover any control for help. Help menu: the steps and keys.");
        ImGui::End();

        connect_window();
        controls_window();
        sync_window();
        leave_popup();

        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 12, 14, 20, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        // (development: the window as a picture: --shot at its frame, then leave; with VX_STUDIO_SHOTS=DIR
        //  half a second after every left click, DIR/click-N.bmp: screen grabbers see a GPU window black)
        static const char *shots = getenv("VX_STUDIO_SHOTS");
        static double click_shot_at = 0; static int click_n = 0;
        if (shots && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) click_shot_at = now + 0.5;
        bool click_shot = shots && click_shot_at > 0 && now >= click_shot_at;
        if ((want_shot && shot) || click_shot) {
            int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            if (click_shot) {
                SDL_SaveBMP(s, (std::string(shots) + "/click-" + std::to_string(++click_n) + ".bmp").c_str());
                click_shot_at = 0;
            } else { SDL_SaveBMP(s, shot); quit = true; }
            SDL_FreeSurface(s);
        }
        SDL_RenderPresent(ren);
        if (shot && g_paused && frame >= shot_at) want_shot = true;
    }
    if (J.th.joinable()) J.th.join();
    studio_save();
    if (g_running) vx_close();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
