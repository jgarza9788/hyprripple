// Run: make test
#include "../src/Logic.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Logic;

static int failed = 0;
static void ok(const char* name, bool cond) {
    std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond)
        failed++;
}
static bool near(double a, double b, double eps = 1e-3) {
    return std::abs(a - b) < eps;
}

int main() {
    // ── applySetting ─────────────────────────────────────────────────────────
    {
        Settings s;
        ok("bool true", applySetting(s, "on_focus", "false").empty() && !s.onFocus);
        ok("bool rejects junk", !applySetting(s, "on_focus", "maybe").empty());
        ok("float clamps high", applySetting(s, "opacity", "7").empty() && s.opacity == 1.f);
        ok("float clamps low", applySetting(s, "radius", "-5").empty() && s.radius == 4.f);
        ok("float rejects text", !applySetting(s, "radius", "big").empty());
        ok("float rejects trailing junk", !applySetting(s, "radius", "12px").empty());
        ok("float rejects nan", !applySetting(s, "radius", "nan").empty());
        ok("float rejects inf", !applySetting(s, "opacity", "inf").empty());
        ok("int rounds + clamps", applySetting(s, "duration", "99999").empty() && s.duration == 5000);
        ok("max ripples clamps", applySetting(s, "max_ripples", "0").empty() && s.maxRipples == 1);
        ok("enum ok", applySetting(s, "on_click", "any").empty() && s.onClick == "any");
        ok("enum rejects", !applySetting(s, "on_click", "right").empty());
        ok("style fill alias", applySetting(s, "style", "fill").empty() && s.style == "classic");
        ok("style sparkle", applySetting(s, "style", "sparkle").empty() && s.style == "sparkle");
        ok("sparkle amount clamps", applySetting(s, "sparkle_amount", "3").empty() && s.sparkleAmount == 1.f);
        ok("workspace screen", applySetting(s, "workspace_radius", "screen").empty() && s.workspaceRadius == "screen");
        ok("workspace number normalised", applySetting(s, "workspace_radius", "900.6").empty() && s.workspaceRadius == "901");
        ok("workspace rejects negative", !applySetting(s, "workspace_radius", "-3").empty());
        ok("workspace rejects injection", !applySetting(s, "workspace_radius", "1 ; dispatch exit").empty());
        ok("color hex", applySetting(s, "color", "#12abef").empty() && s.color == "#12abef");
        ok("color key", applySetting(s, "color", "selection_background").empty());
        ok("color empty -> accent", applySetting(s, "color", "").empty() && s.color == "accent");
        ok("color rejects quote", !applySetting(s, "color", "a\"b").empty());
        ok("color rejects spaces/semicolon", !applySetting(s, "color", "red; exit").empty());
        ok("exclude globs", applySetting(s, "exclude", "steam_app_*, mpv,  org.gnome.*").empty());
        ok("exclude rejects quote", !applySetting(s, "exclude", "mpv\"").empty());
        ok("exclude rejects too long", !applySetting(s, "exclude", std::string(600, 'a')).empty());
        ok("value too long", !applySetting(s, "color", std::string(300, 'a')).empty());
        ok("unknown key", !applySetting(s, "nope", "1").empty());
    }

    // ── timing ───────────────────────────────────────────────────────────────
    {
        Life tap{.mode = 0, .duration = 1.f, .elapsed = 0.f};
        ok("tap starts small", near(spreadFraction(tap), 0.08f));
        tap.elapsed = 0.7f;
        ok("tap full at 70%", near(spreadFraction(tap), 1.f));
        tap.elapsed = 0.3f;
        ok("tap no fade before 45%", fade(tap) == 0.f);
        tap.elapsed = 0.725f;
        ok("tap half faded mid-way", near(fade(tap), 0.5f));
        tap.elapsed = 1.0f;
        ok("tap finished at 100%", finished(tap));
        ok("tap alpha 0 when finished", alpha(tap, 0.3f) == 0.f);

        Life held{.mode = 0, .duration = 1.f, .elapsed = 3.f, .held = true};
        ok("held: no fade while pressed", fade(held) == 0.f && !finished(held));
        ok("held: full alpha while pressed", near(alpha(held, 0.3f), 0.3f));
        held.releasedAt = 3.f;
        held.elapsed    = 3.f + 0.275f;
        ok("held: fades after release", near(fade(held), 0.5f));
        held.elapsed = 3.f + 0.56f;
        ok("held: finished fadeLen after release", finished(held));

        Life quick{.mode = 0, .duration = 1.f, .elapsed = 0.3f, .held = true, .releasedAt = 0.1f};
        ok("quick release fades on normal schedule", fade(quick) == 0.f);
        quick.elapsed = 1.f;
        ok("quick release finishes normally", finished(quick));

        Life stuck{.mode = 0, .duration = 1.f, .elapsed = MAX_HOLD_S + 1.f, .held = true};
        ok("lost release capped at MAX_HOLD", finished(stuck));

        Life sp{.mode = 2, .duration = 1.f, .elapsed = 0.f};
        ok("sparkle starts at 20%", near(spreadFraction(sp), 0.2f));
        sp.elapsed = 0.75f;
        ok("sparkle full at 75%", near(spreadFraction(sp), 1.f));
        sp.elapsed = 0.5f;
        ok("sparkle not fading at 50%", fade(sp) == 0.f);

        ok("m3 ease endpoints", near(m3Ease(0), 0) && near(m3Ease(1), 1));
        ok("m3 ease front-loaded", m3Ease(0.3f) > 0.6f);
        bool mono = true;
        for (int i = 1; i <= 100; ++i)
            mono &= m3Ease(i / 100.f) >= m3Ease((i - 1) / 100.f) - 1e-5f;
        ok("m3 ease monotonic", mono);

        Life zero{.mode = 0, .duration = 0.f, .elapsed = 1.f};
        ok("zero duration is safe", finished(zero) && std::isfinite(spreadFraction(zero)));
    }

    // ── sampling presets ─────────────────────────────────────────────────────
    {
        Settings s;
        ok("negative style", applySetting(s, "style", "negative").empty() && styleMode(s.style) == MODE_NEGATIVE);
        ok("water style", applySetting(s, "style", "water").empty() && styleMode(s.style) == MODE_WATER);
        ok("prism style", applySetting(s, "style", "prism").empty() && styleMode(s.style) == MODE_PRISM);
        ok("distortion clamps", applySetting(s, "distortion", "9").empty() && s.distortion == 1.f);
        ok("distortion rejects text", !applySetting(s, "distortion", "lots").empty());
        ok("sampling modes", isSamplingMode(3) && isSamplingMode(4) && isSamplingMode(5) && !isSamplingMode(0) && !isSamplingMode(2));
        ok("wave modes", isWaveMode(4) && isWaveMode(5) && !isWaveMode(3));
        ok("no reach for non-waves", waveReach(0, 200, 1) == 0.f && waveReach(3, 200, 1) == 0.f);
        ok("reach grows with distortion", waveReach(4, 200, 1) > waveReach(4, 200, 0.2f) && waveReach(4, 200, 0) == 0.f);
        ok("reach capped", waveReach(5, 1e6f, 1) <= 48.f * 2.0f + 1e-3f);

        bool  mono = true;
        float prev = -1;
        for (int i = 0; i <= 100; ++i) {
            Life w{.mode = MODE_WATER, .duration = 1.f, .elapsed = i / 100.f};
            const float f = spreadFraction(w);
            mono &= f >= prev - 1e-6f;
            prev = f;
        }
        ok("water spreads monotonically", mono);
        Life w0{.mode = MODE_WATER, .duration = 1.f, .elapsed = 0.f};
        Life w1{.mode = MODE_WATER, .duration = 1.f, .elapsed = 1.f};
        ok("water starts small, reaches full at the end", spreadFraction(w0) < 0.05f && near(spreadFraction(w1), 1.f));
        Life wMid{.mode = MODE_WATER, .duration = 1.f, .elapsed = 0.7f};
        ok("water still travelling at 70%", spreadFraction(wMid) < 0.95f);
        Life wFade{.mode = MODE_WATER, .duration = 1.f, .elapsed = 0.3f};
        ok("water fade starts early (20%)", fade(wFade) > 0.f);
        // gentle: alpha never drops more than 4% of opacity per 1% of life
        bool gentle = true;
        for (int i = 1; i <= 100; ++i) {
            Life a{.mode = MODE_WATER, .duration = 1.f, .elapsed = (i - 1) / 100.f};
            Life b{.mode = MODE_WATER, .duration = 1.f, .elapsed = i / 100.f};
            if (i > 6) // after the fade-in
                gentle &= alpha(a, 1.f) - alpha(b, 1.f) <= 0.04f;
        }
        ok("water fade is gentle", gentle);
        Life cl{.mode = 0, .duration = 1.f, .elapsed = 0.7f};
        ok("classic timing unchanged", near(spreadFraction(cl), 1.f));
        Life wEnd{.mode = MODE_WATER, .duration = 1.f, .elapsed = 1.01f};
        ok("water finishes", finished(wEnd));
    }

    // ── geometry ─────────────────────────────────────────────────────────────
    {
        const Rect r{100, 100, 200, 100};
        ok("cover from corner", near(coverRadius({100, 100}, r), std::sqrt(200.0 * 200 + 100 * 100)));
        ok("cover from center", near(coverRadius({200, 150}, r), std::sqrt(100.0 * 100 + 50 * 50)));
        auto e = entryPoint(r, {103, 150});
        ok("entry left edge", near(e.x, 100) && near(e.y, 150));
        e = entryPoint(r, {297, 120});
        ok("entry right edge", near(e.x, 300) && near(e.y, 120));
        e = entryPoint(r, {180, 102});
        ok("entry top edge", near(e.x, 180) && near(e.y, 100));
        e = entryPoint(r, {180, 199});
        ok("entry bottom edge", near(e.x, 180) && near(e.y, 200));
        e = entryPoint(r, {50, 150});
        ok("entry from outside clamps", near(e.x, 100) && near(e.y, 150));
    }

    // ── class matching ───────────────────────────────────────────────────────
    {
        ok("glob exact", globMatch("mpv", "mpv"));
        ok("glob case-insensitive", globMatch("MPV", "mpv"));
        ok("glob star", globMatch("steam_app_*", "steam_app_1234"));
        ok("glob star middle", globMatch("org.*.Nautilus", "org.gnome.Nautilus"));
        ok("glob question", globMatch("mp?", "mpv"));
        ok("glob no partial", !globMatch("mpv", "mpvx"));
        ok("glob empty text", !globMatch("a", "") && globMatch("*", ""));
        // pathological pattern must return quickly
        ok("glob pathological", !globMatch("*a*a*a*a*a*a*a*a*b", std::string(5000, 'a')));
        ok("list match", classExcluded("firefox, steam_app_*,mpv", "steam_app_42"));
        ok("list trims", classExcluded("  mpv  ", "mpv"));
        ok("list miss", !classExcluded("firefox,mpv", "kitty"));
        ok("list empty", !classExcluded("", "kitty") && !classExcluded(",,", "kitty"));
        ok("empty class never excluded", !classExcluded("*", ""));
    }

    // ── rate limiter ─────────────────────────────────────────────────────────
    {
        RateLimiter rl;
        int         allowed = 0;
        for (int i = 0; i < 100; ++i)
            allowed += rl.allow(0.f);
        ok("burst caps instant spam", allowed == 10);
        ok("empty bucket refuses", !rl.allow(0.01f));
        ok("refills over time", rl.allow(0.2f) && rl.allow(0.2f));
        RateLimiter steady;
        int         n = 0;
        for (int i = 0; i < 1000; ++i)
            n += steady.allow(i * 0.001f); // 1000 calls over 1 s
        ok("sustained rate ~ burst + perSecond", n >= 28 && n <= 31);
        RateLimiter back;
        back.allow(5.f);
        ok("clock going backwards is safe", back.allow(1.f));
    }

    // ── colors / json ────────────────────────────────────────────────────────
    {
        auto c = parseColorLiteral("#ff8000");
        ok("hex6", c && near((*c)[0], 1) && near((*c)[1], 128 / 255.0) && near((*c)[3], 1));
        c = parseColorLiteral("#ff800080");
        ok("hex8 alpha", c && near((*c)[3], 128 / 255.0));
        c = parseColorLiteral("#f80");
        ok("hex3", c && near((*c)[1], 136 / 255.0));
        c = parseColorLiteral("rgba(ff000080)");
        ok("rgba()", c && near((*c)[0], 1) && near((*c)[3], 128 / 255.0));
        c = parseColorLiteral("0x80ff0000");
        ok("0xaarrggbb", c && near((*c)[0], 1) && near((*c)[3], 128 / 255.0));
        ok("rejects name", !parseColorLiteral("accent"));
        ok("rejects bad hex", !parseColorLiteral("#ggg"));
        ok("rejects wrong length", !parseColorLiteral("#12345"));
        ok("color key ok", isColorKey("color4") && isColorKey("selection_background"));
        ok("color key rejects", !isColorKey("4color") && !isColorKey("a-b") && !isColorKey(""));
        ok("json escapes", jsonEscape("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001");
    }

    std::printf(failed ? "\n%d failed\n" : "\nall passed\n", failed);
    return failed ? 1 : 0;
}
