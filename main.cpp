#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <sstream>
#include <cstdio>

class Vec2 {
public:
    float x, y;

    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}

    // Scratch: "change x by N" / "change y by N"
    void changeX(float n) { x += n; }
    void changeY(float n) { y += n; }

    // Scratch: "set x to N" / "set y to N"
    void setX(float n) { x = n; }
    void setY(float n) { y = n; }

    // Scratch: "move N steps" (in current direction)
    void move(float steps, float directionDeg) {
        float rad = (directionDeg - 90.0f) * (M_PI / 180.0f);
        x += steps * cosf(rad);
        y += steps * sinf(rad);
    }

    // Distance to another Vec2 — "distance to [sprite]"
    float distanceTo(const Vec2& other) const {
        float dx = other.x - x;
        float dy = other.y - y;
        return sqrtf(dx*dx + dy*dy);
    }

    // Direction toward another point — "point towards [sprite]"
    float directionTo(const Vec2& other) const {
        float dx = other.x - x;
        float dy = other.y - y;
        return atan2f(dx, -dy) * (180.0f / M_PI);
    }

    // Scratch coords -> screen coords (flip Y, offset by center)
    olc::vi2d toScreen(int screenW, int screenH) const {
        return { (int)(x + screenW / 2), (int)(-y + screenH / 2) };
    }

    // Arithmetic ops
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    // Magnitude / normalise
    float length() const { return sqrtf(x*x + y*y); }
    Vec2 normalised() const {
        float l = length();
        return l > 0 ? Vec2{x/l, y/l} : Vec2{0,0};
    }
};

// ----------------------------------------------------------------
// Costume
// ----------------------------------------------------------------
struct Costume {
    std::string name;
    olc::Sprite* src;   // CPU side — for pixel collision sampling
    olc::Decal*  decal; // GPU side — for drawing

    // origin offset (like Scratch's costume centre)
    Vec2 origin;

    Costume(const std::string& name, const std::string& path,
            float ox = 0, float oy = 0)
        : name(name), origin(ox, oy)
    {
        src   = new olc::Sprite(path);
        decal = new olc::Decal(src);
    }

    ~Costume() {
        delete decal;
        delete src;
    }

    int w() const { return src ? src->width  : 0; }
    int h() const { return src ? src->height : 0; }

    // Sample alpha at local pixel coords
    bool opaqueAt(int px, int py) const {
        if (!src || px < 0 || py < 0 || px >= w() || py >= h())
            return false;
        return src->GetPixel(px, py).a > 128;
    }
};

// ----------------------------------------------------------------
// Sprite
// ----------------------------------------------------------------
class Sprite {
public:
    Vec2  pos;
    float direction;
    float size;
    bool  visible;

private:
    std::vector<Costume*> costumes;
    int costumeIndex;

public:
    Sprite()
        : pos(0,0), direction(90.0f), size(100.0f),
          visible(true), costumeIndex(0) {}

    ~Sprite() {
        for (auto* c : costumes) delete c;
    }

    // --- Costumes ---
    void addCostume(Costume* c) {
        costumes.push_back(c);
    }

    void switchCostume(int index) {
        if (index >= 0 && index < (int)costumes.size())
            costumeIndex = index;
    }

    void switchCostume(const std::string& name) {
        for (int i = 0; i < (int)costumes.size(); i++)
            if (costumes[i]->name == name) { costumeIndex = i; return; }
    }

    void nextCostume() {
        if (!costumes.empty())
            costumeIndex = (costumeIndex + 1) % costumes.size();
    }

    Costume* currentCostume() const {
        if (costumes.empty()) return nullptr;
        return costumes[costumeIndex];
    }

    int costumeNumber() const { return costumeIndex; }

    // --- AABB in screen space (used as broad phase) ---
    struct AABB {
        int x, y, w, h; // top-left + size
        bool overlaps(const AABB& o) const {
            return x < o.x+o.w && x+w > o.x &&
                   y < o.y+o.h && y+h > o.y;
        }
    };

    AABB getAABB(int screenW, int screenH) const {
        Costume* c = currentCostume();
        if (!c) return {0,0,0,0};
        float scale = size / 100.0f;
        int sw = (int)(c->w() * scale);
        int sh = (int)(c->h() * scale);
        auto p = pos.toScreen(screenW, screenH);
        int ox = (int)(c->origin.x * scale);
        int oy = (int)(c->origin.y * scale);
        return { p.x - ox, p.y - oy, sw, sh };
    }

    // --- Pixel-perfect collision ---
    bool touchingSprite(const Sprite& other, int screenW, int screenH) const {
        // broad phase
        AABB a = getAABB(screenW, screenH);
        AABB b = other.getAABB(screenW, screenH);
        if (!a.overlaps(b)) return false;

        // narrow phase — iterate overlap region in screen pixels
        int x0 = std::max(a.x, b.x);
        int y0 = std::max(a.y, b.y);
        int x1 = std::min(a.x + a.w, b.x + b.w);
        int y1 = std::min(a.y + a.h, b.y + b.h);

        Costume* ca = currentCostume();
        Costume* cb = other.currentCostume();
        if (!ca || !cb) return false;

        float scaleA = size / 100.0f;
        float scaleB = other.size / 100.0f;

        for (int sy = y0; sy < y1; sy++) {
            for (int sx = x0; sx < x1; sx++) {
                // map screen pixel back to each costume's local space
                int lax = (int)((sx - a.x) / scaleA);
                int lay = (int)((sy - a.y) / scaleA);
                int lbx = (int)((sx - b.x) / scaleB);
                int lby = (int)((sy - b.y) / scaleB);

                if (ca->opaqueAt(lax, lay) && cb->opaqueAt(lbx, lby))
                    return true;
            }
        }
        return false;
    }

    // --- Motion (same as before) ---
    void move(float steps) {
        float rad = (direction - 90.0f) * (float)(M_PI / 180.0);
        pos.x += steps * cosf(rad);
        pos.y += steps * sinf(rad);
    }

    void pointInDirection(float deg) { direction = fmodf(deg, 360.0f); }
    void pointTowards(const Vec2& target) { direction = pos.directionTo(target); }
    void setX(float x) { pos.x = x; }
    void setY(float y) { pos.y = y; }
    void changeX(float n) { pos.x += n; }
    void changeY(float n) { pos.y += n; }
    void goTo(float x, float y) { pos = {x, y}; }
    void goTo(const Vec2& v) { pos = v; }

    void ifOnEdgeBounce(int screenW, int screenH) {
        float hw = screenW / 2.0f, hh = screenH / 2.0f;
        if (pos.x >  hw || pos.x < -hw) direction = 180.0f - direction;
        if (pos.y >  hh || pos.y < -hh) direction = -direction;
    }

    void show() { visible = true; }
    void hide() { visible = false; }
    void setSize(float pct) { size = pct; }
    void changeSize(float n) { size += n; }

    float distanceTo(const Sprite& other) const {
        return pos.distanceTo(other.pos);
    }

    // --- Draw ---
    void draw(olc::PixelGameEngine* pge) const {
        if (!visible) return;
        Costume* c = currentCostume();
        if (!c || !c->decal) return;

        float scale = size / 100.0f;
        auto p = pos.toScreen(pge->ScreenWidth(), pge->ScreenHeight());
        float ox = c->origin.x * scale;
        float oy = c->origin.y * scale;

        pge->DrawDecal(
            { (float)p.x - ox, (float)p.y - oy },
            c->decal,
            { scale, scale }
        );
    }
};

class Game : public olc::PixelGameEngine {
public:
    Sprite player;
    Sprite backdrop;
    Sprite solid;
    Vec2 camera;
    bool stop_all;
    bool consoleactive;
    bool consolelogged;
    std::vector<std::string> cmdline;
    int currentline;
    std::string input;

    Game() : stop_all(false), consoleactive(false), consolelogged(false), currentline(1) {
        sAppName = "Titled Platformer Game 3";
        cmdline = {"Console", "user@tpgthree:~$ "};
    }
    char getkey() {
        for (int i = 0; i < 256; i++) {
            if (GetKey((olc::Key)i).bPressed) {
                std::cout << "key index: " << i << "\n";
            }
        }
        for (char c = 'A'; c <= 'Z'; c++) {
            olc::Key key = (olc::Key)((int)(c - 'A') + (int)olc::Key::A);
            if (GetKey(key).bPressed)
                return GetKey(olc::Key::SHIFT).bHeld ? c : (char)(c + 32);
        }
        for (char c = '0'; c <= '9'; c++) {
            olc::Key key = (olc::Key)((int)(c - '0') + (int)olc::Key::K0);
            if (GetKey(key).bPressed) return c;
        }
        if (GetKey(olc::Key::SPACE).bPressed) return ' ';
        if (GetKey(olc::Key::BACK).bPressed)  return '\b';
        if (GetKey(olc::Key::ENTER).bPressed) return '\n';
        if (GetKey(olc::Key::PERIOD).bPressed)  return GetKey(olc::Key::SHIFT).bHeld ? '>' : '.';
        if (GetKey(olc::Key::COMMA).bPressed)   return GetKey(olc::Key::SHIFT).bHeld ? '<' : ',';
        if (GetKey(olc::Key::EQUALS).bPressed)  return GetKey(olc::Key::SHIFT).bHeld ? '+' : '=';
        if (GetKey(olc::Key::MINUS).bPressed)   return GetKey(olc::Key::SHIFT).bHeld ? '_' : '-';
        if (GetKey(olc::Key::OEM_2).bPressed)   return GetKey(olc::Key::SHIFT).bHeld ? '?' : '/';
        if (GetKey(olc::Key::OEM_1).bPressed)   return GetKey(olc::Key::SHIFT).bHeld ? ':' : ';';
        if (GetKey(olc::Key::BACK).bPressed)    return '\b';
        if (GetKey((olc::Key)94).bPressed)
            return '"';
        if (GetKey(olc::Key::SHIFT).bHeld) {
            if (GetKey(olc::Key::K1).bPressed) return '!';
            if (GetKey(olc::Key::K2).bPressed) return '@';
            if (GetKey(olc::Key::K3).bPressed) return '#';
            if (GetKey(olc::Key::K4).bPressed) return '$';
            if (GetKey(olc::Key::K5).bPressed) return '%';
            if (GetKey(olc::Key::K6).bPressed) return '^';
            if (GetKey(olc::Key::K7).bPressed) return '&';
            if (GetKey(olc::Key::K8).bPressed) return '*';
            if (GetKey(olc::Key::K9).bPressed) return '(';
            if (GetKey(olc::Key::K0).bPressed) return ')';
        }
        return 0;
    }

    void printf(std::string msg, std::vector<std::string> others = {""}) {
        std::vector<std::string> toprint;
        std::string orig = "";
        int i = 1;
        for (char c : msg) {
            if (c == '%') {
                orig += others[i];
            } else {
                orig += c;
            }
        }
        println(orig);
    }

    void println(std::string text) {
        cmdline.push_back(text);
        currentline++;
    }
    void print(std::string text) {
        cmdline[currentline] += text;
    }

    std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> args;
        std::string current;
        bool inQuotes = false;
        for (char c : s) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ' ' && !inQuotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) args.push_back(current);
        return args;
    }
    void handle(std::string full) {
        std::vector<std::string> args = split(full);
        std::string cmd = args[0];
        if (cmd == "whoami") {
            println("user - administrator");
        } else if (cmd == "echo") {
            println(args[1]);
        } else if (cmd == "printf") {
            printf(args[1], std::vector<std::string>(args.begin() + 2, args.end()));
        } else if (cmd == "clear") {
            cmdline = {"Console", ""};
            currentline = 1;
        } else if (cmd == "close") {
            consoleactive = false;
        } else if (cmd == "bash") {
            FILE* pipe = popen(full.substr(5).c_str(), "r");
            char buf[128];
            while (fgets(buf, sizeof(buf), pipe)) {
                println(std::string(buf));
            }
            pclose(pipe);
        }
    }

    bool update(Sprite& player, Sprite& level, float delta) {
        if (GetKey(olc::Key::A).bHeld) camera.x += -1;
        if (GetKey(olc::Key::D).bHeld) camera.x += 1;
        if (GetKey(olc::Key::W).bHeld) camera.y += 1;
        if (GetKey(olc::Key::S).bHeld) camera.y += -1;
        return false;
    }
    bool OnUserCreate() override {
        player.addCostume(new Costume("player", "assets/player.png", 9, 9));
        player.switchCostume("player");
        backdrop.addCostume(new Costume("backdrop", "assets/backdrop.png", 240, 180));
        backdrop.addCostume(new Costume("win", "assets/win.png", 240, 180));
        backdrop.switchCostume("backdrop");
        solid.addCostume(new Costume("1 1", "assets/level/level.png", 366, 279));
        solid.switchCostume("1 1");
        solid.setSize(500);
        return true;
    }
    bool OnUserUpdate(float fElapsedTime) override {
        if (!stop_all) {
            if (GetKey(olc::Key::OEM_3).bPressed && !consoleactive) {
                consoleactive = true;
            }
            if (consoleactive) {
                //draw console
                Clear(olc::BLACK);
                for (int i = 0; i < cmdline.size(); i++) {
                    DrawString(10, i*10, cmdline[i].c_str(), olc::WHITE);
                }
                char key = getkey();
                if (key == '\n') {
                    handle(input);
                    input.clear();
                    println("user@tpgthree:~$ ");
                } else if (key == '\b') {
                    if (!input.empty()) {
                        input.pop_back();
                        cmdline[currentline].pop_back();
                    }
                } else if (key != 0) {
                    input += key;
                    print(std::string(1, key));
                    std::cout << "input: " << input << "\n";
                    std::cout << "key: " << (int)key << "\n";
                }
            } else {
                Clear(olc::BLACK);
                backdrop.draw(this);
                //DRAW
                solid.goTo(0 - camera.x, 0 - camera.y);
                solid.draw(this);
                player.draw(this);
                //UPDATE
                bool status = update(player, solid, fElapsedTime);
                if (status) {
                    backdrop.switchCostume("win");
                    backdrop.draw(this);
                    stop_all = true;
                }
            }
        }
        return true;
    }
};

int main() {
    Game game;
    if (game.Construct(480, 360, 2, 2))
        game.Start();
    return 0;
}