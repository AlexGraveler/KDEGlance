// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
//
// KDEGlance window placement helper for KWin (Plasma 6, Wayland and X11).
//
// The KDEGlance service loads this script at runtime through KWin's D-Bus
// scripting interface and unloads it when it quits. Nothing is written to the
// user's KWin configuration.
//
// Responsibilities (only for KDEGlance's own preview window):
//  * center the window in the work area (panels excluded) of the active screen
//    when it opens;
//  * keep it centered when the viewer changes its size for new content, until
//    the user moves or resizes it; from then on keep the user's center point;
//  * keep non-photo windows inside the work area; delegate photos to KWin after opening;
//  * hide it from the task bar, pager and task switcher;
//  * give it keyboard focus when it opens and give the focus back to the
//    window that had it (Dolphin) when it closes, unless the user has moved
//    the focus elsewhere in the meantime;
//  * report the work areas of all screens back to the viewer.

const APP_IDS = ["org.kde.kdeglance", "kdeglance"];
const SERVICE = "org.kde.kdeglance";
const PATH = "/Viewer";
const IFACE = "org.kde.kdeglance.Viewer";

function isOurs(w) {
    if (!w || !w.normalWindow) {
        return false;
    }
    return APP_IDS.indexOf(w.desktopFileName) >= 0 || APP_IDS.indexOf(w.resourceClass) >= 0;
}

function workArea(output) {
    return workspace.clientArea(KWin.PlacementArea, output, workspace.currentDesktop);
}

function reportWorkAreas() {
    const screens = [];
    const list = workspace.screens;
    for (let i = 0; i < list.length; ++i) {
        const o = list[i];
        const a = workArea(o);
        screens.push({
            name: o.name,
            x: Math.round(a.x),
            y: Math.round(a.y),
            width: Math.round(a.width),
            height: Math.round(a.height),
            scale: o.devicePixelRatio !== undefined ? o.devicePixelRatio : 1,
        });
    }
    const active = workspace.activeScreen ? workspace.activeScreen.name : "";
    callDBus(SERVICE, PATH, IFACE, "ReportWorkAreas", JSON.stringify({ active: active, screens: screens }));
}

function geometrySnapshot(w) {
    // QJSEngine's Qt value wrapper can track the live property. Store scalar
    // coordinates, not the wrapper, when comparing the previous rectangle.
    const g=w.frameGeometry;
    return {x:g.x,y:g.y,width:g.width,height:g.height};
}
function rememberGeometry(w, st) {
    const g=w.frameGeometry;
    st.anchor={x:g.x+g.width/2,y:g.y+g.height/2,left:g.x,top:g.y};
    st.last=geometrySnapshot(w);
}
function refreshMode(w, st, done) {
    callDBus(SERVICE, PATH, IFACE, "Status", function(reply) {
        if (!states[w.internalId.toString()]) return;
        try {
            const status=JSON.parse(reply);
            const photo=status.view==="image";
            if(photo!==st.photo || status.session!==st.session) st.managed=undefined;
            st.photo=photo;
            st.session=status.session;
        } catch(e) { /* keep previous mode */ }
        if(done) done();
    });
}
function place(w, st) {
    if(w.fullScreen || w.maximizeMode || w.tile) return;
    // After initial placement, the compositor exclusively owns photo position
    // and managed geometry. Never restore an old top-left over a tile/maximize
    // transition (its size and position can arrive in separate signals).
    if(st.photo) return;
    const a = workArea(st.output);
    const g = w.frameGeometry;
    const width = Math.min(g.width, a.width);
    const height = Math.min(g.height, a.height);
    let cx = a.x + a.width / 2;
    let cy = a.y + a.height / 2;
    if (st.anchor) {
        cx = st.anchor.x;
        cy = st.anchor.y;
    }
    let x = Math.round(cx - width / 2);
    let y = Math.round(cy - height / 2);
    x = Math.max(a.x, Math.min(x, a.x + a.width - width));
    y = Math.max(a.y, Math.min(y, a.y + a.height - height));
    if (Math.abs(g.x - x) < 1 && Math.abs(g.y - y) < 1 && Math.abs(g.width - width) < 1 && Math.abs(g.height - height) < 1) {
        return;
    }
    st.applying = true;
    w.frameGeometry = { x: x, y: y, width: width, height: height };
    st.applying = false;
    st.last=geometrySnapshot(w);
}

const states = {};

function track(w) {
    const id = w.internalId.toString();
    const previous = workspace.activeWindow;
    const st = {
        output: workspace.activeScreen,
        anchor: null,
        photo: null,
        last: geometrySnapshot(w),
        applying: false,
        interactive: false,
        previous: previous && !isOurs(previous) ? previous : null,
        focusLeft: false,
    };
    states[id] = st;

    w.skipTaskbar = true;
    w.skipPager = true;
    w.skipSwitcher = true;

    place(w, st);
    function reportPhotoState() {
        const managed=Boolean(w.fullScreen || w.maximizeMode || w.tile);
        if(st.photo && st.managed!==managed) {
            st.managed=managed;
            callDBus(SERVICE,PATH,IFACE,"ReportPhotoWindowState",st.session,managed);
        }
    }
    refreshMode(w, st, reportPhotoState);
    if(w.captionChanged) w.captionChanged.connect(function(){
        // File/module navigation is asynchronous; do not apply the previous
        // module's placement policy while its new Status reply is pending.
        st.photo=null;
        refreshMode(w,st,function(){reportPhotoState();if(st.photo===false) place(w,st);});
    });
    if(w.maximizedChanged) w.maximizedChanged.connect(reportPhotoState);
    if(w.tileChanged) w.tileChanged.connect(reportPhotoState);

    w.frameGeometryChanged.connect(function () {
        reportPhotoState();
        if (st.applying || st.interactive || w.move || w.resize || w.fullScreen || w.maximizeMode || w.tile) {
            return;
        }
        if(st.photo !== false) {
            // No geometry writes or D-Bus round trips on the photo wheel path.
            st.last=geometrySnapshot(w);
            return;
        }
        place(w, st);
        st.last=geometrySnapshot(w);
    });
    w.interactiveMoveResizeStarted.connect(function () {
        st.interactive = true;
        refreshMode(w,st);
    });
    w.interactiveMoveResizeFinished.connect(function () {
        st.interactive = false;
        rememberGeometry(w,st);
        st.output = w.output;
        refreshMode(w,st,function(){ reportPhotoState(); if(!st.photo && !w.maximizeMode && !w.tile) place(w,st); });
    });
    w.closed.connect(function () {
        delete states[id];
        if (!st.focusLeft && st.previous && !st.previous.deleted && !st.previous.minimized) {
            workspace.activeWindow = st.previous;
        }
    });

    workspace.activeWindow = w;
    reportWorkAreas();
}

workspace.windowAdded.connect(function (w) {
    if (isOurs(w)) {
        track(w);
    }
});

workspace.windowActivated.connect(function (w) {
    if (!w) {
        return;
    }
    if (!isOurs(w)) {
        for (const id in states) {
            states[id].focusLeft = true;
        }
    }
});

workspace.screensChanged.connect(function () {
    for (const id in states) {
        reportWorkAreas();
        break;
    }
});

// Windows that already exist when the script is (re)loaded.
const existing = workspace.windowList();
for (let i = 0; i < existing.length; ++i) {
    if (isOurs(existing[i])) {
        track(existing[i]);
    }
}
reportWorkAreas();
