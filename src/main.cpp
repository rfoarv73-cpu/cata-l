#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCScene.hpp>
#include <imgui-cocos.hpp>
#include <fstream>

using namespace geode::prelude;

// ===========================================================================
// Showcase Bot / "catalyst"-style menu
// Macro record/playback core + a Dear ImGui interface matching the reference.
// ===========================================================================

enum class BotState { Off, Recording, Playing };

struct InputEvent {
    int  frame;    // physics-frame index since the last level reset
    int  button;   // PlayerButton: 1 = Jump, 2 = Left, 3 = Right
    bool player1;  // p1 / p2 (dual mode)
    bool hold;     // true = press, false = release
};

// --- macro core state ------------------------------------------------------
static BotState                g_state     = BotState::Off;
static std::vector<InputEvent> g_macro;
static int                     g_frame     = 0;
static size_t                  g_playIdx   = 0;
static bool                    g_injecting = false;

// Practice "commit/rollback" stack: each checkpoint is a savepoint of the macro.
struct RestorePoint { size_t macroSize; int frame; };
static std::vector<RestorePoint> g_checkpoints;

// --- UI-bound settings -----------------------------------------------------
static int   g_tps          = 240;
static bool  g_blockLive     = true;
static bool  g_frameAdvance  = false;   // disabled in reference (backend = TODO)
static bool  g_intentDeath   = false;   // disabled in reference (backend = TODO)
static bool  g_speedhackOn   = false;   // disabled in reference (backend = TODO)
// Settings > Delta
static bool  g_lockDelta     = true;
static int   g_lockMode      = 0;       // 0 Hybrid, 1 Time, 2 Frame
static int   g_lookahead     = 4;
static bool  g_ssbFix        = false;   // disabled in reference
static bool  g_realtimeLock  = true;
static int   g_batchCap      = 10;
// menu
static bool  g_menuOpen      = false;
static int   g_sidebarTab    = 0;       // 0 Main, 1 Assist, 2 Render, 3 Visuals
static int   g_settingsTab   = 0;       // 0 Delta, 1 Tweaks

// floating launcher orb (screen position, draggable)
static ImVec2 g_orbPos = ImVec2(70.f, 240.f);

// sidebar tab icons (loaded from resources, tinted at draw time)
static CCTexture2D* g_icons[4] = { nullptr, nullptr, nullptr, nullptr };
static bool g_iconsLoaded = false;

static std::filesystem::path macroPath() { return Mod::get()->getSaveDir() / "macro.txt"; }

static void saveMacro() {
    std::ofstream out(macroPath(), std::ios::trunc);
    for (auto& e : g_macro) out << e.frame << ' ' << e.button << ' ' << e.player1 << ' ' << e.hold << '\n';
    Notification::create(fmt::format("Saved {} inputs", g_macro.size()), NotificationIcon::Success)->show();
}
static void loadMacro() {
    g_macro.clear();
    std::ifstream in(macroPath());
    InputEvent e; int p1 = 0, hold = 0;
    while (in >> e.frame >> e.button >> p1 >> hold) { e.player1 = p1 != 0; e.hold = hold != 0; g_macro.push_back(e); }
    Notification::create(fmt::format("Loaded {} inputs", g_macro.size()), NotificationIcon::Success)->show();
}

// ===========================================================================
// Macro hooks
// ===========================================================================

// NOTE: verify signature against your GD version's bindings.
class $modify(GJBaseGameLayer) {
    void handleButton(bool push, int button, bool player1) {
        if (g_state == BotState::Recording && !g_injecting)
            g_macro.push_back({ g_frame, button, player1, push });

        if (g_state == BotState::Playing && !g_injecting && g_blockLive)
            return; // swallow the human's live inputs during playback

        GJBaseGameLayer::handleButton(push, button, player1);
    }
};

class $modify(PlayLayer) {
    // practice: placing a checkpoint = "commit" the macro up to this frame
    CheckpointObject* createCheckpoint() {
        auto cp = PlayLayer::createCheckpoint();
        if (g_state == BotState::Recording)
            g_checkpoints.push_back({ g_macro.size(), g_frame });
        return cp;
    }
    // practice: removing a checkpoint drops its savepoint too
    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        if (g_state == BotState::Recording && !g_checkpoints.empty())
            g_checkpoints.pop_back();
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        // died in practice -> roll the macro back to the last checkpoint
        if (g_state == BotState::Recording && !g_checkpoints.empty()) {
            auto rp = g_checkpoints.back();
            g_macro.resize(rp.macroSize);
            g_frame = rp.frame;
            return;
        }
        // full restart (level start, or death in normal mode)
        g_frame = 0; g_playIdx = 0;
        if (g_state == BotState::Recording) { g_macro.clear(); g_checkpoints.clear(); }
    }
    // v0 frame counter (see notes: replace with a physics-step hook + TPS lock
    // for frame-perfect playback -- that's what the Delta panel drives).
    void update(float dt) {
        PlayLayer::update(dt);
        if (g_state == BotState::Off) return;
        g_frame++;
        if (g_state == BotState::Playing) {
            while (g_playIdx < g_macro.size() && g_macro[g_playIdx].frame <= g_frame) {
                auto& e = g_macro[g_playIdx++];
                g_injecting = true;
                this->handleButton(e.hold, e.button, e.player1);
                g_injecting = false;
            }
            if (g_playIdx >= g_macro.size()) g_state = BotState::Off;
        }
    }
};

// toggle the menu with a key (default: K) -- desktop only.
// on mobile there's no keyboard, so the "cata!" pause-menu button is the entry.
#if !defined(GEODE_IS_ANDROID) && !defined(GEODE_IS_IOS)
class $modify(cocos2d::CCKeyboardDispatcher) {
    // desktop (win/mac) bindings pass a trailing double; android/ios excluded above
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool arr, double idk) {
        if (down && key == cocos2d::KEY_K) { g_menuOpen = !g_menuOpen; return true; }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, arr, idk);
    }
};
#endif

// pause-menu button also toggles the menu (handy on touch)
class $modify(PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = CCMenu::create();
        menu->setID("catalyst-toggle"_spr);
        auto spr = ButtonSprite::create("cata!", "bigFont.fnt", "GJ_button_05.png", .5f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCObject*) { g_menuOpen = !g_menuOpen; });
        menu->addChild(btn);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition(win.width - 70.f, 30.f);
        this->addChild(menu);
    }
};

// ===========================================================================
// ImGui interface
// ===========================================================================

static void applyTheme() {
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 9.f; s.ChildRounding = 8.f; s.FrameRounding = 6.f;
    s.GrabRounding = 6.f;   s.PopupRounding = 6.f; s.ScrollbarRounding = 6.f;
    s.WindowPadding = ImVec2(12, 12); s.FramePadding = ImVec2(9, 5);
    s.ItemSpacing = ImVec2(8, 8); s.WindowBorderSize = 0.f; s.ChildBorderSize = 1.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.094f, 0.098f, 0.110f, 0.98f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.125f, 0.129f, 0.145f, 1.00f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.110f, 0.114f, 0.129f, 1.00f);
    c[ImGuiCol_Border]          = ImVec4(0.216f, 0.220f, 0.243f, 1.00f);
    c[ImGuiCol_Text]            = ImVec4(0.902f, 0.906f, 0.925f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.470f, 0.475f, 0.505f, 1.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.172f, 0.176f, 0.196f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.212f, 0.216f, 0.239f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.239f, 0.243f, 0.267f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.172f, 0.176f, 0.196f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.223f, 0.227f, 0.251f, 1.00f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.259f, 0.263f, 0.290f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.231f, 0.510f, 0.965f, 0.90f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.231f, 0.510f, 0.965f, 0.70f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.231f, 0.510f, 0.965f, 1.00f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.361f, 0.596f, 0.984f, 1.00f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.180f, 0.745f, 0.588f, 1.00f);

    // bump everything up on touch screens so it's usable with a finger
#if defined(GEODE_IS_MOBILE) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    s.ScaleAllSizes(1.8f);
    ImGui::GetIO().FontGlobalScale = 1.8f;
#endif

    // For pixel-exact typography, drop a .ttf into your mod resources and load it:
    // ImGui::GetIO().Fonts->AddFontFromFileTTF(
    //     (Mod::get()->getResourcesDir() / "menu.ttf").string().c_str(), 16.f);
}

// custom iOS-style switch
static bool ToggleSwitch(const char* id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFrameHeight() * 0.85f;
    float w = h * 1.9f, r = h * 0.5f;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool changed = false;
    if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
    ImU32 on  = ImGui::GetColorU32(ImVec4(0.180f, 0.745f, 0.588f, 1.f));
    ImU32 off = ImGui::GetColorU32(ImVec4(0.290f, 0.294f, 0.322f, 1.f));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), *v ? on : off, r);
    float cx = *v ? (p.x + w - r) : (p.x + r);
    dl->AddCircleFilled(ImVec2(cx, p.y + r), r - 2.f, IM_COL32(255, 255, 255, 255));
    return changed;
}

static void rightAlign(float width) {
    ImGui::SameLine();
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
}

static void rowToggle(const char* label, const char* id, bool* v, bool enabled) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    rightAlign(ImGui::GetFrameHeight() * 1.9f);
    ToggleSwitch(id, v);
    if (!enabled) ImGui::EndDisabled();
}

static void drawMainTab() {
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float colW = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    ImVec4 accent(0.231f, 0.510f, 0.965f, 1.f);

    // ---------- Replay ----------
    ImGui::BeginChild("replay", ImVec2(colW, 0), true);
    ImGui::TextUnformatted("Replay");
    ImGui::Separator();

    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Name");
    rightAlign(140); ImGui::SetNextItemWidth(140);
    const char* names[] = { "untitled" }; static int nameIdx = 0;
    ImGui::Combo("##name", &nameIdx, names, 1);

    float bw = (ImGui::GetContentRegionAvail().x - 2 * spacing) / 3.f;
    auto sbtn = [&](const char* l, BotState st) {
        bool a = g_state == st; if (a) ImGui::PushStyleColor(ImGuiCol_Button, accent);
        bool pressed = ImGui::Button(l, ImVec2(bw, 0)); if (a) ImGui::PopStyleColor();
        return pressed;
    };
    if (sbtn("Stop", BotState::Off))         g_state = BotState::Off;
    ImGui::SameLine();
    if (sbtn("Record", BotState::Recording)) { g_state = BotState::Recording; g_macro.clear(); g_checkpoints.clear(); g_frame = 0; }
    ImGui::SameLine();
    if (sbtn("Play", BotState::Playing))     { g_state = BotState::Playing; g_playIdx = 0; g_frame = 0; }

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("TPS");
    rightAlign(90); ImGui::SetNextItemWidth(90); ImGui::InputInt("##tps", &g_tps, 0, 0);

    rowToggle("Block live inputs", "##blk", &g_blockLive,    true);
    rowToggle("Frame Advance",     "##fa",  &g_frameAdvance, false);
    rowToggle("Intentional death", "##idd", &g_intentDeath,  false);
    rowToggle("Enable speedhack",  "##sh",  &g_speedhackOn,  false);

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::SmallButton("Save")) saveMacro();
    ImGui::SameLine();
    if (ImGui::SmallButton("Load")) loadMacro();
    ImGui::EndChild();

    ImGui::SameLine();

    // ---------- Settings ----------
    ImGui::BeginChild("settings", ImVec2(0, 0), true);
    ImGui::TextUnformatted("Settings");
    ImGui::Separator();

    auto subtab = [&](const char* l, int i) {
        bool a = g_settingsTab == i; if (a) ImGui::PushStyleColor(ImGuiCol_Button, accent);
        if (ImGui::Button(l, ImVec2(84, 0))) g_settingsTab = i;
        if (a) ImGui::PopStyleColor();
    };
    subtab("Delta", 0); ImGui::SameLine(); subtab("Tweaks", 1);
    ImGui::Dummy(ImVec2(0, 4));

    if (g_settingsTab == 0) {
        rowToggle("Lock Delta", "##ld", &g_lockDelta, true);

        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Lock Mode");
        rightAlign(120); ImGui::SetNextItemWidth(120);
        const char* modes[] = { "Hybrid", "Time", "Frame" };
        ImGui::Combo("##lm", &g_lockMode, modes, 3);

        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Lookahead");
        rightAlign(120); ImGui::SetNextItemWidth(120); ImGui::SliderInt("##la", &g_lookahead, 0, 16);

        rowToggle("SSB Fix",       "##ssb", &g_ssbFix,       false);
        rowToggle("Realtime Lock", "##rl",  &g_realtimeLock, true);

        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Batch Cap");
        rightAlign(120); ImGui::SetNextItemWidth(120); ImGui::SliderInt("##bc", &g_batchCap, 1, 60);
    } else {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextDisabled("Tweaks - soon");
    }
    ImGui::EndChild();
}

static CCTexture2D* loadTex(const char* name) {
    // create a temp sprite just to pull its texture into the cache; the
    // CCTexture2D stays alive in CCTextureCache after the sprite is gone.
    auto spr = CCSprite::create(name);
    return spr ? spr->getTexture() : nullptr;
}

static void loadIcons() {
    g_icons[0] = loadTex("main.png"_spr);
    g_icons[1] = loadTex("assist.png"_spr);
    g_icons[2] = loadTex("render.png"_spr);
    g_icons[3] = loadTex("visuals.png"_spr);
    g_iconsLoaded = true;
}

static void sidebarItem(int i, const char* label) {
    float cell = ImGui::GetContentRegionAvail().x;
    float icon = ImGui::GetFrameHeight() * 1.4f;
    float h    = icon + ImGui::GetTextLineHeight() + 10.f;
    ImVec2 s   = ImGui::GetCursorScreenPos();

    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##tab", ImVec2(cell, h))) g_sidebarTab = i;
    ImGui::PopID();

    bool active = (g_sidebarTab == i);
    bool hover  = ImGui::IsItemHovered();
    auto* dl = ImGui::GetWindowDrawList();
    if (active)     dl->AddRectFilled(s, ImVec2(s.x + cell, s.y + h), IM_COL32(59, 130, 246, 80), 6.f);
    else if (hover) dl->AddRectFilled(s, ImVec2(s.x + cell, s.y + h), IM_COL32(255, 255, 255, 18), 6.f);

    ImU32 tint = active ? IM_COL32(150, 190, 255, 255) : IM_COL32(175, 178, 185, 255);
    ImVec2 ic(s.x + (cell - icon) * 0.5f, s.y + 5.f);
    if (g_icons[i]) // cocos textures are flipped vertically -> flip V in the UVs
        dl->AddImage((ImTextureID)(intptr_t)g_icons[i]->m_uName,
                     ic, ImVec2(ic.x + icon, ic.y + icon), ImVec2(0, 1), ImVec2(1, 0), tint);

    ImVec2 ts = ImGui::CalcTextSize(label);
    ImU32 txt = active ? IM_COL32(232, 236, 245, 255) : IM_COL32(150, 152, 158, 255);
    dl->AddText(ImVec2(s.x + (cell - ts.x) * 0.5f, ic.y + icon + 2.f), txt, label);
}

static void drawMenu() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    ImGui::SetNextWindowSize(ImVec2(700, 470), ImGuiCond_FirstUseEver);
#else
    ImGui::SetNextWindowSize(ImVec2(560, 330), ImGuiCond_FirstUseEver);
#endif
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 300), ImVec2(FLT_MAX, FLT_MAX));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("##catalyst", &g_menuOpen, flags)) { ImGui::End(); return; }

    // header row: name (left) + close (right)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.68f, 1.f, 1.f));
    ImGui::TextUnformatted("cata!");
    ImGui::PopStyleColor();
    rightAlign(24.f);
    if (ImGui::SmallButton("x")) g_menuOpen = false;
    ImGui::Separator();

    // sidebar
    if (!g_iconsLoaded) loadIcons();
    ImGui::BeginChild("sidebar", ImVec2(110, 0), true);
    const char* tabs[] = { "Main", "Assist", "Render", "Visuals" };
    for (int i = 0; i < 4; i++) sidebarItem(i, tabs[i]);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("content", ImVec2(0, 0), false);
    if (g_sidebarTab == 0) drawMainTab();
    else ImGui::TextDisabled("%s - soon", tabs[g_sidebarTab]);
    ImGui::EndChild();

    ImGui::End();
}

static void drawOrb() {
    ImGuiIO& io = ImGui::GetIO();
    float R = 27.f;
#if defined(GEODE_IS_MOBILE) || defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    R = 42.f;
#endif

    ImGui::SetNextWindowPos(g_orbPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(R * 2, R * 2), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##cata-orb", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(p.x + R, p.y + R);

    ImGui::InvisibleButton("##orbbtn", ImVec2(R * 2, R * 2));
    bool hovered = ImGui::IsItemHovered();

    // tap = open, drag = move (small movement still counts as a tap)
    static bool moved = false; static ImVec2 start;
    if (ImGui::IsItemActivated()) { moved = false; start = io.MousePos; }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        g_orbPos.x += io.MouseDelta.x;
        g_orbPos.y += io.MouseDelta.y;
        if (fabsf(io.MousePos.x - start.x) + fabsf(io.MousePos.y - start.y) > 6.f) moved = true;
    }
    if (ImGui::IsItemDeactivated() && !moved) g_menuOpen = !g_menuOpen;

    // keep it on screen
    if (g_orbPos.x < 0) g_orbPos.x = 0;
    if (g_orbPos.y < 0) g_orbPos.y = 0;
    if (g_orbPos.x > io.DisplaySize.x - R * 2) g_orbPos.x = io.DisplaySize.x - R * 2;
    if (g_orbPos.y > io.DisplaySize.y - R * 2) g_orbPos.y = io.DisplaySize.y - R * 2;

    // dark-blue translucent fill, brighter outline, text = outline color but more opaque
    ImU32 fill    = hovered ? IM_COL32(40, 105, 190, 110) : IM_COL32(30, 85, 160, 85);
    ImU32 outline = IM_COL32(95, 165, 240, 150);
    ImU32 textCol = IM_COL32(95, 165, 240, 225);

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, R, fill, 48);
    dl->AddCircle(center, R, outline, 48, 2.5f);

    const char* label = "cata!";
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), textCol, label);

    ImGui::End();
    ImGui::PopStyleVar();
}

// gd-imgui-cocos renders through a plain cocos node that lives in the running
// scene (only one may exist at a time, since ImGui uses a single global
// context). We keep one persistent node and move it into each new scene so the
// launcher orb / menu stay visible across the whole game.
static ImGuiNode* g_imgui = nullptr;

static void ensureImGui(CCScene* scene) {
    if (!scene) return;

    if (!g_imgui) {
        g_imgui = ImGuiNode::create([] {
            drawOrb();
            if (g_menuOpen) drawMenu();
        });
        if (!g_imgui) return;
        g_imgui->retain();   // survive scene changes; context lives with it
        applyTheme();        // ImGui context was (re)created inside create()
    }

    if (g_imgui->getParent() != scene) {
        g_imgui->removeFromParentAndCleanup(false);
        g_imgui->setZOrder(1000);
        scene->addChild(g_imgui);
    }
}

class $modify(CCScene) {
    static CCScene* create() {
        auto scene = CCScene::create();
        ensureImGui(scene);
        return scene;
    }
};
