#!/usr/bin/env python3
"""Stand-in TrailCommand hub for UI testing the Dash plugin: the REST endpoints
and /ws live state the plugin reads, with plausible changing values.

    fakehub.py [port]     (default 8080)
"""
import base64
import datetime
import hashlib
import json
import math
import socketserver
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler

START = time.time()
controls = [
    {"id": 1, "name": "Light bar", "on": True, "fuse_a": 20},
    {"id": 2, "name": "Rock lights", "on": False, "fuse_a": 10},
    {"id": 3, "name": "Compressor", "on": False, "fuse_a": 40},
    {"id": 4, "name": "Fridge", "on": True, "fuse_a": 15},
    {"id": 5, "name": "Camp lights", "on": True, "fuse_a": 10},
    {"id": 6, "name": "Inverter", "on": False, "fuse_a": 30},
]
AMPS = {1: 11.2, 2: 3.1, 3: 32.0, 4: 4.4, 5: 2.0, 6: 18.5}


def state():
    t = time.time() - START
    wob = math.sin(t / 7)
    ctl = []
    for c in controls:
        amps = AMPS[c["id"]] * (1 + 0.05 * wob) if c["on"] else 0.0
        ctl.append(dict(c, amps=round(amps, 1), load_pct=int(100 * amps / c["fuse_a"])))
    draw = sum(c["amps"] for c in ctl)
    return {
        "house": {"soc": 78.0 + 2 * wob, "band": "green", "volts": 13.1 + 0.1 * wob, "temp_c": 24.5,
                  "amps": round(9.5 - draw, 1), "watts": round((9.5 - draw) * 13.1), "time_left_h": 31.5, "charging": False},
        "starter": {"soc": 96.0, "band": "green", "volts": 12.7, "temp_c": 21.0, "state": "resting"},
        "power": {"solar_w": 180 + 20 * wob, "alternator_w": 0, "controls_w": round(draw * 13.1), "inverter_w": 0,
                  "in_w": 180 + 20 * wob, "out_w": round(draw * 13.1), "net_w": round(180 + 20 * wob - draw * 13.1)},
        "controls": ctl,
        "tires": [
            {"pos": "front_left", "psi": 34.5, "temp_c": 28, "low": False, "hot": False},
            {"pos": "front_right", "psi": 34.0, "temp_c": 29, "low": False, "hot": False},
            {"pos": "rear_left", "psi": 27.0, "temp_c": 30, "low": True, "hot": False},
            {"pos": "rear_right", "psi": 35.0, "temp_c": 31, "low": False, "hot": False},
            {"pos": "spare", "psi": 36.0, "temp_c": 22, "low": False, "hot": False},
        ],
        "modules": {"victron": {"connected": True}, "control": {"connected": True}},
        "net": {"wifi": {"present": True, "connected": True, "ssid": "CampNet", "bars": 3, "dbm": -61},
                "lte": {"present": True, "data_known": True, "sim_ready": True, "connected": True,
                        "carrier": "T-Mobile", "tech": "LTE", "bars": 2, "dbm": -97}},
        "odometer_mi": 48213,
    }


def settings():
    return {"units_distance": "mi", "units_pressure": "psi", "units_temp": "F", "clock": "12",
            "house_name": "House", "starter_name": "Starter", "controller_limit_a": "60",
            "show_house": "1", "show_starter": "1", "show_vehicle": "1", "show_trailer": "0", "alerts_overview": "1"}


def history(days):
    now = time.time()
    if days:
        today = datetime.date.today()
        return {"days": [{"date": (today - datetime.timedelta(days=i)).isoformat(), "amp_hours": round(40 + 25 * math.sin(i), 1)}
                         for i in range(6, -1, -1)]}
    return {"points": [{"ts": now - i * 900, "soc": round(80 - 10 * math.sin(i / 10), 1)} for i in range(96, -1, -1)]}


def maintenance():
    return {"odometer_mi": 48213, "items": [
        {"id": 1, "name": "Engine oil", "interval_miles": 5000, "last_miles": 44500, "interval_months": 6,
         "last_date": "2026-06-01", "status": "soon", "percent_used": 86},
        {"id": 2, "name": "Tire rotation", "interval_miles": 7500, "last_miles": 39000, "interval_months": None,
         "last_date": None, "status": "due", "percent_used": 123},
        {"id": 3, "name": "Air filter", "interval_miles": 15000, "last_miles": 40000, "interval_months": 12,
         "last_date": "2026-03-10", "status": "ok", "percent_used": 55},
    ]}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def reply(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path
        if p == "/ws":
            return self.websocket()
        if p.startswith("/api/health"):
            return self.reply({"app": "trailcommand", "version": "1.4.0", "build": 214, "hostname": "trailcommand-hub"})
        if p.startswith("/api/state"):
            return self.reply(state())
        if p.startswith("/api/settings"):
            return self.reply(settings())
        if p.startswith("/api/history/daily"):
            return self.reply(history(True))
        if p.startswith("/api/history"):
            return self.reply(history(False))
        if p.startswith("/api/maintenance"):
            return self.reply(maintenance())
        self.reply({"error": "not found"}, 404)

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0) or 0))
        if self.path == "/api/controls/all-off":
            for c in controls:
                c["on"] = False
        self.reply({"ok": True})

    def do_PUT(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0) or 0)) or b"{}")
        if self.path.startswith("/api/controls/"):
            cid = int(self.path.split("/")[3])
            for c in controls:
                if c["id"] == cid:
                    c["on"] = bool(body.get("on", not c["on"]))
        self.reply({"ok": True})

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        try:
            while True:
                data = json.dumps(state()).encode()
                n = len(data)
                head = bytes([0x81, n]) if n < 126 else (bytes([0x81, 126]) + struct.pack(">H", n) if n < 65536 else bytes([0x81, 127]) + struct.pack(">Q", n))
                self.wfile.write(head + data)
                self.wfile.flush()
                time.sleep(1)
        except (BrokenPipeError, ConnectionResetError):
            pass
        self.close_connection = True


class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    with Server(("0.0.0.0", port), Handler) as srv:
        print(f"fake hub on :{port}", flush=True)
        srv.serve_forever()
