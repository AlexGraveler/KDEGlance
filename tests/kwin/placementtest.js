// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
//
// Runs src/kwin/placement.js against a small simulation of KWin's scripting
// API (workspace, windows, signals) and checks the placement behaviour.
// usage: node placementtest.js path/to/placement.js
"use strict";
const fs = require("fs");
const vm = require("vm");
const assert = require("assert");

function signal() {
    const handlers = [];
    const s = (...args) => handlers.slice().forEach((h) => h(...args));
    s.connect = (h) => handlers.push(h);
    return s;
}

class Output {
    constructor(name, area) {
        this.name = name;
        this.area = area;
        this.devicePixelRatio = 1;
    }
}

class Window {
    constructor(env, appId, geometry) {
        this.env = env;
        this.desktopFileName = appId;
        this.resourceClass = appId;
        this.normalWindow = true;
        const id=appId + ":" + Window.counter++;
        this.internalId = { toString: () => id };
        this._geometry = Object.assign({}, geometry);
        this.move = false;
        this.resize = false;
        this.fullScreen = false;
        this.deleted = false;
        this.minimized = false;
        this.skipTaskbar = false;
        this.skipPager = false;
        this.skipSwitcher = false;
        this.output = env.workspace.activeScreen;
        this.captionChanged = signal();
        this.frameGeometryChanged = signal();
        this.interactiveMoveResizeStarted = signal();
        this.interactiveMoveResizeFinished = signal();
        this.closed = signal();
    }
    get frameGeometry() {
        if(this.env.liveGeometry) {
            const window=this;
            return {get x(){return window._geometry.x},get y(){return window._geometry.y},
                get width(){return window._geometry.width},get height(){return window._geometry.height}};
        }
        return Object.assign({}, this._geometry);
    }
    set frameGeometry(g) {
        this.env.geometryWrites++;
        this._geometry = { x: g.x, y: g.y, width: g.width, height: g.height };
        this.frameGeometryChanged();
    }
    // A client-side size change (KWin keeps the top-left corner here, the
    // worst case for the script).
    clientResize(width, height) {
        this._geometry = { x: this._geometry.x+(this.env.centerOnResize?(this._geometry.width-width)/2:0),
            y: this.env.centerOnResize?Math.max(0,this._geometry.y+(this._geometry.height-height)/2):this._geometry.y,width,height };
        this.frameGeometryChanged();
    }
    userMove(x, y) {
        this.interactiveMoveResizeStarted();
        this.move = true;
        this._geometry = Object.assign(this._geometry, { x, y });
        this.frameGeometryChanged();
        this.move = false;
        this.interactiveMoveResizeFinished();
    }
    close() {
        this.deleted = true;
        this.closed();
        this.env.workspace.windowRemoved(this);
    }
}
Window.counter = 0;

function makeEnv() {
    const outputs = [new Output("DP-1", { x: 0, y: 0, width: 1920, height: 1040 }), new Output("HDMI-A-1", { x: 1920, y: 0, width: 1280, height: 1024 })];
    const env = { calls: [], geometryWrites: 0, view: "media", pendingReplies: [] };
    let active = null;
    const workspace = {
        screens: outputs,
        activeScreen: outputs[0],
        currentDesktop: {},
        windowAdded: signal(),
        windowRemoved: signal(),
        windowActivated: signal(),
        screensChanged: signal(),
        windowList: () => [],
        clientArea: (option, output) => Object.assign({}, output.area),
        get activeWindow() {
            return active;
        },
        set activeWindow(w) {
            active = w;
            workspace.windowActivated(w);
        },
    };
    env.workspace = workspace;
    env.outputs = outputs;
    env.context = {
        workspace,
        KWin: { PlacementArea: 0 },
        callDBus: (...args) => { env.calls.push(args); if(args[3]==="Status" && typeof args[4]==="function") {
            const deliver=()=>args[4](JSON.stringify({view:env.view,session:"local-1"}));
            if(env.delayedStatus) env.pendingReplies.push(deliver); else deliver();
        } },
        print: () => {},
        JSON,
        Math,
    };
    return env;
}

function load(env, script) {
    vm.createContext(env.context);
    vm.runInContext(script, env.context);
}

function addWindow(env, appId, geometry) {
    const w = new Window(env, appId, geometry);
    env.workspace.windowAdded(w);
    return w;
}

const script = fs.readFileSync(process.argv[2], "utf8");
let passed = 0;
function test(name, fn) {
    fn();
    passed++;
    console.log("PASS " + name);
}

test("centers in the work area and takes focus", () => {
    const env = makeEnv();
    env.outputs[0].area = { x: 0, y: 0, width: 1920, height: 1036 }; // 44 px panel at the bottom
    load(env, script);
    const dolphin = addWindow(env, "org.kde.dolphin", { x: 50, y: 50, width: 1000, height: 700 });
    env.workspace.activeWindow = dolphin;
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 800, height: 600 });
    assert.deepStrictEqual(w.frameGeometry, { x: 560, y: 218, width: 800, height: 600 });
    assert.strictEqual(env.workspace.activeWindow, w);
    assert.ok(w.skipTaskbar && w.skipPager && w.skipSwitcher);
    const report = env.calls.find((c) => c[3] === "ReportWorkAreas");
    assert.ok(report, "work areas reported");
    const data = JSON.parse(report[4]);
    assert.strictEqual(data.active, "DP-1");
    assert.strictEqual(data.screens[0].height, 1036);
});

test("other windows are left alone", () => {
    const env = makeEnv();
    load(env, script);
    const other = addWindow(env, "org.kde.konsole", { x: 3, y: 4, width: 500, height: 400 });
    assert.deepStrictEqual(other.frameGeometry, { x: 3, y: 4, width: 500, height: 400 });
    assert.strictEqual(env.geometryWrites, 0);
});

test("stays centered when the content size changes", () => {
    const env = makeEnv();
    load(env, script);
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 800, height: 600 });
    w.clientResize(400, 300);
    assert.deepStrictEqual(w.frameGeometry, { x: 760, y: 370, width: 400, height: 300 });
});

test("keeps the user's position after a manual move", () => {
    const env = makeEnv();
    load(env, script);
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 800, height: 600 });
    w.userMove(100, 120); // center now at (500, 420)
    assert.deepStrictEqual(w.frameGeometry, { x: 100, y: 120, width: 800, height: 600 });
    w.clientResize(600, 400);
    assert.deepStrictEqual(w.frameGeometry, { x: 200, y: 220, width: 600, height: 400 });
});

test("is clamped into the work area", () => {
    const env = makeEnv();
    load(env, script);
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 2500, height: 1500 });
    assert.deepStrictEqual(w.frameGeometry, { x: 0, y: 0, width: 1920, height: 1040 });
    w.userMove(1700, 900);
    const g = w.frameGeometry;
    assert.ok(g.x + g.width <= 1920 && g.y + g.height <= 1040, "never outside after a move");
});

test("opens on the active screen", () => {
    const env = makeEnv();
    env.workspace.activeScreen = env.outputs[1];
    load(env, script);
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 640, height: 480 });
    assert.deepStrictEqual(w.frameGeometry, { x: 1920 + 320, y: 272, width: 640, height: 480 });
});

test("fullscreen is not repositioned", () => {
    const env = makeEnv();
    load(env, script);
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 640, height: 480 });
    const writes = env.geometryWrites;
    w.fullScreen = true;
    w.clientResize(1920, 1080);
    assert.strictEqual(env.geometryWrites, writes);
});

test("focus returns to Dolphin when the preview closes", () => {
    const env = makeEnv();
    load(env, script);
    const dolphin = addWindow(env, "org.kde.dolphin", { x: 0, y: 0, width: 900, height: 600 });
    env.workspace.activeWindow = dolphin;
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 640, height: 480 });
    assert.strictEqual(env.workspace.activeWindow, w);
    w.close();
    assert.strictEqual(env.workspace.activeWindow, dolphin);
});

test("focus is not stolen back after the user clicked elsewhere", () => {
    const env = makeEnv();
    load(env, script);
    const dolphin = addWindow(env, "org.kde.dolphin", { x: 0, y: 0, width: 900, height: 600 });
    const editor = addWindow(env, "org.kde.kate", { x: 0, y: 0, width: 900, height: 600 });
    env.workspace.activeWindow = dolphin;
    const w = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 640, height: 480 });
    env.workspace.activeWindow = editor; // focus loss closes the preview
    w.close();
    assert.strictEqual(env.workspace.activeWindow, editor);
});

test("each new preview window starts centered again", () => {
    const env = makeEnv();
    load(env, script);
    const w1 = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 800, height: 600 });
    w1.userMove(10, 10);
    w1.close();
    const w2 = addWindow(env, "org.kde.kdeglance", { x: 0, y: 0, width: 800, height: 600 });
    assert.deepStrictEqual(w2.frameGeometry, { x: 560, y: 220, width: 800, height: 600 });
});

test("photos remain partly off screen after a move and wheel resize", () => {
    const env=makeEnv(); env.view="image"; load(env,script);
    const w=addWindow(env,"org.kde.kdeglance",{x:0,y:0,width:800,height:600});
    w.userMove(-300,-200);
    assert.deepStrictEqual(w.frameGeometry,{x:-300,y:-200,width:800,height:600});
    w.clientResize(400,300);
    assert.deepStrictEqual(w.frameGeometry,{x:-300,y:-200,width:400,height:300});
});
test("photo position-only changes are not recentered", () => {
    const env=makeEnv(); env.view="image"; env.liveGeometry=true; load(env,script);
    const w=addWindow(env,"org.kde.kdeglance",{x:0,y:0,width:800,height:600});
    w.frameGeometry={x:1800,y:900,width:800,height:600};
    assert.deepStrictEqual(w.frameGeometry,{x:1800,y:900,width:800,height:600});
});
test("photo tiling and maximize never receive stale placement writes", () => {
    const env=makeEnv();env.view="image";load(env,script);
    const w=addWindow(env,"org.kde.kdeglance",{x:0,y:0,width:800,height:600});
    w.userMove(1500,700);
    const writes=env.geometryWrites;
    for(const r of [{x:0,y:0,width:1920,height:1040},{x:0,y:0,width:960,height:1040},
        {x:960,y:0,width:960,height:1040},{x:400,y:200,width:800,height:600}]) {
        // Separate compositor size/position signals, including the transition
        // before its tile/maximize property has changed.
        w.clientResize(r.width,r.height);
        w._geometry=Object.assign({},r);w.frameGeometryChanged();
        assert.deepStrictEqual(w.frameGeometry,r);
    }
    assert.strictEqual(env.geometryWrites,writes);
});
test("photo compositor states report only transitions and never place", () => {
    const env=makeEnv();env.view="image";load(env,script);
    const w=addWindow(env,"org.kde.kdeglance",{x:0,y:0,width:800,height:600});
    const writes=env.geometryWrites;
    w.maximizeMode=3;w.clientResize(1920,1040);
    w.clientResize(1919,1040);
    w.maximizeMode=0;w.tile={};w.clientResize(960,1040);
    w.tile=null;w.clientResize(800,600);
    assert.strictEqual(env.geometryWrites,writes);
    assert.deepStrictEqual(env.calls.filter(c=>c[3]==="ReportPhotoWindowState").map(c=>c[5]),[false,true,false]);
});
test("module switch cannot apply stale media placement to a photo", () => {
    const env=makeEnv();load(env,script);
    const w=addWindow(env,"org.kde.kdeglance",{x:0,y:0,width:800,height:600});
    env.delayedStatus=true;env.view="image";w.captionChanged();
    const writes=env.geometryWrites;
    w.clientResize(1200,700);
    for(const reply of env.pendingReplies) reply();
    assert.strictEqual(env.geometryWrites,writes);
});
console.log(passed + " passed");
