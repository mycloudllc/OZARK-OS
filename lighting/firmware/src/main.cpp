// Ozark lighting controller: three addressable LED outputs (interior front,
// interior rear, exterior) driven from the head unit over serial.
// Protocol: ../PROTOCOL.md

#include <Arduino.h>
#include <Preferences.h>

#include "zone.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {
constexpr uint32_t kBaud = 460800;
constexpr uint32_t kFrameMs = 16;  // ~60 fps
constexpr size_t kMaxLine = 256;
constexpr uint32_t kStoreVersion = 3;  // bump when StripConfig/Look change

// GPIOs a zone may use: outputs that don't affect flash (ESP32-WROOM; on
// WROVER modules 16 and 17 belong to the PSRAM). GPIO 2 is a strapping pin
// (only matters when flashing over USB, and an LED strip's input doesn't pull
// it high) and drives the blue LED on most dev boards; WLED uses it by default.
constexpr uint8_t kPins[] = {2, 4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};

const StripConfig kDefaults[3] = {
    {16, 60, 2, false, false, false},  // F: interior front, GRB
    {17, 60, 2, false, false, false},  // R: interior rear
    {18, 60, 2, false, false, false},  // E: exterior
};

Zone zones[3] = {Zone('F', kDefaults[0]), Zone('R', kDefaults[1]), Zone('E', kDefaults[2])};
uint8_t max_bri = 200;

StripConfig staged[3];
uint8_t staged_max_bri = 200;
// The first CFG change refused since the last apply or revert: CFGAPPLY then
// applies nothing, so a rejected value can't leave the others half applied
char staged_error[32] = "";

Music music = {};
Preferences prefs;
char line[kMaxLine];
size_t line_len = 0;
bool discarding = false;  // the line was too long: skip to its end
uint32_t last_frame = 0;

bool valid_pin(long pin);

// ---------------------------------------------------------------- storage

// What flash gave back is usable: the same checks as CFG and CFGAPPLY
bool valid_configs(const StripConfig configs[3])
{
    for (int z = 0; z < 3; z++) {
        const StripConfig &c = configs[z];
        if (!valid_pin(c.pin) || c.count < 1 || c.count > kMaxLeds || c.order >= 6)
            return false;
        for (int other = z + 1; other < 3; other++) {
            if (configs[other].pin == c.pin)
                return false;
        }
    }
    return true;
}

// The looks: "look2" (with the background colour), or the shorter "look" an
// older firmware stored, which gets a black background
bool load_looks(Look looks[3])
{
    if (prefs.getBytes("look2", looks, sizeof(Look) * 3) == sizeof(Look) * 3)
        return true;
    struct LookV1 {
        uint8_t r, g, b, r2, g2, b2, pattern, speed, bri;
    } old[3];
    if (prefs.getBytes("look", old, sizeof(old)) != sizeof(old))
        return false;
    for (int z = 0; z < 3; z++)
        looks[z] = Look{old[z].r, old[z].g, old[z].b, old[z].r2, old[z].g2, old[z].b2,
                        old[z].pattern, old[z].speed, old[z].bri, 0, 0, 0};
    return true;
}

void load()
{
    prefs.begin("ozark-light", true);
    if (prefs.getUInt("version", 0) == kStoreVersion) {
        StripConfig configs[3];
        Look looks[3];
        if (prefs.getBytes("config", configs, sizeof(configs)) == sizeof(configs) && load_looks(looks)) {
            // Anything out of range (a damaged store) keeps the defaults
            if (valid_configs(configs)) {
                for (int z = 0; z < 3; z++)
                    zones[z].config = configs[z];
            }
            for (int z = 0; z < 3; z++) {
                if (looks[z].pattern < PAT_COUNT)
                    zones[z].look = looks[z];
            }
            const uint8_t stored_max_bri = prefs.getUChar("maxbri", max_bri);
            if (stored_max_bri >= 1)
                max_bri = stored_max_bri;
        }
    }
    prefs.end();
}

void save()
{
    StripConfig configs[3];
    Look looks[3];
    for (int z = 0; z < 3; z++) {
        configs[z] = zones[z].config;
        looks[z] = zones[z].look;
    }
    prefs.begin("ozark-light", false);
    prefs.putUInt("version", kStoreVersion);
    prefs.putBytes("config", configs, sizeof(configs));
    prefs.putBytes("look2", looks, sizeof(looks));
    prefs.remove("look");  // the older layout, superseded by look2
    prefs.putUChar("maxbri", max_bri);
    prefs.end();
}

// ---------------------------------------------------------------- parsing

int zone_index(char z)
{
    switch (z) {
        case 'F': return 0;
        case 'R': return 1;
        case 'E': return 2;
        default: return -1;
    }
}

// Zones a look command applies to: one, interior (F+R) or all
bool look_targets(const char *arg, bool targets[3])
{
    if (!arg || !arg[0] || arg[1])
        return false;
    targets[0] = targets[1] = targets[2] = false;
    switch (arg[0]) {
        case 'I': targets[0] = targets[1] = true; return true;
        case 'A': targets[0] = targets[1] = targets[2] = true; return true;
        default: {
            const int z = zone_index(arg[0]);
            if (z < 0)
                return false;
            targets[z] = true;
            return true;
        }
    }
}

bool to_byte(const char *s, uint8_t &out)
{
    if (!s || !*s)
        return false;
    char *end;
    const long v = strtol(s, &end, 10);
    if (*end || v < 0 || v > 255)
        return false;
    out = uint8_t(v);
    return true;
}

bool valid_pin(long pin)
{
    for (uint8_t p : kPins) {
        if (p == pin)
            return true;
    }
    return false;
}

void reply(const char *text)
{
    Serial.print(text);
    Serial.print('\n');
}

void print_config(char name, const StripConfig &c)
{
    Serial.printf("CFG %c pin=%u count=%u order=%s rgbw=%u khz=%u reverse=%u\n", name, c.pin, c.count,
                  kOrderNames[c.order], c.rgbw, c.khz400 ? 400 : 800, c.reverse);
}

// ---------------------------------------------------------------- commands

void cmd_look(const char *cmd, char **args, int argc)
{
    bool targets[3];
    if (argc < 2 || !look_targets(args[1], targets))
        return reply("ERR zone");

    const bool color = !strcmp(cmd, "COLOR") || !strcmp(cmd, "COLOR2") || !strcmp(cmd, "COLOR3");
    if (color) {
        uint8_t r, g, b;
        if (argc != 5 || !to_byte(args[2], r) || !to_byte(args[3], g) || !to_byte(args[4], b))
            return reply("ERR value");
        for (int z = 0; z < 3; z++) {
            if (!targets[z])
                continue;
            Look &l = zones[z].look;
            if (cmd[5] == '2') {
                l.r2 = r; l.g2 = g; l.b2 = b;
            }
            else if (cmd[5] == '3') {  // background
                l.r3 = r; l.g3 = g; l.b3 = b;
            }
            else {
                l.r = r; l.g = g; l.b = b;
            }
        }
        return reply("OK");
    }

    uint8_t v;
    if (argc != 3 || !to_byte(args[2], v))
        return reply("ERR value");
    if (!strcmp(cmd, "PAT") && v >= PAT_COUNT)
        return reply("ERR pattern");
    for (int z = 0; z < 3; z++) {
        if (!targets[z])
            continue;
        Look &l = zones[z].look;
        if (!strcmp(cmd, "PAT"))
            l.pattern = v;
        else if (!strcmp(cmd, "SPEED"))
            l.speed = v;
        else
            l.bri = v;
    }
    reply("OK");
}

void cmd_aud(char **args, int argc)
{
    // No answer: frames arrive ~43 times a second
    if (argc != kBands + 2)
        return;
    for (int b = 0; b < kBands; b++) {
        uint8_t v;
        if (!to_byte(args[1 + b], v))
            return;
        music.bands[b] = v;
    }
    if (args[kBands + 1][0] == '1')
        music.beat = true;
    music.last_ms = millis();
}

// Refuse a CFG change, and remember it for CFGAPPLY
void cfg_error(const char *text)
{
    if (!staged_error[0])
        strlcpy(staged_error, text, sizeof(staged_error));
    reply(text);
}

void cmd_cfg(char **args, int argc)
{
    if (argc != 4)
        return cfg_error("ERR args");
    const char zone = args[1][0];
    const char *key = args[2];
    const char *value = args[3];
    char *end;
    const long v = strtol(value, &end, 10);
    const bool number = *end == '\0';

    if (zone == 'G' && !args[1][1]) {
        if (strcmp(key, "MAXBRI") || !number || v < 1 || v > 255)
            return cfg_error("ERR value");
        staged_max_bri = v;
        return reply("OK");
    }

    const int z = zone_index(zone);
    if (z < 0 || args[1][1])
        return cfg_error("ERR zone");
    StripConfig &c = staged[z];

    if (!strcmp(key, "PIN")) {
        if (!number || !valid_pin(v))
            return cfg_error("ERR pin");
        c.pin = v;
    }
    else if (!strcmp(key, "COUNT")) {
        if (!number || v < 1 || v > kMaxLeds)
            return cfg_error("ERR count");
        c.count = v;
    }
    else if (!strcmp(key, "ORDER")) {
        int found = -1;
        for (int i = 0; i < 6; i++) {
            if (!strcmp(value, kOrderNames[i]))
                found = i;
        }
        if (found < 0)
            return cfg_error("ERR order");
        c.order = found;
    }
    else if (!strcmp(key, "RGBW") || !strcmp(key, "REVERSE")) {
        if (!number || (v != 0 && v != 1))
            return cfg_error("ERR value");
        (key[0] == 'R' && key[1] == 'G' ? c.rgbw : c.reverse) = v;
    }
    else if (!strcmp(key, "KHZ")) {
        if (!number || (v != 800 && v != 400))
            return cfg_error("ERR khz");
        c.khz400 = v == 400;
    }
    else {
        return cfg_error("ERR key");
    }
    reply("OK");
}

// Staged = applied config, nothing refused
void stage_current()
{
    for (int z = 0; z < 3; z++)
        staged[z] = zones[z].config;
    staged_max_bri = max_bri;
    staged_error[0] = '\0';
}

// All or nothing. Either way the staged changes are gone afterwards, so a
// refused or abandoned edit can't be applied later with an unrelated one.
void cmd_cfgapply()
{
    if (staged_error[0]) {
        Serial.printf("ERR not applied, a change was refused (%s)\n", staged_error);
        return stage_current();
    }
    for (int a = 0; a < 3; a++) {
        for (int b = a + 1; b < 3; b++) {
            if (staged[a].pin == staged[b].pin) {
                reply("ERR pins must differ");
                return stage_current();
            }
        }
    }
    for (int z = 0; z < 3; z++)
        zones[z].apply(staged[z]);
    max_bri = staged_max_bri;
    save();
    stage_current();
    reply("OK APPLIED");
}

void handle(char *text)
{
    // Split into at most 12 space-separated words
    char *args[12];
    int argc = 0;
    for (char *tok = strtok(text, " \t"); tok && argc < 12; tok = strtok(nullptr, " \t"))
        args[argc++] = tok;
    if (argc == 0)
        return;
    const char *cmd = args[0];

    if (!strcmp(cmd, "AUD"))
        return cmd_aud(args, argc);
    if (!strcmp(cmd, "PING"))
        return reply("PONG");
    if (!strcmp(cmd, "COLOR") || !strcmp(cmd, "COLOR2") || !strcmp(cmd, "COLOR3") || !strcmp(cmd, "PAT") ||
        !strcmp(cmd, "SPEED") || !strcmp(cmd, "BRI"))
        return cmd_look(cmd, args, argc);
    if (!strcmp(cmd, "SAVE")) {
        save();
        return reply("OK SAVED");
    }
    if (!strcmp(cmd, "INFO")) {
        Serial.printf("INFO fw=%s max_leds=%d zones=F,R,E pins=", FW_VERSION, kMaxLeds);
        for (size_t i = 0; i < sizeof(kPins); i++)
            Serial.printf(i ? ",%u" : "%u", kPins[i]);
        Serial.print(" orders=RGB,RBG,GRB,GBR,BRG,BGR\n");
        return;
    }
    if (!strcmp(cmd, "CFGGET")) {
        for (int z = 0; z < 3; z++)
            print_config(zones[z].name, zones[z].config);
        Serial.printf("CFG G maxbri=%u\n", max_bri);
        return reply("END");
    }
    if (!strcmp(cmd, "CFG"))
        return cmd_cfg(args, argc);
    if (!strcmp(cmd, "CFGAPPLY"))
        return cmd_cfgapply();
    if (!strcmp(cmd, "CFGREVERT")) {
        stage_current();
        return reply("OK");
    }
    if (!strcmp(cmd, "TEST")) {
        const int z = argc == 2 && !args[1][1] ? zone_index(args[1][0]) : -1;
        if (z < 0)
            return reply("ERR zone");
        zones[z].test(millis());
        return reply("OK");
    }
    if (!strcmp(cmd, "FACTORY")) {
        prefs.begin("ozark-light", false);
        prefs.clear();
        prefs.end();
        reply("OK");
        Serial.flush();
        ESP.restart();
        return;
    }
    reply("ERR unknown command");
}

void read_serial()
{
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\n') {
            if (discarding) {
                discarding = false;  // end of the runaway line
                reply("ERR line too long");
            }
            else {
                line[line_len] = '\0';
                if (line_len && line[line_len - 1] == '\r')
                    line[line_len - 1] = '\0';
                handle(line);
            }
            line_len = 0;
        }
        else if (discarding) {
            continue;
        }
        else if (line_len < kMaxLine - 1) {
            line[line_len++] = c;
        }
        else {
            // Runaway line: drop all of it, not just its start (the rest would
            // otherwise run as a command of its own)
            line_len = 0;
            discarding = true;
        }
    }
}
}  // namespace

void setup()
{
    // Room for a burst of commands while a frame is being sent to the strips
    Serial.setRxBufferSize(4096);
    Serial.begin(kBaud);

    load();
    stage_current();
    for (Zone &zone : zones)
        zone.apply(zone.config);

    Serial.printf("READY fw=%s zones=F,R,E\n", FW_VERSION);
}

void loop()
{
    read_serial();

    const uint32_t now = millis();
    if (now - last_frame >= kFrameMs) {
        last_frame = now;
        for (Zone &zone : zones)
            zone.render(now, music, max_bri);
        music.beat = false;
    }
}
