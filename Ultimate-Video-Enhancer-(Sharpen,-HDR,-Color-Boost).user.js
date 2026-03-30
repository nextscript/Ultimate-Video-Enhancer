// ==UserScript==
// @name         Ultimate Video Enhancer (Sharpen, HDR, Color Boost)
// @name:de      Ultimate Video Enhancer (Schärfe, HDR, Farben)
// @namespace    gvf
// @author       Freak288
// @version      1.10.1
// @description  Instantly improve every video on any website. Adds real-time sharpening, HDR boost, better colors and contrast to all HTML5 videos.
// @description:de  Verbessert sofort jedes Video auf jeder Website. Fügt Schärfe, HDR, bessere Farben und Kontrast in Echtzeit hinzu – für alle HTML5-Videos.
// @match        *://*/*
// @run-at       document-idle
// @grant        GM_getValue
// @grant        GM_setValue
// @grant        GM_addValueChangeListener
// @grant        GM_info
// @grant        GM_download
// @grant        GM_xmlhttpRequest
// @grant        unsafeWindow
// @grant        GM_addElement
// @connect      raw.githubusercontent.com
// @connect      github.com
// @connect      cdn.jsdelivr.net
// @iconURL      https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/refs/heads/main/logomes.png
// @downloadURL https://update.greasyfork.org/scripts/561189/Ultimate%20Video%20Enhancer%20%28Sharpen%2C%20HDR%2C%20Color%20Boost%29.user.js
// @updateURL https://update.greasyfork.org/scripts/561189/Ultimate%20Video%20Enhancer%20%28Sharpen%2C%20HDR%2C%20Color%20Boost%29.meta.js
// ==/UserScript==

(function () {
    'use strict';
    if (typeof window === 'undefined') return;

    // -------------------------
    // GVF SVG Import Page Handler
    // -------------------------
    (function handleImportPage() {
        try {
            const host = (location.hostname || '').toLowerCase();
            const isImportPage = host === 'svg.ts3x.cc' || document.documentElement.hasAttribute('data-gvf-import-page');
            if (!isImportPage) return;
            window.__GVF_IMPORT_PAGE__ = true;
            window.GVF_DETECTED = true;
            // Dispatch event so the page can detect GVF regardless of timing
            try {
                document.dispatchEvent(new CustomEvent('gvf-detected'));
            } catch (_) {}

            function wireButtons() {
                document.querySelectorAll('[data-gvf-install]').forEach(btn => {
                    if (btn.__gvfWired) return;
                    btn.__gvfWired = true;
                    btn.addEventListener('click', () => {
                        try {
                            const entry = JSON.parse(btn.getAttribute('data-gvf-install') || '{}');
                            if (!entry.label || !entry.code) { alert('Invalid entry.'); return; }

                            let codes = [];
                            try {
                                const raw = GM_getValue('gvf_custom_svg_codes', null);
                                if (raw) { const p = JSON.parse(raw); if (Array.isArray(p)) codes = p; }
                            } catch (_) {}

                            const exists = codes.find(e => e.label === entry.label);
                            // Auto-detect type if not provided: GLSL code → webgl, otherwise svg
                            const detectedType = entry.type === 'canvas2d' ? 'canvas2d' :
                                entry.type === 'webgl' ? 'webgl' :
                                entry.type === 'svg' ? 'svg' : (
                                /^\s*#version\s+300\s+es/m.test(entry.code) ||
                                /\bvoid\s+main\s*\(/m.test(entry.code) ||
                                /\buniform\s+sampler2D\b/m.test(entry.code)
                                ? 'webgl' : 'svg'
                            );
                            if (exists) {
                                if (!confirm(`"${entry.label}" already exists. Overwrite?`)) return;
                                exists.code = entry.code;
                                exists.type = detectedType;
                                exists.enabled = true;
                            } else {
                                codes.push({ id: 'csvg_' + Date.now(), label: entry.label, code: entry.code, type: detectedType, enabled: true });
                            }

                            GM_setValue('gvf_custom_svg_codes', JSON.stringify(codes));

                            const orig = btn.innerHTML;
                            btn.innerHTML = '✓ Installed!';
                            btn.disabled = true;
                            btn.classList.remove('btn-success');
                            btn.classList.add('btn-outline-success');
                            setTimeout(() => {
                                btn.innerHTML = orig;
                                btn.disabled = false;
                                btn.classList.add('btn-success');
                                btn.classList.remove('btn-outline-success');
                            }, 2000);
                        } catch (e) { alert('Install failed: ' + e.message); }
                    });
                });
            }

            if (document.readyState === 'loading') {
                document.addEventListener('DOMContentLoaded', wireButtons, { once: true });
            } else {
                wireButtons();
            }
            new MutationObserver(wireButtons).observe(document.documentElement, { childList: true, subtree: true });
        } catch (_) {}
    })();

    if (window.__GVF_IMPORT_PAGE__) return;
    if (window.__GLOBAL_VIDEO_FILTER__) return;
    window.__GLOBAL_VIDEO_FILTER__ = true;

    // -------------------------
    // IDs / Constants
    // -------------------------
    const STYLE_ID = 'global-video-filter-style';
    const SVG_ID = 'global-video-filter-svg';
    const GPU_SVG_ID = 'gvf-gpu-svg';
    const GPU_GAIN_FILTER_ID = 'gvf-gpu-gain-filter';
    const GPU_PROFILE_FILTER_ID = 'gvf-gpu-profile-filter';
    const WEBGL_CANVAS_ID = 'gvf-webgl-canvas';
    const WEBGL_WRAPPER_ATTR = 'data-gvf-webgl-wrapper';
    const RECORDING_HUD_ID = 'gvf-recording-hud';
    const CONFIG_MENU_ID = 'gvf-config-menu';
    const LUT_CONFIG_MENU_ID = 'gvf-lut-config-menu';
    const NOTIFICATION_ID = 'gvf-profile-notification';
    const svgNS = 'http://www.w3.org/2000/svg';

    // Hotkeys
    const HDR_TOGGLE_KEY = 'p';
    const PROF_TOGGLE_KEY = 'c';
    const GRADE_HUD_KEY = 'g';
    const IO_HUD_KEY = 'i';
    const AUTO_KEY = 'a';
    const SCOPES_KEY = 's';
    const GPU_MODE_KEY = 'x';
    const PROFILE_CYCLE_KEY = 'q'; // Shift+Q for profile cycling

    // -------------------------
    // Throttling for less computationally intensive operations
    // -------------------------
    let lastRenderTime = 0;
    const RENDER_THROTTLE = 41; // ~24 FPS cap to reduce GPU load

    function render() {
        if (renderMode === 'gpu') {
            applyGpuFilter();
        } else {
            regenerateSvgImmediately();
        }
    }

    function throttledRender(timestamp) {
        if (timestamp - lastRenderTime >= RENDER_THROTTLE) {
            lastRenderTime = timestamp;
            render();
        }
        requestAnimationFrame(throttledRender);
    }

    function isVideoRenderable(video) {
        if (!video) return false;
        if (video.readyState < 2 || video.videoWidth === 0 || video.videoHeight === 0) return false;
        if (video.paused || video.ended) return false;
        const cs = window.getComputedStyle(video);
        if (!cs || cs.display === 'none' || cs.visibility === 'hidden') return false;
        const r = video.getBoundingClientRect();
        if (!r || r.width < 40 || r.height < 40) return false;
        if (r.bottom <= 0 || r.right <= 0) return false;
        if (r.top >= (window.innerHeight || 0) || r.left >= (window.innerWidth || 0)) return false;
        return true;
    }

    function isHudHostTabActive() {
        try {
            if (document.hidden) return false;
            if (document.visibilityState && document.visibilityState !== 'visible') return false;
            if (typeof document.hasFocus === 'function' && !document.hasFocus()) {
                // Don't hide HUD if a GVF element currently has focus (e.g. slider, textarea, button)
                const focused = document.activeElement;
                if (!focused || !focused.closest(
                    '.gvf-video-overlay-io, .gvf-video-overlay-grade, .gvf-video-overlay, [id^="gvf-"]'
                )) return false;
            }
        } catch (_) { }
        return true;
    }

    function isHudVideoVisible(video) {
        if (!video) return false;
        if (!isHudHostTabActive()) return false;
        if (video.readyState < 1) return false;
        const cs = window.getComputedStyle(video);
        if (!cs || cs.display === 'none' || cs.visibility === 'hidden') return false;
        const r = video.getBoundingClientRect();
        if (!r || r.width < 40 || r.height < 40) return false;
        if (r.bottom <= 0 || r.right <= 0) return false;
        if (r.top >= (window.innerHeight || 0) || r.left >= (window.innerWidth || 0)) return false;
        return true;
    }

    function getHudPrimaryVideo() {
        if (!isHudHostTabActive()) return null;
        const videos = Array.from(document.querySelectorAll('video'));
        let best = null;
        let bestArea = 0;
        for (const video of videos) {
            if (!isHudVideoVisible(video)) continue;
            const r = video.getBoundingClientRect();
            const area = Math.max(0, r.width) * Math.max(0, r.height);
            if (area > bestArea) {
                bestArea = area;
                best = video;
            }
        }
        return best;
    }

    function getGpuPrimaryVideo() {
        const videos = Array.from(document.querySelectorAll('video'));
        let best = null;
        let bestArea = 0;
        for (const video of videos) {
            if (!isVideoRenderable(video)) continue;
            const r = video.getBoundingClientRect();
            const area = Math.max(0, r.width) * Math.max(0, r.height);
            if (area > bestArea) {
                bestArea = area;
                best = video;
            }
        }
        return best;
    }

    // Like getGpuPrimaryVideo but also matches paused/ended videos — needed for GLSL overlays
    function getWebglPrimaryVideo() {
        const videos = Array.from(document.querySelectorAll('video'));
        let best = null;
        let bestArea = 0;
        for (const video of videos) {
            if (!video) continue;
            if (video.readyState < 1 || video.videoWidth === 0 || video.videoHeight === 0) continue;
            const cs = window.getComputedStyle(video);
            if (!cs || cs.display === 'none' || cs.visibility === 'hidden') continue;
            const r = video.getBoundingClientRect();
            if (!r || r.width < 40 || r.height < 40) continue;
            if (r.bottom <= 0 || r.right <= 0) continue;
            if (r.top >= (window.innerHeight || 0) || r.left >= (window.innerWidth || 0)) continue;
            const area = r.width * r.height;
            if (area > bestArea) { bestArea = area; best = video; }
        }
        return best;
    }

    // -------------------------
    // LOG + DEBUG SWITCH
    // -------------------------
    let logs = true;    // console logs
    let debug = false;    // visual debug (Auto-dot) - DEFAULT FALSE

    // -------------------------
    // CSS.escape Polyfill
    // -------------------------
    const cssEscape = (s) => {
        try {
            if (window.CSS && typeof window.CSS.escape === 'function') return window.CSS.escape(String(s));
        } catch (_) { }
        return String(s).replace(/[^a-zA-Z0-9_-]/g, (m) => '\\' + m);
    };

    // GM keys
    const K = {
        enabled: 'gvf_enabled',
        moody: 'gvf_moody',
        teal: 'gvf_teal',
        vib: 'gvf_vib',
        icons: 'gvf_icons',

        SL: 'gvf_sl',
        SR: 'gvf_sr',
        BL: 'gvf_bl',
        WL: 'gvf_wl',
        DN: 'gvf_dn',
        EDGE: 'gvf_edge',

        HDR: 'gvf_hdr',
        HDR_LAST: 'gvf_hdr_last',

        PROF: 'gvf_profile',

        G_HUD: 'gvf_g_hud',
        I_HUD: 'gvf_i_hud',
        S_HUD: 'gvf_s_hud',

        RENDER_MODE: 'gvf_render_mode',

        U_CONTRAST: 'gvf_u_contrast',
        U_BLACK: 'gvf_u_black',
        U_WHITE: 'gvf_u_white',
        U_HIGHLIGHTS: 'gvf_u_highlights',
        U_SHADOWS: 'gvf_u_shadows',
        U_SAT: 'gvf_u_saturation',
        U_VIB: 'gvf_u_vibrance',
        U_SHARP: 'gvf_u_sharpen',
        U_GAMMA: 'gvf_u_gamma',
        U_GRAIN: 'gvf_u_grain',
        U_HUE: 'gvf_u_hue',

        U_R_GAIN: 'gvf_u_r_gain',
        U_G_GAIN: 'gvf_u_g_gain',
        U_B_GAIN: 'gvf_u_b_gain',

        AUTO_ON: 'gvf_auto_on',
        AUTO_STRENGTH: 'gvf_auto_strength',
        AUTO_LOCK_WB: 'gvf_auto_lock_wb',
        NOTIFY: 'gvf_notify',

        LOGS: 'gvf_logs',
        DEBUG: 'gvf_debug',

        // Color blindness filter
        CB_FILTER: 'gvf_cb_filter',

        // Profile Management
        ACTIVE_USER_PROFILE: 'gvf_active_user_profile',
        USER_PROFILES: 'gvf_user_profiles',
        USER_PROFILES_REV: 'gvf_user_profiles_rev',

        // LUT Profile Management
        LUT_ACTIVE_PROFILE: 'gvf_lut_active_profile',
        LUT_PROFILES: 'gvf_lut_profiles',
        LUT_PROFILES_REV: 'gvf_lut_profiles_rev',
        LUT_GROUPS: 'gvf_lut_groups',

        USER_PROFILE_MANAGER_POS: 'gvf_user_profile_manager_pos',
        LUT_PROFILE_MANAGER_POS: 'gvf_lut_profile_manager_pos',

        // Custom SVG filter codes
        CUSTOM_SVG_CODES: 'gvf_custom_svg_codes'
    };

    // -------------------------
    // Helpers
    // -------------------------
    const clamp = (n, a, b) => Math.min(b, Math.max(a, n));
    const roundTo = (n, step) => Math.round(n / step) * step;
    const snap0 = (n, eps) => (Math.abs(n) <= eps ? 0 : n);
    const nFix = (n, digits = 1) => Number((Number(n) || 0).toFixed(digits));
    const gmGet = (key, fallback) => { try { return GM_getValue(key, fallback); } catch (_) { return fallback; } };
    const gmSet = (key, val) => { try { GM_setValue(key, val); } catch (_) { } };
    const nowMs = () => (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
    const isFirefox = () => { try { return /firefox/i.test(navigator.userAgent || ''); } catch (_) { return false; } };

    // -------------------------
    // Custom SVG Codes  { id, label, code, enabled }
    // -------------------------
    let customSvgCodes = [];

    function loadCustomSvgCodes() {
        try {
            const raw = gmGet(K.CUSTOM_SVG_CODES, null);
            if (raw) {
                const p = JSON.parse(raw);
                if (Array.isArray(p)) { customSvgCodes = p.filter(e => e && e.id); return; }
            }
        } catch (_) {}
        customSvgCodes = [];
    }

    function saveCustomSvgCodes() {
        try { gmSet(K.CUSTOM_SVG_CODES, JSON.stringify(customSvgCodes)); } catch (_) {}
        // Update count badge in HUD if visible
        const badge = document.getElementById('gvf-svg-codes-count');
        if (badge) {
            const ac = customSvgCodes.filter(e => e.enabled).length;
            badge.textContent = customSvgCodes.length ? `${ac}/${customSvgCodes.length} active` : '';
        }
    }

    function parseCustomSvgCode(codeStr) {
        // Manual parser: extract tag name + attributes, build SVGElements via createElementNS.
        // Avoids DOMParser entirely — no CSP/namespace/whitespace issues.
        try {
            const results = [];
            // Match self-closing or open tags: <tagName attr="val" .../>  or <tagName ...>
            const tagRe = /<([a-zA-Z][a-zA-Z0-9]*)((?:\s+[^>]*?)?)\s*\/?>/g;
            let m;
            while ((m = tagRe.exec(codeStr)) !== null) {
                const tagName = m[1];
                const attrStr = m[2] || '';
                const el = document.createElementNS('http://www.w3.org/2000/svg', tagName);

                // Parse attributes: name="value" or name='value'
                const attrRe = /([a-zA-Z][a-zA-Z0-9_:-]*)\s*=\s*(?:"([\s\S]*?)"|'([\s\S]*?)')/g;
                let am;
                while ((am = attrRe.exec(attrStr)) !== null) {
                    const attrName = am[1];
                    // Normalize whitespace in the value (newlines → space)
                    const attrVal = (am[2] !== undefined ? am[2] : am[3]).replace(/[\r\n\t]+/g, ' ').replace(/ {2,}/g, ' ').trim();
                    el.setAttribute(attrName, attrVal);
                }
                results.push(el);
            }
            return results.length ? results : null;
        } catch (_) { return null; }
    }

    // -------------------------
    // Custom WebGL Overlay Manager
    // Handles type:'webgl' entries: each active entry gets its own fullscreen canvas
    // overlaid on the primary video. Fragment shader receives:
    //   uniform sampler2D u_video;  (video frame texture)
    //   uniform vec2      u_res;    (canvas width, height)
    //   in vec2           v_uv;     (0..1 UV, WebGL2 / GLSL300)
    // -------------------------
    // ── GLSL overlay shared state ─────────────────────────────────────────────
    let _mouseX = 0.5, _mouseY = 0.5;
    let _scrollZoom = 1.0;
    const _ZOOM_MIN = 0.5, _ZOOM_MAX = 8.0, _ZOOM_STEP = 0.15;
    document.addEventListener('wheel', e => {
        // Only zoom when mouse is over a video element
        const vid = document.querySelector('video');
        if (!vid) return;
        const r = vid.getBoundingClientRect();
        if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) return;
        e.preventDefault();
        const delta = e.deltaY > 0 ? -_ZOOM_STEP : _ZOOM_STEP;
        _scrollZoom = Math.min(_ZOOM_MAX, Math.max(_ZOOM_MIN, _scrollZoom + delta));
    }, { passive: false });
    let _rawMouseClientX = 0, _rawMouseClientY = 0;
    document.addEventListener('mousemove', e => {
        // Store raw client coords — each instance computes relative to its own video BCR
        _rawMouseClientX = e.clientX;
        _rawMouseClientY = e.clientY;
        // Also keep normalized fallback
        _mouseX = e.clientX / (window.innerWidth  || 1);
        _mouseY = e.clientY / (window.innerHeight || 1);
    }, { passive: true });

    function _getStrength() {
        // Returns a 0..1 value representing how many/how intense filters are active
        let s = 0, n = 0;
        if (enabled)     { s += 1; n++; }
        if (darkMoody)   { s += 1; n++; }
        if (tealOrange)  { s += 1; n++; }
        if (vibrantSat)  { s += 1; n++; }
        if (normHDR() !== 0) { s += Math.abs(normHDR()); n++; }
        return n > 0 ? Math.min(1, s / n) : 0;
    }

    function _getLayers() {
        let n = 0;
        if (enabled)         n++;
        if (darkMoody)       n++;
        if (tealOrange)      n++;
        if (vibrantSat)      n++;
        if (normHDR() !== 0) n++;
        if (autoOn)          n++;
        return n;
    }

    const CustomWebglOverlayManager = (() => {
        // Map: entry.id -> { canvas, gl, program, texture, rafId, sig }
        const _instances = new Map();

        const _vsSource = `#version 300 es
in vec2 a_pos;
in vec2 a_uv;
out vec2 v_uv;
void main(){
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}`;

        function _compileShader(gl, type, src) {
            const sh = gl.createShader(type);
            gl.shaderSource(sh, src);
            gl.compileShader(sh);
            if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
                const err = gl.getShaderInfoLog(sh);
                gl.deleteShader(sh);
                throw new Error(err);
            }
            return sh;
        }

        // Normalize user GLSL300 fragment code to use internal uniform names
        function _normalizeUserFrag(src) {
            // Strip #version if present — we prepend our own
            let s = src.replace(/^\s*#version\s+\S+[\t ]*/mg, '');
            // Strip duplicate precision qualifiers — we provide precision highp float
            s = s.replace(/^\s*precision\s+\w+\s+\w+\s*;\s*/mg, '');
            // Strip duplicate declarations of our injected uniforms/ins/outs
            s = s.replace(/^\s*uniform\s+sampler2D\s+u_video\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+sampler2D\s+u_video_raw\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+vec2\s+u_res\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_time\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+vec2\s+u_mouse\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_strength\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_layers\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_zoom\s*;\s*/mg, '');
            // Shadertoy: iChannel1 -> u_video_raw
            s = s.replace(/iChannel1/g, 'u_video_raw');
            s = s.replace(/^\s*in\s+vec2\s+v_uv\s*;\s*/mg, '');
            s = s.replace(/^\s*out\s+vec4\s+fragColor\s*;\s*/mg, '');
            // Upgrade texture2D -> texture (GLSL100 style)
            s = s.replace(/\btexture2D\b/g, 'texture');
            // Shadertoy: iResolution -> u_res, iTime -> u_time, iChannel0 -> u_video
            s = s.replace(/\biResolution\b/g, 'vec3(u_res, 0.0)');
            s = s.replace(/\biTime\b/g, 'u_time');
            s = s.replace(/\biChannel0\b/g, 'u_video');
            // Shadertoy: mainImage(out vec4 fragColor, in vec2 fragCoord) -> void main()
            // wrap it: call mainImage and map fragCoord = v_uv * u_res
            if (/\bmainImage\s*\(/.test(s) && !s.includes('void main')) {
                s = s + '\nvoid main(){\n    mainImage(fragColor, v_uv * u_res);\n}';
            }
            return s;
        }

        function _buildFragSrc(userSrc) {
            const body = _normalizeUserFrag(userSrc);

            let mainBlock;
            if (body.includes('void main')) {
                // User provided full main — use as-is
                mainBlock = body;
            } else {
                // No main — detect last defined function: return type + name + params
                // Matches: vec3 foo(sampler2D tex, vec2 uv, ...) or vec4 bar(...)
                const fnRe = /\b(vec4|vec3|vec2|float|void)\s+(\w+)\s*\(([^)]*)\)/g;
                let lastFn = null;
                let m;
                while ((m = fnRe.exec(body)) !== null) lastFn = { ret: m[1], name: m[2], params: m[3] };

                if (lastFn && lastFn.ret !== 'void') {
                    // Build argument list: supply standard uniforms/vars by type
                    const argMap = {
                        'sampler2D': 'u_video',
                        'vec2':      null, // resolved by position below
                        'float':     '1.0',
                        'vec3':      'vec3(1.0)',
                        'vec4':      'vec4(1.0)',
                        'int':       '1',
                        'bool':      'false'
                    };
                    // For vec2 params: first = uv, second = res
                    let vec2Count = 0;
                    const args = lastFn.params.split(',').map(p => {
                        p = p.trim();
                        if (!p) return '';
                        const type = p.split(/\s+/)[0];
                        if (type === 'vec2') {
                            return vec2Count++ === 0 ? 'v_uv' : 'u_res';
                        }
                        return argMap[type] !== undefined ? argMap[type] : '0.0';
                    }).filter(Boolean).join(', ');

                    const call = `${lastFn.name}(${args})`;
                    const fragAssign = lastFn.ret === 'vec4'
                        ? `fragColor = ${call};`
                        : lastFn.ret === 'vec3'
                            ? `fragColor = vec4(${call}, 1.0);`
                            : `float _r = ${call}; fragColor = vec4(_r, _r, _r, 1.0);`;

                    mainBlock = `${body}\nvoid main(){\n    ${fragAssign}\n}`;
                } else {
                    // Fallback: wrap body directly
                    mainBlock = `void main(){\n${body}\n}`;
                }
            }

            return `#version 300 es
precision highp float;
uniform sampler2D u_video;        // filtered frame  (TEXTURE0)
uniform sampler2D u_video_raw;    // raw video frame (TEXTURE1)
uniform vec2 u_res;
uniform float u_time;
uniform vec2 u_mouse;             // normalized mouse position [0..1]
uniform float u_strength;         // current GVF filter strength
uniform float u_layers;           // number of active GVF layers
uniform float u_zoom;             // scroll-controlled zoom level (default 1.0)
in vec2 v_uv;
out vec4 fragColor;
${mainBlock}`;
        }

        function _createInstance(entry, video) {
            const canvas = document.createElement('canvas');
            canvas.setAttribute('data-gvf-custom-webgl', entry.id);
            // fixed to body — tracks video BCR in drawLoop, never inside player DOM so controls stay untouched
            canvas.style.cssText = `position:absolute;pointer-events:none;z-index:0;display:block;top:0;left:0;`;

            // Canvas z-index:0 inside player container — controls (z-index:59) paint above it naturally.

            let gl;
            try {
                gl = canvas.getContext('webgl2', { alpha: true, antialias: false, premultipliedAlpha: false, preserveDrawingBuffer: true });
                if (!gl) throw new Error('webgl2 unavailable');
            } catch (e) {
                logW('[GVF WebGL Overlay] WebGL2 not available:', e);
                return null;
            }

            let program;
            try {
                const fragSrc = _buildFragSrc(entry.code);
                const vs = _compileShader(gl, gl.VERTEX_SHADER, _vsSource);
                const fs = _compileShader(gl, gl.FRAGMENT_SHADER, fragSrc);
                program = gl.createProgram();
                gl.attachShader(program, vs);
                gl.attachShader(program, fs);
                gl.linkProgram(program);
                if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
                    throw new Error(gl.getProgramInfoLog(program));
                }
                gl.detachShader(program, vs); gl.deleteShader(vs);
                gl.detachShader(program, fs); gl.deleteShader(fs);
            } catch (e) {
                logW('[GVF WebGL Overlay] Shader compile error for "' + entry.label + '":', e.message);
                return null;
            }

            gl.useProgram(program);

            // VAO — keeps attribute state per-instance, no cross-frame pollution
            const vao = gl.createVertexArray();
            gl.bindVertexArray(vao);

            const verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
            const uvs   = new Float32Array([ 0, 0, 1, 0,  0,1, 1,1]);

            const vb = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, vb);
            gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
            const aPos = gl.getAttribLocation(program, 'a_pos');
            gl.enableVertexAttribArray(aPos);
            gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

            const ub = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, ub);
            gl.bufferData(gl.ARRAY_BUFFER, uvs, gl.STATIC_DRAW);
            const aUv = gl.getAttribLocation(program, 'a_uv');
            gl.enableVertexAttribArray(aUv);
            gl.vertexAttribPointer(aUv, 2, gl.FLOAT, false, 0, 0);

            gl.bindVertexArray(null);

            // Texture 0 — filtered frame (drawn via offscreen 2D canvas with CSS filter)
            const texture = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, texture);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            gl.bindTexture(gl.TEXTURE_2D, null);

            // Texture 1 — raw video frame
            const textureRaw = gl.createTexture();
            gl.bindTexture(gl.TEXTURE_2D, textureRaw);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            gl.bindTexture(gl.TEXTURE_2D, null);

            // Offscreen 2D canvas used to bake CSS filter onto video frame → texture 0
            const _filteredCanvas = document.createElement('canvas');
            const _filteredCtx = _filteredCanvas.getContext('2d', { alpha: false, willReadFrequently: false });

            const uVideo    = gl.getUniformLocation(program, 'u_video');
            const uVideoRaw = gl.getUniformLocation(program, 'u_video_raw');
            const uRes      = gl.getUniformLocation(program, 'u_res');
            const uTime     = gl.getUniformLocation(program, 'u_time');
            const uMouse    = gl.getUniformLocation(program, 'u_mouse');
            const uStrength = gl.getUniformLocation(program, 'u_strength');
            const uLayers   = gl.getUniformLocation(program, 'u_layers');
            const uZoom     = gl.getUniformLocation(program, 'u_zoom');

            gl.uniform1i(uVideo, 0);
            gl.uniform1i(uVideoRaw, 1);

            let alive = true;
            let lastPaused = false;

            function _reparentCanvas() {
                // Always insert canvas directly after the video element.
                // DOM order: video → canvas → controls — controls always paint above.
                // This works in both normal and fullscreen mode because we follow
                // the video element itself, not the fullscreen container.
                const parent = video.parentElement || document.body;
                if (canvas.parentNode !== parent || canvas.previousSibling !== video) {
                    const after = video.nextSibling;
                    parent.insertBefore(canvas, after !== canvas ? after : null);
                }
            }

            function doRender() {
                if (!video || !video.isConnected || video.readyState < 2) {
                    canvas.style.display = 'none';
                    return;
                }
                _reparentCanvas();
                const pr = (canvas.parentElement || document.body).getBoundingClientRect();
                const r = video.getBoundingClientRect();
                if (!r || r.width < 1 || r.height < 1) { canvas.style.display = 'none'; return; }
                canvas.style.display = 'block';
                canvas.style.position = 'absolute';
                canvas.style.left = (r.left - pr.left) + 'px';
                canvas.style.top  = (r.top  - pr.top)  + 'px';
                canvas.style.width  = r.width  + 'px';
                canvas.style.height = r.height + 'px';

                const w = video.videoWidth, h = video.videoHeight;
                if (!w || !h) return;
                if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }

                gl.viewport(0, 0, w, h);
                gl.useProgram(program);
                gl.bindVertexArray(vao);

                // ── Texture 0: filtered frame ────────────────────────────────
                // Bake current CSS filter string onto an offscreen 2D canvas,
                // then upload that as u_video so shaders see the filtered image.
                try {
                    if (_filteredCanvas.width !== w || _filteredCanvas.height !== h) {
                        _filteredCanvas.width = w;
                        _filteredCanvas.height = h;
                    }
                    const cssFilter = (() => {
                        try {
                            const s = document.getElementById('global-video-filter-style');
                            if (!s) return 'none';
                            const m = s.textContent.match(/filter\s*:\s*([^!;]+)/);
                            return (m && m[1].trim()) ? m[1].trim() : 'none';
                        } catch(_) { return 'none'; }
                    })();
                    _filteredCtx.filter = cssFilter;
                    _filteredCtx.drawImage(video, 0, 0, w, h);
                    window.__gvfFilteredFrame = _filteredCanvas; // expose for Canvas2D overlays
                    gl.activeTexture(gl.TEXTURE0);
                    gl.bindTexture(gl.TEXTURE_2D, texture);
                    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, _filteredCanvas);
                } catch (_) { return; }

                // ── Texture 1: raw frame ─────────────────────────────────────
                try {
                    gl.activeTexture(gl.TEXTURE1);
                    gl.bindTexture(gl.TEXTURE_2D, textureRaw);
                    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
                } catch (_) { /* raw frame unavailable — leave last */ }

                gl.uniform1i(uVideo, 0);
                gl.uniform1i(uVideoRaw, 1);
                gl.uniform2f(uRes, w, h);
                if (uTime     !== null) gl.uniform1f(uTime, performance.now() * 0.001);
                if (uMouse !== null) {
                    const _vr = video.getBoundingClientRect();
                    // Letterbox correction: map mouse into actual content area, not full BCR
                    const _vAsp = video.videoWidth / (video.videoHeight || 1);
                    const _bAsp = _vr.width / (_vr.height || 1);
                    let _contentL = _vr.left, _contentT = _vr.top;
                    let _contentW = _vr.width, _contentH = _vr.height;
                    if (_vAsp > _bAsp) {
                        _contentH = _vr.width / _vAsp;
                        _contentT = _vr.top + (_vr.height - _contentH) / 2;
                    } else if (_vAsp < _bAsp) {
                        _contentW = _vr.height * _vAsp;
                        _contentL = _vr.left + (_vr.width - _contentW) / 2;
                    }
                    const _mx =       (_rawMouseClientX - _contentL) / (_contentW || 1);
                    const _my = 1.0 - (_rawMouseClientY - _contentT) / (_contentH || 1);
                    gl.uniform2f(uMouse, _mx, _my);
                }
                if (uStrength !== null) gl.uniform1f(uStrength, _getStrength());
                if (uLayers   !== null) gl.uniform1f(uLayers,   _getLayers());
                if (uZoom     !== null) gl.uniform1f(uZoom,     _scrollZoom);
                gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
                gl.bindVertexArray(null);
            }

            let _hasRenderedFrame = false;
            let _lastMouseX = _mouseX, _lastMouseY = _mouseY;

            // Paused render: re-draw uniforms (mouse etc.) without re-uploading textures.
            // preserveDrawingBuffer:true keeps the last frame in the framebuffer;
            // we only need to re-run the draw call with updated uniforms.
            function _doRenderPausedUniforms() {
                if (!_hasRenderedFrame) { doRender(); return; }
                _reparentCanvas();
                const pr = (canvas.parentElement || document.body).getBoundingClientRect();
                const r = video.getBoundingClientRect();
                if (!r || r.width < 1) { canvas.style.display = 'none'; return; }
                canvas.style.display = 'block';
                canvas.style.position = 'absolute';
                canvas.style.left = (r.left - pr.left) + 'px';
                canvas.style.top  = (r.top  - pr.top)  + 'px';
                canvas.style.width  = r.width  + 'px';
                canvas.style.height = r.height + 'px';
                // Re-bind textures from preserveDrawingBuffer and redraw with new uniforms
                gl.useProgram(program);
                gl.bindVertexArray(vao);
                if (uMouse !== null) {
                    const _vr = video.getBoundingClientRect();
                    // Letterbox correction: map mouse into actual content area, not full BCR
                    const _vAsp = video.videoWidth / (video.videoHeight || 1);
                    const _bAsp = _vr.width / (_vr.height || 1);
                    let _contentL = _vr.left, _contentT = _vr.top;
                    let _contentW = _vr.width, _contentH = _vr.height;
                    if (_vAsp > _bAsp) {
                        _contentH = _vr.width / _vAsp;
                        _contentT = _vr.top + (_vr.height - _contentH) / 2;
                    } else if (_vAsp < _bAsp) {
                        _contentW = _vr.height * _vAsp;
                        _contentL = _vr.left + (_vr.width - _contentW) / 2;
                    }
                    const _mx =       (_rawMouseClientX - _contentL) / (_contentW || 1);
                    const _my = 1.0 - (_rawMouseClientY - _contentT) / (_contentH || 1);
                    gl.uniform2f(uMouse, _mx, _my);
                }
                if (uTime     !== null) gl.uniform1f(uTime, performance.now() * 0.001);
                if (uStrength !== null) gl.uniform1f(uStrength, _getStrength());
                if (uLayers   !== null) gl.uniform1f(uLayers,   _getLayers());
                if (uZoom     !== null) gl.uniform1f(uZoom,     _scrollZoom);
                gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
                gl.bindVertexArray(null);
            }

            function drawLoop() {
                if (!alive) return;
                requestAnimationFrame(drawLoop);
                const paused = !video || video.paused || video.ended;
                if (paused) {
                    // Always redraw on pause so mouse-driven effects (loupe etc.) keep working
                    _doRenderPausedUniforms();
                    lastPaused = true;
                    return;
                }
                lastPaused = false;
                doRender();
                _hasRenderedFrame = true;
            }

            // Also render immediately on pause/seek so the frame stays visible
            const _onPause  = () => { if (alive) doRender(); };
            const _onSeek   = () => { if (alive) doRender(); };
            video.addEventListener('pause',    _onPause,  { passive: true });
            video.addEventListener('seeked',   _onSeek,   { passive: true });
            video.addEventListener('loadeddata', _onSeek, { passive: true });

            const inst = { canvas, gl, program, texture, textureRaw, vao, vb, ub, sig: entry.id + '||' + entry.code, _stop: () => {
                alive = false;
                try { video.removeEventListener('pause',     _onPause); } catch(_){}
                try { video.removeEventListener('seeked',    _onSeek);  } catch(_){}
                try { video.removeEventListener('loadeddata',_onSeek);  } catch(_){}
            }};
            requestAnimationFrame(drawLoop);

            // Insert canvas directly after video — DOM order ensures canvas paints
            // above video but controls (later siblings) paint above canvas.
            const _initParent = video.parentElement || document.body;
            _initParent.insertBefore(canvas, video.nextSibling);



            return inst;
        }

        function _destroyInstance(inst) {
            try {
                if (inst._stop) inst._stop(); // also restores video opacity
                if (inst.canvas && inst.canvas.isConnected) inst.canvas.remove();
                const gl = inst.gl;
                if (gl) {
                    if (inst.texture)    gl.deleteTexture(inst.texture);
                    if (inst.textureRaw) gl.deleteTexture(inst.textureRaw);
                    if (inst.program) gl.deleteProgram(inst.program);
                    if (inst.vao) gl.deleteVertexArray(inst.vao);
                    if (inst.vb) gl.deleteBuffer(inst.vb);
                    if (inst.ub) gl.deleteBuffer(inst.ub);
                }
            } catch (_) {}
        }

        function update(video) {
            const activeEntries = customSvgCodes.filter(e => e && e.enabled && e.type === 'webgl');

            // Remove instances for entries that are gone/disabled
            for (const [id, inst] of _instances.entries()) {
                const still = activeEntries.find(e => e.id === id);
                if (!still) { _destroyInstance(inst); _instances.delete(id); }
            }

            if (!video) return;

            // Add/update instances for active entries
            for (const entry of activeEntries) {
                const sig = entry.id + '||' + entry.code;
                const existing = _instances.get(entry.id);
                if (existing) {
                    // Recompile if code changed
                    if (existing.sig !== sig) {
                        _destroyInstance(existing);
                        _instances.delete(entry.id);
                    } else {
                        // Ensure canvas is still in DOM (directly after video)
                        if (!existing.canvas.isConnected) {
                            const parent = video.parentElement || document.body;
                            parent.insertBefore(existing.canvas, video.nextSibling);
                        }
                        continue;
                    }
                }
                const inst = _createInstance(entry, video);
                if (inst) _instances.set(entry.id, inst);
            }
        }

        function destroyAll() {
            for (const inst of _instances.values()) _destroyInstance(inst);
            _instances.clear();
        }

        function reparentAll() {
            // Handled per-frame in _reparentCanvas() — no-op here
        }

        return { update, destroyAll, reparentAll };
    })();

    // Try-compile a GLSL fragment shader and return null on success, error string on failure
    function validateGlslCode(src) {
        try {
            const canvas = document.createElement('canvas');
            const gl = canvas.getContext('webgl2');
            if (!gl) return null;
            // Apply same normalization as _buildFragSrc/_normalizeUserFrag
            let s = src.replace(/^\s*#version\s+\S+[\t ]*/mg, '');
            s = s.replace(/^\s*precision\s+\w+\s+\w+\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+sampler2D\s+u_video\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+sampler2D\s+u_video_raw\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+vec2\s+u_res\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_time\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+vec2\s+u_mouse\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_strength\s*;\s*/mg, '');
            s = s.replace(/^\s*uniform\s+float\s+u_layers\s*;\s*/mg, '');
            s = s.replace(/^\s*in\s+vec2\s+v_uv\s*;\s*/mg, '');
            s = s.replace(/^\s*out\s+vec4\s+fragColor\s*;\s*/mg, '');
            s = s.replace(/\btexture2D\b/g, 'texture');
            s = s.replace(/\biResolution\b/g, 'vec3(u_res, 0.0)');
            s = s.replace(/\biTime\b/g, 'u_time');
            s = s.replace(/\biChannel0\b/g, 'u_video');
            s = s.replace(/\biChannel1\b/g, 'u_video_raw');
            if (/\bmainImage\s*\(/.test(s) && !s.includes('void main')) {
                s = s + '\nvoid main(){\n    mainImage(fragColor, v_uv * u_res);\n}';
            } else if (!s.includes('void main')) {
                // helper-only: find last non-void function and wrap
                const fnRe = /\b(vec4|vec3|vec2|float)\s+(\w+)\s*\(([^)]*)\)/g;
                let lastFn = null, m;
                while ((m = fnRe.exec(s)) !== null) lastFn = { ret: m[1], name: m[2], params: m[3] };
                if (lastFn) {
                    let vec2Count = 0;
                    const args = lastFn.params.split(',').map(p => {
                        const type = (p.trim().split(/\s+/)[0] || '');
                        if (type === 'sampler2D') return 'u_video';
                        if (type === 'vec2') return vec2Count++ === 0 ? 'v_uv' : 'u_res';
                        if (type === 'float') return '1.0';
                        return '0.0';
                    }).filter(Boolean).join(', ');
                    const call = `${lastFn.name}(${args})`;
                    const assign = lastFn.ret === 'vec4' ? `fragColor = ${call};`
                        : lastFn.ret === 'vec3' ? `fragColor = vec4(${call}, 1.0);`
                        : `float _r = ${call}; fragColor = vec4(_r,_r,_r,1.0);`;
                    s = s + `\nvoid main(){\n    ${assign}\n}`;
                } else {
                    s = `void main(){\n${s}\n}`;
                }
            }
            const fragSrc = `#version 300 es\nprecision highp float;\nuniform sampler2D u_video;\nuniform sampler2D u_video_raw;\nuniform vec2 u_res;\nuniform float u_time;\nuniform vec2 u_mouse;\nuniform float u_strength;\nuniform float u_layers;\nuniform float u_zoom;\nin vec2 v_uv;\nout vec4 fragColor;\n${s}`;
            const sh = gl.createShader(gl.FRAGMENT_SHADER);
            gl.shaderSource(sh, fragSrc);
            gl.compileShader(sh);
            const ok = gl.getShaderParameter(sh, gl.COMPILE_STATUS);
            const info = ok ? null : (gl.getShaderInfoLog(sh) || 'Unknown error');
            gl.deleteShader(sh);
            return info;
        } catch (e) {
            return null;
        }
    }

    function updateCustomWebglOverlays() {
        if (isFirefox()) return; // Custom Filter Codes not supported in Firefox
        const video = getWebglPrimaryVideo() || getGpuPrimaryVideo() || getHudPrimaryVideo();
        CustomWebglOverlayManager.update(video);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Canvas 2D Overlay Manager
    // Handles type:'canvas2d' entries — each gets its own fullscreen canvas
    // positioned directly after the <video> element (same as WebGL manager).
    // User code receives: ctx, canvas, video, width, height, frame, u_mouse, u_zoom
    // ─────────────────────────────────────────────────────────────────────────
    const CustomCanvas2DOverlayManager = (() => {
        // Map: entry.id -> { canvas, fn, rafId, lastTime }
        const _instances = new Map();

        // Trusted Types (e.g. YouTube) block new Function().
        // GM_addElement injects a <script> bypassing CSP/Trusted Types — it's
        // executed in the page context synchronously before the next microtask.
        function _compileUserFn(code) {
            const args = ['ctx','canvas','video','width','height','frame','u_mouse','u_zoom'];

            // 1. Direct new Function (works on most sites)
            try { return new Function(...args, code); } catch(e) {
                if (!String(e).includes('Trusted') && !(e instanceof EvalError)) throw e;
            }

            // 2. unsafeWindow.Function (Tampermonkey sandbox)
            try {
                if (typeof unsafeWindow !== 'undefined') {
                    return new unsafeWindow.Function(...args, code);
                }
            } catch(_) {}

            // 3. GM_addElement with synchronous script injection
            try {
                if (typeof GM_addElement === 'function') {
                    const key = '__gvfFn_' + Math.random().toString(36).slice(2);
                    const wrap = `window["${key}"]=function(${args.join(',')}){${code}}`;
                    // Use a blob URL for synchronous execution
                    const blob = new Blob([wrap], { type: 'text/javascript' });
                    const url = URL.createObjectURL(blob);
                    const script = document.createElement('script');
                    script.src = url;
                    // Synchronous: append and wait for load
                    let done = false;
                    script.onload = () => { done = true; };
                    (document.head || document.documentElement).appendChild(script);
                    // Poll briefly for sync execution (scripts from blob URLs execute sync in same tick on some browsers)
                    const fn = unsafeWindow[key] || window[key];
                    script.remove();
                    URL.revokeObjectURL(url);
                    if (fn) { delete unsafeWindow[key]; return fn; }
                    // Fallback: GM_addElement textContent
                    GM_addElement('script', { textContent: wrap });
                    const fn2 = unsafeWindow[key];
                    delete unsafeWindow[key];
                    if (typeof fn2 === 'function') return fn2;
                }
            } catch(_) {}

            return null;
        }

        function _createInstance(entry, video) {
            let fn;
            try {
                fn = _compileUserFn(entry.code);
            } catch (e) {
                console.warn('[GVF Canvas2D] Compile error for', entry.label, e);
                return null;
            }
            if (!fn) {
                console.warn('[GVF Canvas2D] Could not compile:', entry.label);
                return null;
            }
            const canvas = document.createElement('canvas');
            canvas.style.cssText = 'display:none;position:absolute;pointer-events:none;z-index:5;';
            canvas.setAttribute('data-gvf-custom-canvas2d', entry.id);

            function _reparentCanvas() {
                const parent = video.parentElement || document.body;
                if (canvas.parentNode !== parent || canvas.previousSibling !== video) {
                    const after = video.nextSibling;
                    parent.insertBefore(canvas, after !== canvas ? after : null);
                }
            }
            _reparentCanvas();

            let lastTime = null;
            let alive = true;

            function drawLoop(now) {
                if (!alive) return;

                if (!video || !video.isConnected || video.readyState < 2) {
                    canvas.style.display = 'none';
                    inst.rafId = requestAnimationFrame(drawLoop);
                    return;
                }

                _reparentCanvas();

                const pr = (canvas.parentElement || document.body).getBoundingClientRect();
                const vr = video.getBoundingClientRect();
                if (!vr || vr.width < 1 || vr.height < 1) {
                    canvas.style.display = 'none';
                    inst.rafId = requestAnimationFrame(drawLoop);
                    return;
                }

                canvas.style.display   = 'block';
                canvas.style.position  = 'absolute';
                canvas.style.left      = (vr.left - pr.left) + 'px';
                canvas.style.top       = (vr.top  - pr.top)  + 'px';
                canvas.style.width     = vr.width  + 'px';
                canvas.style.height    = vr.height + 'px';

                const dpr = window.devicePixelRatio || 1;
                const w = Math.round(vr.width  * dpr);
                const h = Math.round(vr.height * dpr);
                if (canvas.width !== w || canvas.height !== h) {
                    canvas.width = w;
                    canvas.height = h;
                }

                const frameMs = lastTime !== null ? now - lastTime : 0;
                lastTime = now;

                const ctx2d = canvas.getContext('2d');
                ctx2d.clearRect(0, 0, w, h);

                // Letterbox: map canvas coords to actual video content area
                const vAspect = (video.videoWidth || w) / (video.videoHeight || h);
                const cAspect = w / h;
                let vx = 0, vy = 0, vw = w, vh = h;
                if (vAspect > cAspect) {
                    vh = Math.round(w / vAspect);
                    vy = Math.round((h - vh) / 2);
                } else {
                    vw = Math.round(h * vAspect);
                    vx = Math.round((w - vw) / 2);
                }

                ctx2d.save();

                const rawMx = (typeof _rawMouseClientX !== 'undefined' ? _rawMouseClientX : 0);
                const rawMy = (typeof _rawMouseClientY !== 'undefined' ? _rawMouseClientY : 0);
                // Map mouse into vw/vh pixel space (letterbox-corrected, DPR-scaled)
                const relX = ((rawMx - vr.left) / vr.width  * w - vx) / vw;
                const relY = 1.0 - ((rawMy - vr.top)  / vr.height * h - vy) / vh;
                const u_mouse = { x: Math.max(0, Math.min(1, relX)), y: Math.max(0, Math.min(1, relY)) };
                const u_zoom  = (typeof _scrollZoom !== 'undefined' ? _scrollZoom : 1.0);

                ctx2d.translate(vx, vy);
                try {
                    fn(ctx2d, canvas, video, vw, vh, frameMs, u_mouse, u_zoom);
                } catch (e) { /* silently skip */ }

                ctx2d.restore();
                inst.rafId = requestAnimationFrame(drawLoop);
            }

            const inst = { canvas, fn, rafId: requestAnimationFrame(drawLoop), alive };
            inst._stop = () => { alive = false; };
            return inst;
        }

        function _reparentCanvas(canvas, video) {
            if (!video) return;
            const parent = video.parentElement;
            if (!parent) return;
            if (canvas.parentElement !== parent) {
                parent.insertBefore(canvas, video.nextSibling);
            }
        }

        function _destroyInstance(inst) {
            if (inst._stop) inst._stop();
            if (inst.rafId) cancelAnimationFrame(inst.rafId);
            if (inst.canvas && inst.canvas.isConnected) inst.canvas.remove();
        }

        function update(video) {
            const activeEntries = customSvgCodes.filter(e => e && e.enabled && e.type === 'canvas2d');
            const activeIds = new Set(activeEntries.map(e => e.id));

            // Remove stale instances
            for (const [id, inst] of _instances) {
                if (!activeIds.has(id)) {
                    _destroyInstance(inst);
                    _instances.delete(id);
                }
            }

            if (!video) return;

            // Create new instances
            for (const entry of activeEntries) {
                if (!_instances.has(entry.id)) {
                    const inst = _createInstance(entry, video);
                    if (inst) _instances.set(entry.id, inst);
                }
            }
        }

        function reparentAll() {
            const video = getWebglPrimaryVideo() || getGpuPrimaryVideo() || getHudPrimaryVideo();
            if (!video) return;
            for (const inst of _instances.values()) {
                const parent = video.parentElement || document.body;
                if (inst.canvas.parentNode !== parent || inst.canvas.previousSibling !== video) {
                    const after = video.nextSibling;
                    parent.insertBefore(inst.canvas, after !== inst.canvas ? after : null);
                }
            }
        }

        function destroyAll() {
            for (const inst of _instances.values()) _destroyInstance(inst);
            _instances.clear();
        }

        return { update, reparentAll, destroyAll, _compileUserFn };
    })();

    // ─────────────────────────────────────────────────────────────────────────
    // MediaPipe Loader
    // Loads MediaPipe UMD bundles via GM_addElement (bypasses CSP/Trusted Types).
    // Globals land on unsafeWindow after load.
    // Usage from a canvas2d overlay entry:
    //   const mp = await unsafeWindow.__gvfMP('face_mesh');
    //   mp.FaceMesh, mp.FACEMESH_TESSELATION, mp.drawConnectors
    // ─────────────────────────────────────────────────────────────────────────
    (function initGvfMediaPipe() {
        if (unsafeWindow.__gvfMP) return;

        // Pinned UMD-compatible versions — unpinned jsdelivr paths resolve to
        // newer ES-module builds that throw "Cannot use import statement outside a module".
        // These specific versions ship proper IIFE/UMD bundles.
        const BASE = 'https://cdn.jsdelivr.net/npm/@mediapipe/';
        const VERS = {
            drawing_utils:       '0.3.1620248257',
            face_mesh:           '0.4.1633559619',
            hands:               '0.4.1646424915',
            pose:                '0.5.1675469404',
            selfie_segmentation: '0.1.1675465747',
        };
        const CDN = (pkg) => `${BASE}${pkg}@${VERS[pkg]}/`;

        const PACKAGES = {
            face_mesh: {
                scripts: [CDN('drawing_utils') + 'drawing_utils.js',
                          CDN('face_mesh')     + 'face_mesh.js'],
                globals: ['FaceMesh', 'FACEMESH_TESSELATION', 'FACEMESH_RIGHT_EYE',
                          'FACEMESH_LEFT_EYE', 'FACEMESH_LIPS', 'drawConnectors', 'drawLandmarks'],
                locateFile: (f) => CDN('face_mesh') + f,
            },
            hands: {
                scripts: [CDN('drawing_utils') + 'drawing_utils.js',
                          CDN('hands')         + 'hands.js'],
                globals: ['Hands', 'HAND_CONNECTIONS', 'drawConnectors', 'drawLandmarks'],
                locateFile: (f) => CDN('hands') + f,
            },
            pose: {
                scripts: [CDN('drawing_utils') + 'drawing_utils.js',
                          CDN('pose')          + 'pose.js'],
                globals: ['Pose', 'POSE_CONNECTIONS', 'drawConnectors', 'drawLandmarks'],
                locateFile: (f) => CDN('pose') + f,
            },
            selfie_segmentation: {
                scripts: [CDN('selfie_segmentation') + 'selfie_segmentation.js'],
                globals: ['SelfieSegmentation'],
                locateFile: (f) => CDN('selfie_segmentation') + f,
            },
        };

        const _loaded  = new Set();
        const _pending = new Map();

        function _loadScript(src) {
            if (_loaded.has(src)) return Promise.resolve();
            if (_pending.has(src)) return _pending.get(src);
            const p = new Promise((resolve, reject) => {
                try {
                    const el = GM_addElement('script', { src, async: false });
                    el.onload  = () => { _loaded.add(src); resolve(); };
                    el.onerror = () => reject(new Error('[GVF MediaPipe] Failed to load: ' + src));
                } catch(e) { reject(e); }
            });
            _pending.set(src, p);
            p.finally(() => _pending.delete(src));
            return p;
        }

        // Returns an object with all globals + locateFile for the requested package.
        // Resolves after all scripts are loaded.
        async function loadPackage(name) {
            const pkg = PACKAGES[name];
            if (!pkg) throw new Error('[GVF MediaPipe] Unknown package: ' + name + '. Available: ' + Object.keys(PACKAGES).join(', '));

            for (const src of pkg.scripts) {
                await _loadScript(src);
            }

            const result = { locateFile: pkg.locateFile };
            for (const g of pkg.globals) {
                result[g] = unsafeWindow[g] ?? null;
            }
            return result;
        }

        // Expose on both: unsafeWindow (Tampermonkey sandbox) and window (page context via GM_addElement/blob)
        unsafeWindow.__gvfMP = loadPackage;
        window.__gvfMP = loadPackage;
    })();

    // ── Deepgram STT ──────────────────────────────────────────────────────────
    // Runs natively in userscript context to bypass Trusted Types / CSP.
    // Canvas2D overlays configure and read from unsafeWindow.__gvfDG.
    (function initGvfDeepgram() {
        if (unsafeWindow.__gvfDG) return;
        unsafeWindow.__gvfDG = {
            ws: null, lines: [], interim: '',
            state: 'idle', error: null,
            mediaRecorder: null, audioCtx: null, stream: null,
            _apiKey: null, _lang: 'de', _maxLines: 5
        };

        const dg = unsafeWindow.__gvfDG;

        async function startDeepgram() {
            if (dg.state === 'connecting' || dg.state === 'running') return;
            if (!dg._apiKey) return;
            dg.state = 'connecting';
            dg.error = null;

            // Get fresh mic stream each time
            try {
                if (dg.stream) {
                    dg.stream.getTracks().forEach(t => t.stop());
                    dg.stream = null;
                }
                dg.stream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
            } catch(e) {
                dg.state = 'error';
                dg.error = 'Mic: ' + e.message;
                return;
            }

            const lang = dg._lang === 'auto' ? 'multi' : dg._lang;
            const url = `wss://api.deepgram.com/v1/listen?language=${lang}&model=nova-2&punctuate=true&interim_results=true&encoding=linear16&sample_rate=16000&channels=1&smart_format=true&no_delay=true`;
            const ws = new WebSocket(url, ['token', dg._apiKey]);
            dg.ws = ws;

            ws.onopen = async () => {
                dg.state = 'running';
                try {
                    const ac = new AudioContext();
                    dg.audioCtx = ac;
                    await ac.resume();
                    const actualRate = ac.sampleRate;
                    const targetRate = 16000;
                    const ratio = actualRate / targetRate;
                    const src = ac.createMediaStreamSource(dg.stream);

                    // AudioWorklet — more reliable than deprecated ScriptProcessor
                    const workletCode = `
                        class PCMProcessor extends AudioWorkletProcessor {
                            constructor() { super(); this._ratio = ${ratio}; this._buf = []; }
                            process(inputs) {
                                const input = inputs[0][0];
                                if (!input) return true;
                                for (let i = 0; i < input.length; i++) this._buf.push(input[i]);
                                const outLen = Math.floor(this._buf.length / this._ratio);
                                if (outLen > 0) {
                                    const out = new Float32Array(outLen);
                                    for (let i = 0; i < outLen; i++) {
                                        const pos = i * this._ratio;
                                        const idx = Math.floor(pos);
                                        const frac = pos - idx;
                                        const a = this._buf[idx] || 0;
                                        const b = this._buf[Math.min(idx+1, this._buf.length-1)] || 0;
                                        out[i] = a + frac * (b - a);
                                    }
                                    this._buf = this._buf.slice(Math.floor(outLen * this._ratio));
                                    this.port.postMessage(out);
                                }
                                return true;
                            }
                        }
                        registerProcessor('gvf-pcm', PCMProcessor);
                    `;
                    const blob = new Blob([workletCode], { type: 'application/javascript' });
                    const url = URL.createObjectURL(blob);
                    await ac.audioWorklet.addModule(url);
                    URL.revokeObjectURL(url);

                    const worklet = new AudioWorkletNode(ac, 'gvf-pcm');
                    worklet.port.onmessage = (e) => {
                        if (ws.readyState !== WebSocket.OPEN) return;
                        const float32 = e.data;
                        const int16 = new Int16Array(float32.length);
                        for (let i = 0; i < float32.length; i++)
                            int16[i] = Math.max(-32768, Math.min(32767, float32[i] * 32768));
                        ws.send(int16.buffer);
                    };
                    src.connect(worklet);
                    worklet.connect(ac.destination);
                    dg._worklet = worklet;
                    dg._src = src;
                    console.log('[GVF Deepgram] AudioWorklet started, rate:', actualRate, '→', targetRate);
                } catch(e) {
                    dg.error = 'Audio: ' + e.message;
                    dg.state = 'error';
                    console.error('[GVF Deepgram] Audio error:', e);
                }
            };

            ws.onmessage = (e) => {
                try {
                    const data = JSON.parse(e.data);
                    // UtteranceEnd — force flush interim as final
                    if (data.type === 'UtteranceEnd' && dg.interim) {
                        dg.lines.push(dg.interim.trim());
                        if (dg.lines.length > (dg._maxLines || 5)) dg.lines.shift();
                        dg.interim = '';
                        return;
                    }
                    const transcript = data?.channel?.alternatives?.[0]?.transcript || '';
                    if (!transcript) return;
                    if (data?.is_final) {
                        dg.lines.push(transcript.trim());
                        if (dg.lines.length > (dg._maxLines || 5)) dg.lines.shift();
                        dg.interim = '';
                    } else {
                        dg.interim = transcript;
                    }
                } catch(_) {}
            };

            ws.onerror = () => { dg.state = 'error'; dg.error = 'WS error'; };

            ws.onclose = (e) => {
                dg.state = 'idle';
                dg.interim = '';
                try { if (dg._worklet) { dg._worklet.disconnect(); } } catch(_) {}
                try { if (dg._src) { dg._src.disconnect(); } } catch(_) {}
                try { if (dg._processor) { dg._processor.disconnect(); } } catch(_) {}
                try { if (dg.audioCtx) { dg.audioCtx.close(); dg.audioCtx = null; } } catch(_) {}
                dg._worklet = null; dg._processor = null; dg._src = null;
                if (e.code === 1008) { dg.error = 'Invalid API key'; return; }
                setTimeout(startDeepgram, 1000);
            };
        }

        dg._start = startDeepgram;

        // Auto-reconnect every 20s to prevent Deepgram timeout
        setInterval(() => {
            if (dg.state === 'running' && dg._apiKey) {
                console.log('[GVF Deepgram] Refreshing connection...');
                try { if (dg.ws) dg.ws.close(1001, 'refresh'); } catch(_) {}
            }
            // Auto-start if idle with valid key
            if (dg.state === 'idle' && dg._apiKey && dg.error !== 'Invalid API key') {
                console.log('[GVF Deepgram] Auto-starting...');
                startDeepgram();
            }
        }, 20000);
    })();

    // Returns the best available filtered frame source for Canvas2D overlays.
    // WebGL mode: reuses __gvfFilteredFrame (already baked with full filter).
    // SVG mode: bakes CSS-only part (brightness/contrast/saturate etc.) into a cached canvas.
    function gvfGetFilteredSource(video) {
        if (window.__gvfFilteredFrame && window.__gvfFilteredFrame.width > 0) {
            return window.__gvfFilteredFrame;
        }
        // SVG mode fallback: apply CSS filter portion only (no SVG url())
        try {
            const w = video.videoWidth || video.clientWidth || 1280;
            const h = video.videoHeight || video.clientHeight || 720;
            if (!gvfGetFilteredSource._c) {
                gvfGetFilteredSource._c = document.createElement('canvas');
                gvfGetFilteredSource._x = gvfGetFilteredSource._c.getContext('2d', { alpha: false });
            }
            const c = gvfGetFilteredSource._c, x = gvfGetFilteredSource._x;
            if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
            let f = 'none';
            try {
                const s = document.getElementById('global-video-filter-style');
                if (s) {
                    const m = s.textContent.match(/filter\s*:\s*([^!;]+)/);
                    if (m && m[1]) f = m[1].trim().replace(/url\([^)]*\)/g, '').replace(/\s+/g, ' ').trim() || 'none';
                }
            } catch(_) {}
            x.filter = f;
            x.drawImage(video, 0, 0, w, h);
            return c;
        } catch(_) { return video; }
    }

    function updateCustomCanvas2DOverlays() {
        if (isFirefox()) return;
        const video = getWebglPrimaryVideo() || getGpuPrimaryVideo() || getHudPrimaryVideo();
        CustomCanvas2DOverlayManager.update(video);
    }

    function openCustomSvgModal() {
        if (isFirefox()) {
            alert('Custom Filter Codes are not supported in Firefox.\nThis feature requires WebGL2 capabilities that are currently unavailable in Firefox.');
            return;
        }
        const MODAL_ID = 'gvf-custom-svg-modal';
        const existing = document.getElementById(MODAL_ID);
        if (existing) { existing.remove(); return; }

        const modal = document.createElement('div');
        modal.id = MODAL_ID;
        modal.style.cssText = `position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);width:560px;max-width:96vw;max-height:85vh;background:rgba(18,18,22,0.98);border:2px solid #4a9eff;border-radius:14px;box-shadow:0 20px 60px rgba(0,0,0,0.85);color:#eaeaea;font-family:system-ui,sans-serif;z-index:2147483647;display:flex;flex-direction:column;padding:18px;user-select:none;pointer-events:auto;`;
        stopEventsOn(modal);

        // Header
        const hdr = document.createElement('div');
        hdr.style.cssText = `display:flex;justify-content:space-between;align-items:center;margin-bottom:14px;padding-bottom:10px;border-bottom:2px solid #4a9eff;flex-shrink:0;`;
        const htitle = document.createElement('div');
        htitle.textContent = '⬡ Custom Filter Codes (SVG / WebGL)';
        htitle.style.cssText = `font-size:16px;font-weight:900;color:#fff;text-shadow:0 0 8px #4a9eff;`;

        const hbtns = document.createElement('div');
        hbtns.style.cssText = `display:flex;gap:6px;align-items:center;flex-shrink:0;`;

        const libBtn = document.createElement('button');
        libBtn.textContent = '📚 Library';
        libBtn.title = 'Open SVG Filter Library';
        libBtn.style.cssText = `padding:4px 10px;background:rgba(100,180,255,0.18);color:#a0d4ff;border:1px solid rgba(100,180,255,0.45);border-radius:6px;font-size:16px;font-weight:900;cursor:pointer;`;
        libBtn.addEventListener('mouseenter', () => { libBtn.style.background = 'rgba(100,180,255,0.32)'; });
        libBtn.addEventListener('mouseleave', () => { libBtn.style.background = 'rgba(100,180,255,0.18)'; });
        libBtn.addEventListener('click', () => { window.open('https://svg.ts3x.cc/', '_blank'); });

        const hclose = document.createElement('button');
        hclose.textContent = '✕';
        hclose.style.cssText = `
        background: rgba(255, 255, 255, 0.1);
            border: none;
            color: #fff;
            font-size: 20px;
            cursor: pointer;
            width: 36px;
            height: 36px;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s;
            border: 1px solid rgba(255,255,255,0.2);`;
        hclose.addEventListener('click', () => modal.remove());

        hbtns.appendChild(libBtn);
        hbtns.appendChild(hclose);
        hdr.appendChild(htitle); hdr.appendChild(hbtns);
        modal.appendChild(hdr);

        // List area
        const listWrap = document.createElement('div');
        listWrap.style.cssText = `overflow-y:auto;max-height:220px;background:rgba(0,0,0,0.3);border-radius:8px;padding:6px;margin-bottom:12px;display:flex;flex-direction:column;gap:6px;flex-shrink:0;`;
        modal.appendChild(listWrap);

        let dragSrcIndex = null;

        function renderList() {
            const scrollTop = listWrap.scrollTop;
            while (listWrap.firstChild) listWrap.removeChild(listWrap.firstChild);
            if (!customSvgCodes.length) {
                const empty = document.createElement('div');
                empty.textContent = 'No entries yet. Add one below.';
                empty.style.cssText = `color:#888;font-size:12px;padding:10px;text-align:center;`;
                listWrap.appendChild(empty);
                return;
            }
            customSvgCodes.forEach((entry, i) => {
                const row = document.createElement('div');
                row.draggable = true;
                row.dataset.idx = String(i);
                row.style.cssText = `display:flex;align-items:center;gap:8px;padding:7px 10px;background:rgba(255,255,255,0.05);border-radius:8px;border:1px solid rgba(255,255,255,0.1);cursor:default;transition:opacity 0.15s,border-color 0.15s;`;

                // Drag handle
                const handle = document.createElement('div');
                handle.textContent = '⠿';
                handle.title = 'Drag to reorder';
                handle.style.cssText = `font-size:14px;color:#666;cursor:grab;flex-shrink:0;line-height:1;padding:0 2px;`;

                // Drag events
                row.addEventListener('dragstart', (e) => {
                    dragSrcIndex = i;
                    e.dataTransfer.effectAllowed = 'move';
                    setTimeout(() => { row.style.opacity = '0.4'; }, 0);
                });
                row.addEventListener('dragend', () => {
                    row.style.opacity = '1';
                    listWrap.querySelectorAll('[data-idx]').forEach(r => {
                        r.style.borderColor = 'rgba(255,255,255,0.1)';
                        r.style.borderStyle = 'solid';
                    });
                });
                row.addEventListener('dragover', (e) => {
                    e.preventDefault();
                    e.dataTransfer.dropEffect = 'move';
                    if (dragSrcIndex !== i) row.style.borderColor = '#4a9eff';
                });
                row.addEventListener('dragleave', () => {
                    row.style.borderColor = 'rgba(255,255,255,0.1)';
                });
                row.addEventListener('drop', (e) => {
                    e.preventDefault();
                    row.style.borderColor = 'rgba(255,255,255,0.1)';
                    if (dragSrcIndex === null || dragSrcIndex === i) return;
                    const moved = customSvgCodes.splice(dragSrcIndex, 1)[0];
                    customSvgCodes.splice(i, 0, moved);
                    dragSrcIndex = null;
                    saveCustomSvgCodes();
                    regenerateSvgImmediately();
                    renderList();
                });

                const chk = document.createElement('input');
                chk.type = 'checkbox';
                chk.checked = !!entry.enabled;
                chk.style.cssText = `width:16px;height:16px;accent-color:#4a9eff;cursor:pointer;flex-shrink:0;`;
                stopEventsOn(chk);
                chk.addEventListener('change', () => {
                    customSvgCodes[i].enabled = chk.checked;
                    saveCustomSvgCodes();
                    regenerateSvgImmediately();
                });

                const lbl = document.createElement('div');
                lbl.style.cssText = `flex:1;font-size:12px;font-weight:700;color:#d0e8ff;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;display:flex;align-items:center;gap:5px;`;
                const lblText = document.createElement('span');
                lblText.textContent = entry.label || 'Untitled';
                lblText.style.cssText = `overflow:hidden;text-overflow:ellipsis;white-space:nowrap;`;
                const typeBadge = document.createElement('span');
                typeBadge.textContent = entry.type === 'webgl' ? 'GLSL' : entry.type === 'canvas2d' ? '2D' : 'SVG';
                typeBadge.style.cssText = `flex-shrink:0;font-size:9px;font-weight:900;padding:1px 5px;border-radius:4px;${entry.type === 'webgl' ? 'background:rgba(120,80,255,0.3);color:#c0a0ff;border:1px solid rgba(120,80,255,0.5);' : entry.type === 'canvas2d' ? 'background:rgba(80,200,120,0.25);color:#80e8a0;border:1px solid rgba(80,200,120,0.5);' : 'background:rgba(74,158,255,0.15);color:#7ab8ff;border:1px solid rgba(74,158,255,0.35);'}`;
                lbl.appendChild(lblText);
                lbl.appendChild(typeBadge);

                // Hotkey badge in label
                if (entry.hotkey) {
                    const hkBadge = document.createElement('span');
                    hkBadge.textContent = entry.hotkey.toUpperCase();
                    hkBadge.title = 'Hotkey: ' + entry.hotkey.toUpperCase();
                    hkBadge.style.cssText = `flex-shrink:0;font-size:9px;font-weight:900;padding:1px 5px;border-radius:4px;background:rgba(255,200,80,0.15);color:#ffc850;border:1px solid rgba(255,200,80,0.4);font-family:monospace;`;
                    lbl.appendChild(hkBadge);
                }

                // Hotkey button
                const hkBtn = document.createElement('button');
                hkBtn.textContent = '⌨';
                hkBtn.title = entry.hotkey ? `Hotkey: ${entry.hotkey.toUpperCase()} (click to change/clear)` : 'Set hotkey';
                hkBtn.style.cssText = `padding:3px 8px;background:${entry.hotkey ? 'rgba(255,200,80,0.18)' : 'rgba(255,255,255,0.07)'};color:${entry.hotkey ? '#ffc850' : '#888'};border:1px solid ${entry.hotkey ? 'rgba(255,200,80,0.4)' : 'rgba(255,255,255,0.15)'};border-radius:5px;font-size:12px;cursor:pointer;`;
                stopEventsOn(hkBtn);
                hkBtn.addEventListener('click', () => {
                    // If already has hotkey, offer clear or re-assign
                    const existing2 = customSvgCodes[i].hotkey;

                    // Create inline capture overlay on the button
                    const popup = document.createElement('div');
                    popup.style.cssText = `position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.7);`;
                    const box = document.createElement('div');
                    box.style.cssText = `background:rgba(18,18,22,0.98);border:2px solid #ffc850;border-radius:12px;padding:20px 28px;display:flex;flex-direction:column;align-items:center;gap:12px;font-family:system-ui,sans-serif;color:#eaeaea;min-width:240px;`;
                    const title2 = document.createElement('div');
                    title2.textContent = '⌨ Set Hotkey';
                    title2.style.cssText = `font-size:14px;font-weight:900;color:#ffc850;`;
                    const sub = document.createElement('div');
                    sub.textContent = `Filter: ${entry.label || 'Untitled'}`;
                    sub.style.cssText = `font-size:11px;color:#888;`;
                    const hint = document.createElement('div');
                    hint.textContent = 'Press any key to assign…';
                    hint.style.cssText = `font-size:13px;color:#ccc;text-align:center;`;
                    const clearBtn2 = document.createElement('button');
                    clearBtn2.textContent = existing2 ? `Clear (${existing2.toUpperCase()})` : 'Cancel';
                    clearBtn2.style.cssText = `padding:6px 16px;background:rgba(255,80,80,0.15);color:#ff8080;border:1px solid rgba(255,80,80,0.4);border-radius:7px;font-size:12px;cursor:pointer;`;
                    box.appendChild(title2); box.appendChild(sub); box.appendChild(hint); box.appendChild(clearBtn2);
                    popup.appendChild(box);
                    document.body.appendChild(popup);

                    const onKey2 = (e2) => {
                        e2.preventDefault(); e2.stopPropagation();
                        const key = e2.key;
                        // Ignore modifier-only keys
                        if (['Control','Alt','Shift','Meta','CapsLock','Tab','Escape'].includes(key)) {
                            if (key === 'Escape') { cleanup(); }
                            return;
                        }
                        customSvgCodes[i].hotkey = key.toLowerCase();
                        saveCustomSvgCodes();
                        cleanup();
                        renderList();
                    };
                    const cleanup = () => {
                        document.removeEventListener('keydown', onKey2, true);
                        popup.remove();
                    };
                    clearBtn2.addEventListener('click', () => {
                        customSvgCodes[i].hotkey = '';
                        saveCustomSvgCodes();
                        cleanup();
                        renderList();
                    });
                    popup.addEventListener('click', (e2) => { if (e2.target === popup) cleanup(); });
                    document.addEventListener('keydown', onKey2, true);
                });

                const editBtn = document.createElement('button');
                editBtn.textContent = '✏';
                editBtn.title = 'Edit';
                editBtn.style.cssText = `padding:3px 8px;background:rgba(100,180,255,0.18);color:#a0d4ff;border:1px solid rgba(100,180,255,0.4);border-radius:5px;font-size:12px;cursor:pointer;`;
                stopEventsOn(editBtn);
                editBtn.addEventListener('click', () => renderEditArea(i));

                const delBtn = document.createElement('button');
                delBtn.textContent = '🗑';
                delBtn.title = 'Delete';
                delBtn.style.cssText = `padding:3px 8px;background:rgba(255,80,80,0.15);color:#ff8080;border:1px solid rgba(255,80,80,0.4);border-radius:5px;font-size:12px;cursor:pointer;`;
                stopEventsOn(delBtn);
                delBtn.addEventListener('click', () => {
                    customSvgCodes.splice(i, 1);
                    saveCustomSvgCodes();
                    regenerateSvgImmediately();
                    renderList();
                    renderEditArea();
                });

                row.appendChild(handle); row.appendChild(chk); row.appendChild(lbl); row.appendChild(hkBtn); row.appendChild(editBtn); row.appendChild(delBtn);
                listWrap.appendChild(row);
            });
            listWrap.scrollTop = scrollTop;
        }

        // Allow dropping onto the list container itself (drop at end)
        listWrap.addEventListener('dragover', (e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'move'; });
        listWrap.addEventListener('drop', (e) => {
            e.preventDefault();
            if (dragSrcIndex === null) return;
            // Only fires if dropped on listWrap background (not a row) → move to end
            const moved = customSvgCodes.splice(dragSrcIndex, 1)[0];
            customSvgCodes.push(moved);
            dragSrcIndex = null;
            saveCustomSvgCodes();
            regenerateSvgImmediately();
            renderList();
        });

        // Edit / Add form
        const editArea = document.createElement('div');
        editArea.style.cssText = `display:flex;flex-direction:column;gap:8px;flex-shrink:0;`;
        modal.appendChild(editArea);

        function renderEditArea(idx) {
            const editing = (idx !== undefined && idx >= 0);
            while (editArea.firstChild) editArea.removeChild(editArea.firstChild);

            const currentType = editing ? (customSvgCodes[idx].type || 'svg') : 'svg';

            const formTitle = document.createElement('div');
            formTitle.textContent = editing ? `✏ Edit: ${customSvgCodes[idx].label}` : '➕ Add new Custom Filter';
            formTitle.style.cssText = `font-size:12px;font-weight:900;color:#4a9eff;`;
            editArea.appendChild(formTitle);

            // Row: Label + Type Dropdown
            const topRow = document.createElement('div');
            topRow.style.cssText = `display:flex;gap:8px;align-items:center;`;

            const labelInput = document.createElement('input');
            labelInput.type = 'text';
            labelInput.placeholder = 'Label (e.g. "Sobel Sketch")';
            labelInput.value = editing ? (customSvgCodes[idx].label || '') : '';
            labelInput.style.cssText = `flex:1;background:rgba(0,0,0,0.5);border:1px solid rgba(255,255,255,0.2);border-radius:7px;padding:7px 10px;color:#fff;font-size:12px;outline:none;box-sizing:border-box;`;
            stopEventsOn(labelInput);

            const typeSelect = document.createElement('select');
            typeSelect.style.cssText = `background:rgba(0,0,0,0.7);border:1px solid rgba(100,180,255,0.5);border-radius:7px;padding:6px 10px;color:#a0d4ff;font-size:12px;font-weight:700;outline:none;cursor:pointer;flex-shrink:0;`;
            stopEventsOn(typeSelect);
            [['svg', '⬡ SVG'], ['webgl', '⬡ WebGL/GLSL'], ['canvas2d', '⬡ Canvas 2D']].forEach(([val, lbl]) => {
                const opt = document.createElement('option');
                opt.value = val; opt.textContent = lbl;
                if (val === currentType) opt.selected = true;
                typeSelect.appendChild(opt);
            });

            topRow.appendChild(labelInput);
            topRow.appendChild(typeSelect);
            editArea.appendChild(topRow);

            const svgPlaceholder = 'SVG Filter-Primitive Code, e.g.:\n<feConvolveMatrix kernelMatrix="0 -1 0 -1 5 -1 0 -1 0"/>';
            const glslPlaceholder = `GLSL Fragment Shader (WebGL2 / GLSL300).\nAvailable uniforms:\n  uniform sampler2D u_video;  // video frame\n  uniform vec2 u_res;          // canvas resolution (px)\n  in vec2 v_uv;                // UV coords 0..1\n  out vec4 fragColor;\n\n// Option A — full shader:\nvoid main(){\n    fragColor = texture(u_video, v_uv);\n}\n\n// Option B — helper function only (main is auto-generated):\nvec3 myEffect(sampler2D tex, vec2 uv, vec2 res) {\n    return texture(tex, uv).rgb;\n}`;
            const canvas2dPlaceholder = `// Canvas 2D effect\n// Available variables: ctx, canvas, video, width, height, frame (ms), u_mouse ({x,y}), u_zoom\n\n// Example: watch timer (bottom right)\nif (!canvas._watchStart) canvas._watchStart = Date.now();\nconst elapsed = Math.floor((Date.now() - canvas._watchStart) / 1000);\nconst h = Math.floor(elapsed / 3600);\nconst m = Math.floor((elapsed % 3600) / 60);\nconst s = elapsed % 60;\nconst pad = n => String(n).padStart(2, '0');\nconst label = h > 0 ? \`\${pad(h)}:\${pad(m)}:\${pad(s)}\` : \`\${pad(m)}:\${pad(s)}\`;\nconst fs = Math.round(height * 0.038);\nctx.font = \`900 \${fs}px monospace\`;\nconst text = '👁 ' + label;\nconst tw = ctx.measureText(text).width;\nconst px = width - tw - fs * 0.6;\nconst py = height - fs * 0.6;\nctx.fillStyle = 'rgba(0,0,0,0.55)';\nconst rp = fs * 0.3;\nctx.beginPath();\nctx.roundRect(px - rp, py - fs - rp, tw + rp*2, fs + rp*2, rp);\nctx.fill();\nctx.fillStyle = '#fff';\nctx.fillText(text, px, py);`;
            const getPlaceholder = t => t === 'webgl' ? glslPlaceholder : t === 'canvas2d' ? canvas2dPlaceholder : svgPlaceholder;

            const codeInput = document.createElement('textarea');
            codeInput.placeholder = getPlaceholder(currentType);
            codeInput.value = editing ? (customSvgCodes[idx].code || '') : '';
            codeInput.style.cssText = `width:100%;height:120px;background:rgba(0,0,0,0.5);border:1px solid rgba(255,255,255,0.2);border-radius:7px;padding:8px 10px;color:#d0ffb0;font-size:11px;font-family:monospace;outline:none;resize:vertical;box-sizing:border-box;`;
            stopEventsOn(codeInput);
            editArea.appendChild(codeInput);

            typeSelect.addEventListener('change', () => {
                codeInput.placeholder = getPlaceholder(typeSelect.value);
            });

            const errMsg = document.createElement('div');
            errMsg.style.cssText = `font-size:11px;color:#ff7070;min-height:14px;`;
            editArea.appendChild(errMsg);

            const btnRow = document.createElement('div');
            btnRow.style.cssText = `display:flex;gap:8px;justify-content:flex-end;`;

            if (editing) {
                const cancelBtn = document.createElement('button');
                cancelBtn.textContent = 'Cancel';
                cancelBtn.style.cssText = `padding:7px 14px;background:rgba(255,255,255,0.1);color:#ccc;border:1px solid rgba(255,255,255,0.2);border-radius:7px;font-size:12px;cursor:pointer;`;
                stopEventsOn(cancelBtn);
                cancelBtn.addEventListener('click', () => renderEditArea());
                btnRow.appendChild(cancelBtn);
            }

            const saveBtn = document.createElement('button');
            saveBtn.textContent = editing ? '💾 Save' : '➕ Add';
            saveBtn.style.cssText = `padding:7px 16px;background:#2a6fdb;color:#fff;border:none;border-radius:7px;font-size:12px;font-weight:900;cursor:pointer;`;
            stopEventsOn(saveBtn);
            saveBtn.addEventListener('click', () => {
                const label = labelInput.value.trim() || 'Untitled';
                const code = codeInput.value.trim();
                const type = typeSelect.value;
                if (!code) { errMsg.textContent = 'Code must not be empty.'; return; }

                if (type === 'svg') {
                    const parsed = parseCustomSvgCode(code);
                    if (!parsed) { errMsg.textContent = '❌ Invalid SVG code — parse error.'; return; }
                } else if (type === 'canvas2d') {
                    try {
                        const testFn = CustomCanvas2DOverlayManager._compileUserFn
                            ? CustomCanvas2DOverlayManager._compileUserFn(code)
                            : new Function('ctx', 'canvas', 'video', 'width', 'height', 'frame', 'u_mouse', 'u_zoom', code);
                        if (!testFn) throw new Error('Compilation failed');
                    } catch (e) {
                        errMsg.textContent = '❌ Canvas 2D error: ' + e.message; return;
                    }
                } else {
                    // WebGL: try-compile for immediate feedback
                    errMsg.textContent = '⏳ Validating shader…';
                    const glslErr = validateGlslCode(code);
                    if (glslErr) { errMsg.textContent = '❌ GLSL error: ' + glslErr.split('\n').slice(0,3).join(' | '); return; }
                }

                errMsg.textContent = '';
                if (editing) {
                    customSvgCodes[idx].label = label;
                    customSvgCodes[idx].code = code;
                    customSvgCodes[idx].type = type;
                } else {
                    customSvgCodes.push({ id: 'csvg_' + Date.now(), label, code, type, enabled: true });
                }
                saveCustomSvgCodes();
                regenerateSvgImmediately();
                updateCustomWebglOverlays();
                updateCustomCanvas2DOverlays();
                renderList();
                renderEditArea();
            });
            btnRow.appendChild(saveBtn);
            editArea.appendChild(btnRow);
        }

        renderList();
        renderEditArea();
        makeFloatingManagerDraggable(modal, hdr, 'gvf_custom_svg_modal_pos');
        // Expose renderList so the sync handler can refresh the modal live
        modal._gvfRenderList = renderList;
        const _fsEl = getFsEl();
        (_fsEl || document.body || document.documentElement).appendChild(modal);
    }

    // -------------------------
    // Bulk-update guard
    // -------------------------
    let _inSync = false;
    let _suspendSync = false;

    // Debug/Load settings from storage
    logs = !!gmGet(K.LOGS, true);
    debug = !!gmGet(K.DEBUG, false); // Default false

    // -------------------------
    // User Profile Management
    // -------------------------
    let userProfiles = [];
    let activeUserProfile = null;
    let _lastProfileStorageRev = 0;
    let _lastProfileStorageActiveId = '';
    let _applyingRemoteProfileSync = false;
    let _isApplyingUserProfileSettings = false;
    let _isSwitchingUserProfile = false;
    let _suppressValueSyncUntil = 0;

    function suppressValueSync(ms = 250) {
        const until = Date.now() + Math.max(0, Number(ms) || 0);
        if (until > _suppressValueSyncUntil) _suppressValueSyncUntil = until;
    }

    function isValueSyncSuppressed() {
        return Date.now() < _suppressValueSyncUntil;
    }

    // -------------------------
    // LUT Profile Management
    // -------------------------
    let lutProfiles = [];
    let lutGroups = [];

    // Active LUT selection is stored as a composite key so duplicate names are allowed across groups.
    // Key format: "<group>||<name>" (group may be empty for ungrouped, e.g. "||Warm").
    let activeLutProfileKey = String(gmGet(K.LUT_ACTIVE_PROFILE, 'none') || 'none');
    let activeLutMatrix4x5 = null; // Array[20] or null

    function _lutNormGroup(g) {
        const s = (g === undefined || g === null) ? '' : String(g);
        return s.trim();
    }
    function _lutNormName(n) { return String(n || '').trim(); }
    function lutMakeKey(name, group) {
        const nm = _lutNormName(name);
        const gr = _lutNormGroup(group);
        if (!nm) return 'none';
        return `${gr}||${nm}`;
    }
    function lutParseKey(key) {
        const k = String(key || '').trim();
        if (!k || k === 'none') return { group: '', name: 'none', key: 'none' };
        const i = k.indexOf('||');
        if (i >= 0) {
            const g = k.slice(0, i);
            const n = k.slice(i + 2);
            return { group: _lutNormGroup(g), name: _lutNormName(n), key: `${_lutNormGroup(g)}||${_lutNormName(n)}` };
        }
        // Back-compat: old storage used only the name.
        return { group: '', name: _lutNormName(k), key: `||${_lutNormName(k)}` };
    }
    function lutKeyFromProfile(p) {
        const n = _lutNormName(p && p.name);
        const g = _lutNormGroup(p && p.group);
        return lutMakeKey(n, g);
    }

    let lutSelectEl = null;
    let refreshLutDropdownFn = null;

    // Default user profile
    const DEFAULT_USER_PROFILE = {
        id: 'default',
        name: 'Default',
        createdAt: Date.now(),
        settings: {
            enabled: true,
            darkMoody: true,
            tealOrange: false,
            vibrantSat: false,
            sl: 1.0,
            sr: 0.5,
            bl: -1.2,
            wl: 0.2,
            dn: 0.0,
            edge: 0.1,
            hdr: 0.0,
            profile: 'user',
            renderMode: 'svg',
            lutProfile: 'none',
            autoOn: true,
            autoStrength: 0.65,
            autoLockWB: true,
            u_contrast: 0,
            u_black: 0,
            u_white: 0,
            u_highlights: 0,
            u_shadows: 0,
            u_sat: 0,
            u_vib: 0,
            u_sharp: 0,
            u_gamma: 0,
            u_grain: 0,
            u_hue: 0,
            u_r_gain: 128,
            u_g_gain: 128,
            u_b_gain: 128,
            cbFilter: 'none'
        }
    };

    // Firefox-specific default profile
    const DEFAULT_USER_PROFILE_FIREFOX = {
        id: 'default',
        name: 'Default',
        createdAt: Date.now(),
        settings: {
            enabled: true,
            darkMoody: true,
            tealOrange: false,
            vibrantSat: false,
            sl: 1.3,
            sr: -1.1,
            bl: 0.3,
            wl: 0.2,
            dn: 0.0,
            edge: 0.0,
            hdr: 0.0,
            profile: 'off',
            renderMode: 'svg',
            lutProfile: 'none',
            autoOn: true,
            autoStrength: 0.65,
            autoLockWB: true,
            u_contrast: 0,
            u_black: 0,
            u_white: 0,
            u_highlights: 0,
            u_shadows: 0,
            u_sat: 0,
            u_vib: 0,
            u_sharp: 0,
            u_gamma: 0,
            u_grain: 0,
            u_hue: 0,
            u_r_gain: 128,
            u_g_gain: 128,
            u_b_gain: 128,
            cbFilter: 'none'
        }
    };

    // Profile Management Functions
    function getDefaultUserProfilesFallback() {
        const isFirefoxBrowser = isFirefox();
        const defaultProfile = isFirefoxBrowser ? DEFAULT_USER_PROFILE_FIREFOX : DEFAULT_USER_PROFILE;
        return [JSON.parse(JSON.stringify(defaultProfile))];
    }

    function getDefaultUserProfileSettingsSnapshot() {
        const isFirefoxBrowser = isFirefox();
        const defaults = isFirefoxBrowser ? DEFAULT_USER_PROFILE_FIREFOX.settings : DEFAULT_USER_PROFILE.settings;
        return JSON.parse(JSON.stringify(defaults || {}));
    }

    const PROFILE_UI_ONLY_KEYS = ['iconsShown', 'gradingHudShown', 'ioHudShown', 'scopesHudShown'];

    function stripUiOnlySettings(settingsObj) {
        const src = (settingsObj && typeof settingsObj === 'object') ? JSON.parse(JSON.stringify(settingsObj)) : {};
        for (const key of PROFILE_UI_ONLY_KEYS) {
            if (Object.prototype.hasOwnProperty.call(src, key)) delete src[key];
        }
        return src;
    }

    function settingsEqualNormalized(a, b) {
        try {
            return JSON.stringify(stripUiOnlySettings(a)) === JSON.stringify(stripUiOnlySettings(b));
        } catch (_) {
            return false;
        }
    }

    function buildImportedUserProfileSettings(settingsObj) {
        const defaults = stripUiOnlySettings(getDefaultUserProfileSettingsSnapshot());
        const src = stripUiOnlySettings(settingsObj);
        return {
            ...defaults,
            ...src
        };
    }

    function normalizeUserProfilesForStorage(list) {
        const src = Array.isArray(list) ? list : [];
        const out = [];
        const seen = new Set();
        for (const raw of src) {
            if (!raw || typeof raw !== 'object') continue;
            const id = String(raw.id || '').trim();
            const name = String(raw.name || '').trim();
            if (!id || !name || seen.has(id)) continue;
            seen.add(id);
            out.push({
                id,
                name,
                createdAt: Number(raw.createdAt || Date.now()),
                updatedAt: Number(raw.updatedAt || raw.createdAt || Date.now()),
                settings: buildImportedUserProfileSettings(raw.settings && typeof raw.settings === 'object' ? raw.settings : {})
            });
        }
        return out;
    }

    function readUserProfilesFromLocalStorage() {
        try {
            const raw = localStorage.getItem(K.USER_PROFILES);
            if (!raw) return null;
            const parsed = JSON.parse(raw);
            const normalized = normalizeUserProfilesForStorage(parsed);
            return normalized.length ? normalized : null;
        } catch (_) {
            return null;
        }
    }

    function writeUserProfilesToLocalStorage(profiles, activeId, rev) {
        try { localStorage.setItem(K.USER_PROFILES, JSON.stringify(profiles)); } catch (_) { }
        try { localStorage.setItem(K.ACTIVE_USER_PROFILE, String(activeId || 'default')); } catch (_) { }
        try { localStorage.setItem(K.USER_PROFILES_REV, String(Number(rev || Date.now()) || Date.now())); } catch (_) { }
    }

    function readUserProfilesRevFromLocalStorage() {
        try {
            const raw = localStorage.getItem(K.USER_PROFILES_REV);
            const n = Number(raw);
            return Number.isFinite(n) && n > 0 ? n : 0;
        } catch (_) {
            return 0;
        }
    }

    function readUserProfilesRevFromGM() {
        try {
            const n = Number(gmGet(K.USER_PROFILES_REV, 0));
            return Number.isFinite(n) && n > 0 ? n : 0;
        } catch (_) {
            return 0;
        }
    }

    function resolveStoredActiveUserProfileId(profiles, preferLocal = false) {
        const list = Array.isArray(profiles) ? profiles : [];
        const hasId = (id) => !!id && list.some(p => p && p.id === id);

        let lsId = '';
        let gmId = '';
        try { lsId = String(localStorage.getItem(K.ACTIVE_USER_PROFILE) || '').trim(); } catch (_) { }
        try { gmId = String(gmGet(K.ACTIVE_USER_PROFILE, '') || '').trim(); } catch (_) { }

        if (preferLocal && hasId(lsId)) return lsId;
        if (hasId(gmId)) return gmId;
        if (hasId(lsId)) return lsId;
        if (hasId(_lastProfileStorageActiveId)) return String(_lastProfileStorageActiveId || '').trim();

        const first = list[0] && list[0].id ? String(list[0].id).trim() : '';
        return first || 'default';
    }

    function loadUserProfiles() {
        try {
            const storedGm = gmGet(K.USER_PROFILES, null);
            const normalizedGm = normalizeUserProfilesForStorage(storedGm);
            const storedLs = readUserProfilesFromLocalStorage();

            const gmRev = readUserProfilesRevFromGM();
            const lsRev = readUserProfilesRevFromLocalStorage();
            const useLs = (!!storedLs && storedLs.length && lsRev >= gmRev);
            const useGm = (!!normalizedGm.length && !useLs);

            let needsPersist = false;

            if (useLs) {
                userProfiles = storedLs;
            } else if (useGm) {
                userProfiles = normalizedGm;
            } else if (storedLs && storedLs.length) {
                userProfiles = storedLs;
            } else {
                userProfiles = getDefaultUserProfilesFallback();
                needsPersist = true;
            }

            const activeId = resolveStoredActiveUserProfileId(userProfiles, useLs || (!!storedLs && storedLs.length && lsRev >= gmRev));
            activeUserProfile = userProfiles.find(p => p.id === activeId) || userProfiles[0] || null;
            if (!activeUserProfile && userProfiles.length) activeUserProfile = userProfiles[0];
            if (!activeUserProfile) {
                userProfiles = getDefaultUserProfilesFallback();
                activeUserProfile = userProfiles[0] || null;
                needsPersist = true;
            }

            _lastProfileStorageRev = Math.max(gmRev, lsRev, 0);
            _lastProfileStorageActiveId = String(activeUserProfile && activeUserProfile.id ? activeUserProfile.id : 'default');

            if (needsPersist || !normalizedGm.length || !storedLs || !storedLs.length) {
                saveUserProfiles();
            } else {
                writeUserProfilesToLocalStorage(JSON.parse(JSON.stringify(userProfiles)), _lastProfileStorageActiveId, _lastProfileStorageRev || Date.now());
            }

            log('User profiles loaded:', userProfiles.length, 'Active:', activeUserProfile?.name, 'Source:', useLs ? 'localStorage' : (useGm ? 'GM' : 'fallback'));
        } catch (e) {
            logW('Error loading user profiles:', e);
            userProfiles = getDefaultUserProfilesFallback();
            activeUserProfile = userProfiles[0] || null;
            saveUserProfiles();
        }
    }

    function persistActiveUserProfileSelection(profileId, revMaybe) {
        const nextActiveId = String(profileId || 'default').trim() || 'default';
        const rev = Number(revMaybe);
        const nextRev = Number.isFinite(rev) && rev > 0 ? rev : Date.now();

        _lastProfileStorageRev = nextRev;
        _lastProfileStorageActiveId = nextActiveId;

        try { gmSet(K.ACTIVE_USER_PROFILE, nextActiveId); } catch (_) { }
        try { gmSet(K.USER_PROFILES_REV, nextRev); } catch (_) { }

        try { localStorage.setItem(K.ACTIVE_USER_PROFILE, nextActiveId); } catch (_) { }
        try { localStorage.setItem(K.USER_PROFILES_REV, String(nextRev)); } catch (_) { }

        return nextRev;
    }

    function saveUserProfiles(revMaybe) {
        try {
            suppressValueSync(300);
            userProfiles = normalizeUserProfilesForStorage(userProfiles);
            if (!userProfiles.length) {
                userProfiles = getDefaultUserProfilesFallback();
            }

            if (activeUserProfile) {
                const freshActive = userProfiles.find(p => p.id === activeUserProfile.id);
                activeUserProfile = freshActive || userProfiles[0] || null;
            } else {
                activeUserProfile = userProfiles[0] || null;
            }

            const snapshot = JSON.parse(JSON.stringify(userProfiles));
            const rev = persistActiveUserProfileSelection(activeUserProfile ? activeUserProfile.id : 'default', revMaybe);
            gmSet(K.USER_PROFILES, snapshot);
            gmSet(K.USER_PROFILES_REV, rev);
            writeUserProfilesToLocalStorage(snapshot, activeUserProfile ? activeUserProfile.id : 'default', rev);
        } catch (e) {
            logW('Error saving user profiles:', e);
            try {
                const snapshot = JSON.parse(JSON.stringify(normalizeUserProfilesForStorage(userProfiles)));
                const rev = persistActiveUserProfileSelection(activeUserProfile ? activeUserProfile.id : 'default', revMaybe);
                try { gmSet(K.USER_PROFILES, snapshot); } catch (_) { }
                try { gmSet(K.USER_PROFILES_REV, rev); } catch (_) { }
                writeUserProfilesToLocalStorage(snapshot, activeUserProfile ? activeUserProfile.id : 'default', rev);
            } catch (_) { }
        }
    }

    function refreshUserProfileManagerUi() {
        try { updateProfileList(); } catch (_) { }
        try {
            const activeInfo = document.getElementById('gvf-active-profile-info');
            if (activeInfo) {
                setActiveProfileInfo(activeInfo, activeUserProfile?.name);
            }
        } catch (_) { }
    }

    function pullUserProfilesFromSharedStorage(reason = '', force = false) {
        if (_applyingRemoteProfileSync || _isSwitchingUserProfile) return false;

        try {
            const storedGm = gmGet(K.USER_PROFILES, null);
            const normalizedGm = normalizeUserProfilesForStorage(storedGm);
            const storedLs = readUserProfilesFromLocalStorage();
            const gmRev = readUserProfilesRevFromGM();
            const lsRev = readUserProfilesRevFromLocalStorage();
            const newestRev = Math.max(gmRev, lsRev, 0);

            let nextProfiles = [];
            if (storedLs && storedLs.length && lsRev > gmRev) {
                nextProfiles = storedLs;
            } else if (normalizedGm.length) {
                nextProfiles = normalizedGm;
            } else if (storedLs && storedLs.length) {
                nextProfiles = storedLs;
            } else {
                nextProfiles = getDefaultUserProfilesFallback();
            }

            const nextActiveId = resolveStoredActiveUserProfileId(
                nextProfiles,
                !!(storedLs && storedLs.length && lsRev >= gmRev)
            );

            const currentSnapshot = JSON.stringify(normalizeUserProfilesForStorage(userProfiles));
            const nextSnapshot = JSON.stringify(normalizeUserProfilesForStorage(nextProfiles));
            const currentActiveId = String(activeUserProfile && activeUserProfile.id ? activeUserProfile.id : '');
            const snapshotChanged = nextSnapshot != currentSnapshot;
            const activeChanged = nextActiveId !== currentActiveId;

            if (!force) {
                if (newestRev < _lastProfileStorageRev) return false;
                if (activeChanged && !snapshotChanged && newestRev <= _lastProfileStorageRev) {
                    log('Ignored stale active-profile sync:', reason || 'unknown', 'Incoming:', nextActiveId, 'Current:', currentActiveId);
                    return false;
                }
            }

            const hasChanged = force
                || newestRev > _lastProfileStorageRev
                || activeChanged
                || snapshotChanged;

            if (!hasChanged) return false;

            userProfiles = normalizeUserProfilesForStorage(nextProfiles);
            if (!userProfiles.length) userProfiles = getDefaultUserProfilesFallback();
            activeUserProfile = userProfiles.find(p => p.id === nextActiveId) || userProfiles[0] || null;
            if (!activeUserProfile && userProfiles.length) activeUserProfile = userProfiles[0] || null;

            _lastProfileStorageRev = newestRev || Date.now();
            _lastProfileStorageActiveId = String(activeUserProfile && activeUserProfile.id ? activeUserProfile.id : 'default');

            if (activeUserProfile && activeUserProfile.settings && typeof activeUserProfile.settings === 'object') {
                _applyingRemoteProfileSync = true;
                try {
                    applyUserProfileSettings(activeUserProfile.settings);
                } finally {
                    _applyingRemoteProfileSync = false;
                }
            }

            refreshUserProfileManagerUi();
            log('User profiles synced from shared storage:', reason || 'unknown', 'Active:', activeUserProfile && activeUserProfile.name ? activeUserProfile.name : 'none');
            return true;
        } catch (e) {
            _applyingRemoteProfileSync = false;
            logW('User profile sync from shared storage failed:', reason, e);
            return false;
        }
    }

    let _remoteProfilePullTimer = null;
    function schedulePullUserProfilesFromSharedStorage(reason = '', force = false, delay = 60) {
        if (_remoteProfilePullTimer) clearTimeout(_remoteProfilePullTimer);
        _remoteProfilePullTimer = setTimeout(() => {
            _remoteProfilePullTimer = null;
            pullUserProfilesFromSharedStorage(reason, force);
        }, Math.max(0, Number(delay) || 0));
    }

    // LUT Profiles (Storage + Apply)
    // -------------------------
    function normalizeLutProfilesForStorage(input) {
        const list = Array.isArray(input) ? input : [];
        const out = [];
        const seen = new Set();
        for (const raw of list) {
            if (!raw || typeof raw !== 'object') continue;
            const name = String(raw.name || '').trim();
            if (!name) continue;
            const group = (raw.group === undefined || raw.group === null) ? undefined : String(raw.group).trim();
            const key = `${_lutNormGroup(group)}||${_lutNormName(name)}`;
            if (seen.has(key)) continue;
            const m = Array.isArray(raw.matrix4x5) ? raw.matrix4x5.map(v => Number(v)) : [];
            if (m.length !== 20 || m.some(v => !Number.isFinite(v))) continue;
            seen.add(key);
            out.push({
                name,
                group: group || undefined,
                createdAt: Number(raw.createdAt || Date.now()),
                updatedAt: Number(raw.updatedAt || raw.createdAt || Date.now()),
                matrix4x5: m
            });
        }
        return out;
    }

    function readLutProfilesFromLocalStorage() {
        try {
            const raw = localStorage.getItem(K.LUT_PROFILES);
            if (!raw) return null;
            const parsed = JSON.parse(raw);
            const normalized = normalizeLutProfilesForStorage(parsed);
            return normalized.length ? normalized : null;
        } catch (_) {
            return null;
        }
    }

    function writeLutProfilesToLocalStorage(profiles, activeKey, rev) {
        try { localStorage.setItem(K.LUT_PROFILES, JSON.stringify(profiles)); } catch (_) { }
        try { localStorage.setItem(K.LUT_ACTIVE_PROFILE, String(activeKey || 'none')); } catch (_) { }
        try { localStorage.setItem(K.LUT_PROFILES_REV, String(Number(rev || Date.now()) || Date.now())); } catch (_) { }
    }

    function readLutProfilesRevFromLocalStorage() {
        try {
            const raw = localStorage.getItem(K.LUT_PROFILES_REV);
            const n = Number(raw);
            return Number.isFinite(n) && n > 0 ? n : 0;
        } catch (_) {
            return 0;
        }
    }

    function readLutProfilesRevFromGM() {
        try {
            const n = Number(gmGet(K.LUT_PROFILES_REV, 0));
            return Number.isFinite(n) && n > 0 ? n : 0;
        } catch (_) {
            return 0;
        }
    }

    function loadLutProfiles() {
        try {
            const storedGm = normalizeLutProfilesForStorage(gmGet(K.LUT_PROFILES, null));
            const storedLs = readLutProfilesFromLocalStorage();
            const gmRev = readLutProfilesRevFromGM();
            const lsRev = readLutProfilesRevFromLocalStorage();
            const useLs = (!!storedLs && storedLs.length && lsRev > gmRev);
            const useGm = (!!storedGm.length && !useLs);

            if (useLs) lutProfiles = storedLs;
            else if (useGm) lutProfiles = storedGm;
            else lutProfiles = storedLs || storedGm || [];
        } catch (e) {
            lutProfiles = [];
            logW('Error loading LUT profiles:', e);
        }

        // Load and normalize LUT group list (supports empty groups)
        loadLutGroups();
        try {
            const set = new Set(Array.isArray(lutGroups) ? lutGroups.map(g => String(g || '').trim()).filter(Boolean) : []);
            for (const p of (Array.isArray(lutProfiles) ? lutProfiles : [])) {
                const g = (p && p.group) ? String(p.group).trim() : '';
                if (g) set.add(g);
            }
            lutGroups = Array.from(set).sort((a, b) => a.localeCompare(b));
            saveLutGroups();
        } catch (_) { }

        let storedActiveLs = '';
        try { storedActiveLs = String(localStorage.getItem(K.LUT_ACTIVE_PROFILE) || '').trim(); } catch (_) { }
        let storedActiveGm = String(gmGet(K.LUT_ACTIVE_PROFILE, 'none') || 'none');
        activeLutProfileKey = (storedActiveLs && readLutProfilesRevFromLocalStorage() > readLutProfilesRevFromGM()) ? storedActiveLs : storedActiveGm;
        if (!activeLutProfileKey) activeLutProfileKey = 'none';

        const want = lutParseKey(activeLutProfileKey);
        let p = null;

        if (want.key !== 'none') {
            p = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => lutKeyFromProfile(x) === want.key) || null;
            if (!p && want.name && want.name !== 'none') {
                p = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => _lutNormName(x && x.name) === want.name) || null;
                if (p) activeLutProfileKey = lutKeyFromProfile(p);
            }
        }

        activeLutMatrix4x5 = (p && Array.isArray(p.matrix4x5) && p.matrix4x5.length === 20) ? p.matrix4x5 : null;
        saveLutProfiles();
        log('LUT profiles loaded:', lutProfiles.length, 'Active:', activeLutProfileKey);
    }

    function saveLutProfiles() {
        try {
            lutProfiles = normalizeLutProfilesForStorage(lutProfiles);
            const snapshot = JSON.parse(JSON.stringify(lutProfiles));
            const rev = Date.now();
            gmSet(K.LUT_PROFILES, snapshot);
            gmSet(K.LUT_ACTIVE_PROFILE, activeLutProfileKey || 'none');
            gmSet(K.LUT_PROFILES_REV, rev);
            writeLutProfilesToLocalStorage(snapshot, activeLutProfileKey || 'none', rev);
        } catch (e) {
            logW('Error saving LUT profiles:', e);
            try {
                const snapshot = JSON.parse(JSON.stringify(normalizeLutProfilesForStorage(lutProfiles)));
                const rev = Date.now();
                try { gmSet(K.LUT_PROFILES, snapshot); } catch (_) { }
                try { gmSet(K.LUT_ACTIVE_PROFILE, activeLutProfileKey || 'none'); } catch (_) { }
                try { gmSet(K.LUT_PROFILES_REV, rev); } catch (_) { }
                writeLutProfilesToLocalStorage(snapshot, activeLutProfileKey || 'none', rev);
            } catch (_) { }
        }
    }


    function loadLutGroups() {
        try {
            const stored = gmGet(K.LUT_GROUPS, null);
            if (stored && Array.isArray(stored)) {
                lutGroups = stored.map(v => String(v || '').trim()).filter(Boolean);
            } else {
                lutGroups = [];
            }
        } catch (e) {
            lutGroups = [];
            logW('Error loading LUT groups:', e);
        }
        // normalize / unique
        try {
            const set = new Set();
            for (const g of lutGroups) { if (g) set.add(String(g).trim()); }
            lutGroups = Array.from(set).filter(Boolean).sort((a, b) => a.localeCompare(b));
        } catch (_) { }
    }

    function saveLutGroups() {
        try {
            const set = new Set();
            for (const g of (Array.isArray(lutGroups) ? lutGroups : [])) {
                const gg = String(g || '').trim();
                if (gg) set.add(gg);
            }
            lutGroups = Array.from(set).sort((a, b) => a.localeCompare(b));
            gmSet(K.LUT_GROUPS, lutGroups);
        } catch (e) {
            logW('Error saving LUT groups:', e);
        }
    }

    function setActiveLutProfile(keyOrName, groupMaybe, opts = {}) {
        const inVal = String(keyOrName || 'none').trim() || 'none';
        const key = (inVal.includes('||') || inVal === 'none') ? inVal : lutMakeKey(inVal, groupMaybe);
        activeLutProfileKey = key;

        const want = lutParseKey(activeLutProfileKey);
        let p = null;
        if (want.key !== 'none') {
            p = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => lutKeyFromProfile(x) === want.key) || null;
            if (!p && want.name && want.name !== 'none') {
                // fallback: first match by name (legacy)
                p = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => _lutNormName(x && x.name) === want.name) || null;
                if (p) activeLutProfileKey = lutKeyFromProfile(p);
            }
        }

        activeLutMatrix4x5 = (p && Array.isArray(p.matrix4x5) && p.matrix4x5.length === 20) ? p.matrix4x5 : null;
        saveLutProfiles();

        log('Active LUT profile set:', activeLutProfileKey);

        // Sync LUT dropdown immediately
        try {
            if (lutSelectEl) lutSelectEl.value = String(activeLutProfileKey || 'none');
            if (typeof refreshLutDropdownFn === 'function') refreshLutDropdownFn();
        } catch (_) { }

        const skipProfileSave = !!(opts && opts.skipProfileSave);
        const skipVisualApply = !!(opts && opts.skipVisualApply);

        if (!skipProfileSave && !_isApplyingUserProfileSettings && !_applyingRemoteProfileSync) {
            updateCurrentProfileSettings();
        }

        if (!skipVisualApply) {
            if (renderMode === 'gpu') {
                applyGpuFilter();
            } else {
                ensureSvgFilter(true);
                applyFilter({ skipSvgIfPossible: false });
            }
            scheduleOverlayUpdate();
        }
    }

    function getActiveLutProfile() {
        const want = lutParseKey(activeLutProfileKey);
        if (want.key === 'none') return null;
        return (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => lutKeyFromProfile(x) === want.key) || null;
    }

    function upsertLutProfile(profile) {
        const name = String(profile && profile.name ? profile.name : '').trim();
        if (!name) throw new Error('Profile name is empty.');

        const groupRaw = (profile && Object.prototype.hasOwnProperty.call(profile, 'group')) ? profile.group : undefined;
        let group = (groupRaw === undefined || groupRaw === null) ? undefined : String(groupRaw).trim();
        if (group === '') group = undefined;

        const idx = lutProfiles.findIndex(p => {
            const pn = _lutNormName(p && p.name);
            const pg = _lutNormGroup(p && p.group);
            const ng = _lutNormGroup(group);
            return (pn === name) && (pg === ng);
        });
        const now = Date.now();

        const prev = (idx >= 0) ? lutProfiles[idx] : null;
        const prevGroup = prev && prev.group ? String(prev.group).trim() : undefined;
        if (group === undefined) group = prevGroup;

        const next = {
            name,
            group: group || undefined,
            createdAt: (idx >= 0 && lutProfiles[idx].createdAt) ? lutProfiles[idx].createdAt : now,
            updatedAt: now,
            matrix4x5: profile.matrix4x5
        };

        if (idx >= 0) lutProfiles[idx] = next;
        else lutProfiles.push(next);

        if (next.group) {
            const g = String(next.group).trim();
            if (g) {
                if (!Array.isArray(lutGroups)) lutGroups = [];
                if (!lutGroups.some(x => String(x).trim() === g)) {
                    lutGroups.push(g);
                    saveLutGroups();
                }
            }
        }

        saveLutProfiles();
        return next;
    }

    function deleteLutProfile(keyOrName, groupMaybe) {
        const inVal = String(keyOrName || '').trim();
        if (!inVal) return;

        const key = (inVal.includes('||')) ? inVal : lutMakeKey(inVal, groupMaybe);
        const want = lutParseKey(key);
        if (want.key === 'none') return;

        lutProfiles = (Array.isArray(lutProfiles) ? lutProfiles : []).filter(p => lutKeyFromProfile(p) !== want.key);

        if (String(activeLutProfileKey) === want.key) {
            activeLutProfileKey = 'none';
            activeLutMatrix4x5 = null;
        }

        saveLutProfiles();
        log('Deleted LUT profile:', want.key);
    }

    // -------------------------
    // JSZip Loader (CDN) - DOM method only
    // -------------------------
    let _jszipPromise = null;
    function ensureJsZipLoaded() {
        if (window.JSZip) return Promise.resolve(window.JSZip);
        if (_jszipPromise) return _jszipPromise;

        _jszipPromise = new Promise((resolve, reject) => {
            const existing = document.querySelector('script[data-gvf-jszip="1"]');
            if (existing && window.JSZip) return resolve(window.JSZip);

            const s = document.createElement('script');
            s.dataset.gvfJszip = '1';
            s.setAttribute('data-gvf-jszip', '1');
            s.src = 'https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js';
            s.async = true;
            s.onload = () => (window.JSZip ? resolve(window.JSZip) : reject(new Error('JSZip loaded but window.JSZip is missing.')));
            s.onerror = () => reject(new Error('Failed to load JSZip from CDN.'));
            (document.head || document.documentElement).appendChild(s);
        });
        return _jszipPromise;
    }

    // -------------------------
    // PNG -> 4x5 Matrix (Least Squares)
    // Supports tiled 2D LUT layouts (e.g. 512x512 => LUT_SIZE=64, tiles 8x8).
    // -------------------------
    function detectTiledLutLayout(w, h) {
        const candidates = [16, 32, 64, 128];
        for (const lutSize of candidates) {
            if ((w % lutSize) !== 0 || (h % lutSize) !== 0) continue;
            const tilesX = w / lutSize;
            const tilesY = h / lutSize;
            if (tilesX * tilesY !== lutSize) continue;
            return { lutSize, tilesX, tilesY };
        }
        return null;
    }

    function imgDataGetRGBA(imgData, x, y) {
        const i = (y * imgData.width + x) * 4;
        return [
            imgData.data[i] / 255,
            imgData.data[i + 1] / 255,
            imgData.data[i + 2] / 255,
            imgData.data[i + 3] / 255
        ];
    }

    function lutSampleTiled(imgData, r, g, b, layout, flipY) {
        const { lutSize, tilesX } = layout;
        r = clamp(r, 0, 1); g = clamp(g, 0, 1); b = clamp(b, 0, 1);

        const lerp = (a, c, t) => a + (c - a) * t;

        const bf = b * (lutSize - 1);
        const b0 = Math.floor(bf);
        const b1 = Math.min(b0 + 1, lutSize - 1);
        const bt = bf - b0;

        const sampleSlice = (bslice) => {
            const tileX = bslice % tilesX;
            const tileY = Math.floor(bslice / tilesX);

            const xf = r * (lutSize - 1);
            const yf = g * (lutSize - 1);
            const x0 = Math.floor(xf), x1 = Math.min(x0 + 1, lutSize - 1);
            const y0 = Math.floor(yf), y1 = Math.min(y0 + 1, lutSize - 1);
            const tx = xf - x0, ty = yf - y0;

            const px = (ix, iy) => {
                let x = tileX * lutSize + ix;
                let y = tileY * lutSize + iy;
                if (flipY) y = (imgData.height - 1) - y;
                return imgDataGetRGBA(imgData, x, y);
            };

            const c00 = px(x0, y0), c10 = px(x1, y0), c01 = px(x0, y1), c11 = px(x1, y1);

            const out = [0,0,0];
            for (let i = 0; i < 3; i++) {
                const c0 = lerp(c00[i], c10[i], tx);
                const c1 = lerp(c01[i], c11[i], tx);
                out[i] = lerp(c0, c1, ty);
            }
            return out;
        };

        const c0 = sampleSlice(b0);
        const c1 = sampleSlice(b1);
        return [lerp(c0[0], c1[0], bt), lerp(c0[1], c1[1], bt), lerp(c0[2], c1[2], bt)];
    }

    function invert4x4(A) {
        const M = A.map(r => r.slice());
        const I = [
            [1,0,0,0],
            [0,1,0,0],
            [0,0,1,0],
            [0,0,0,1],
        ];

        for (let col = 0; col < 4; col++) {
            let piv = col;
            let pivVal = Math.abs(M[piv][col]);
            for (let r = col + 1; r < 4; r++) {
                const v = Math.abs(M[r][col]);
                if (v > pivVal) { pivVal = v; piv = r; }
            }
            if (pivVal < 1e-12) throw new Error('Matrix inversion failed (singular).');

            if (piv !== col) {
                [M[col], M[piv]] = [M[piv], M[col]];
                [I[col], I[piv]] = [I[piv], I[col]];
            }

            const pivot = M[col][col];
            for (let j = 0; j < 4; j++) { M[col][j] /= pivot; I[col][j] /= pivot; }

            for (let r = 0; r < 4; r++) {
                if (r === col) continue;
                const f = M[r][col];
                for (let j = 0; j < 4; j++) {
                    M[r][j] -= f * M[col][j];
                    I[r][j] -= f * I[col][j];
                }
            }
        }
        return I;
    }

    function fitAffineRGB(X, Y) {
        const XtX = [
            [0,0,0,0],
            [0,0,0,0],
            [0,0,0,0],
            [0,0,0,0],
        ];
        const XtY = [
            [0,0,0],
            [0,0,0],
            [0,0,0],
            [0,0,0],
        ];

        for (let i = 0; i < X.length; i++) {
            const x = X[i];
            const y = Y[i];
            for (let a = 0; a < 4; a++) {
                for (let b = 0; b < 4; b++) XtX[a][b] += x[a] * x[b];
                XtY[a][0] += x[a] * y[0];
                XtY[a][1] += x[a] * y[1];
                XtY[a][2] += x[a] * y[2];
            }
        }

        const inv = invert4x4(XtX);

        const M = [
            [0,0,0],
            [0,0,0],
            [0,0,0],
            [0,0,0],
        ];
        for (let i = 0; i < 4; i++) {
            for (let k = 0; k < 3; k++) {
                let sum = 0;
                for (let j = 0; j < 4; j++) sum += inv[i][j] * XtY[j][k];
                M[i][k] = sum;
            }
        }
        return M;
    }

    function buildMatrix4x5FromAffine(M4x3) {
        return [
            M4x3[0][0], M4x3[1][0], M4x3[2][0], 0.0, M4x3[3][0],
            M4x3[0][1], M4x3[1][1], M4x3[2][1], 0.0, M4x3[3][1],
            M4x3[0][2], M4x3[1][2], M4x3[2][2], 0.0, M4x3[3][2],
            0.0,       0.0,       0.0,       1.0, 0.0
        ];
    }

    function _lutSrgbToLinear(v) { return (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)); }
    function _lutLinearToSrgb(v) { return (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1/2.4) - 0.055); }

    function buildSamplesGrid(samplesPerAxis, linearizeIn) {
        const X = [];
        for (let bi = 0; bi < samplesPerAxis; bi++) {
            for (let gi = 0; gi < samplesPerAxis; gi++) {
                for (let ri = 0; ri < samplesPerAxis; ri++) {
                    let r = ri / (samplesPerAxis - 1);
                    let g = gi / (samplesPerAxis - 1);
                    let b = bi / (samplesPerAxis - 1);
                    if (linearizeIn) {
                        r = _lutSrgbToLinear(r);
                        g = _lutSrgbToLinear(g);
                        b = _lutSrgbToLinear(b);
                    }
                    X.push([r, g, b, 1.0]);
                }
            }
        }
        return X;
    }

    async function pngFileToMatrix4x5(file, opts = {}) {
        const flipY = !!opts.flipY;
        const linearizeIn = !!opts.linearizeIn;
        const delinearizeOut = !!opts.delinearizeOut;
        const samplesPerAxis = clamp(Number(opts.samplesPerAxis || 11), 5, 25);

        const url = URL.createObjectURL(file);
        try {
            const img = new Image();
            img.decoding = 'async';
            img.src = url;
            await new Promise((resolve, reject) => {
                img.onload = () => resolve();
                img.onerror = () => reject(new Error('Failed to load LUT PNG.'));
            });

            const w = img.naturalWidth || img.width;
            const h = img.naturalHeight || img.height;

            const layout = detectTiledLutLayout(w, h);
            if (!layout) throw new Error(`Unsupported LUT layout for ${w}x${h}. Expected tiled layout (e.g. 512x512 => LUT_SIZE=64 tiles 8x8).`);

            const canvas = document.createElement('canvas');
            canvas.width = w;
            canvas.height = h;
            const ctx = canvas.getContext('2d', { willReadFrequently: true });
            if (!ctx) throw new Error('Canvas 2D context unavailable.');

            ctx.drawImage(img, 0, 0, w, h);
            const imgData = ctx.getImageData(0, 0, w, h);

            const X = buildSamplesGrid(samplesPerAxis, linearizeIn);
            const Y = [];

            for (let i = 0; i < X.length; i++) {
                const x = X[i];
                const rgb = lutSampleTiled(imgData, x[0], x[1], x[2], layout, flipY);
                let out = rgb;
                if (delinearizeOut) out = [_lutLinearToSrgb(out[0]), _lutLinearToSrgb(out[1]), _lutLinearToSrgb(out[2])];
                Y.push(out);
            }

            const M4x3 = fitAffineRGB(X, Y);
            const m4x5 = buildMatrix4x5FromAffine(M4x3);

            return { matrix4x5: m4x5, layout, width: w, height: h };
        } finally {
            URL.revokeObjectURL(url);
        }
    }



    // -------------------------
    // CUBE 3D LUT (.cube) -> 4x5 matrix (row-major)
    // -------------------------

    function parseCubeText(text) {
        const lines = String(text || '').split(/\r?\n/);
        let size = 0;
        let domainMin = [0, 0, 0];
        let domainMax = [1, 1, 1];
        const data = [];

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i].trim();
            if (!line || line.startsWith('#')) continue;

            const parts = line.split(/\s+/);
            if (parts[0] === 'TITLE') continue;

            if (parts[0] === 'LUT_3D_SIZE') {
                size = parseInt(parts[1], 10) || 0;
                continue;
            }
            if (parts[0] === 'DOMAIN_MIN' && parts.length >= 4) {
                domainMin = [Number(parts[1]), Number(parts[2]), Number(parts[3])];
                continue;
            }
            if (parts[0] === 'DOMAIN_MAX' && parts.length >= 4) {
                domainMax = [Number(parts[1]), Number(parts[2]), Number(parts[3])];
                continue;
            }

            // data line: r g b
            if (parts.length >= 3) {
                const r = Number(parts[0]), g = Number(parts[1]), b = Number(parts[2]);
                if (Number.isFinite(r) && Number.isFinite(g) && Number.isFinite(b)) data.push([r, g, b]);
            }
        }

        if (!size || data.length !== size * size * size) {
            throw new Error('Invalid .cube file: missing LUT_3D_SIZE or wrong data length.');
        }

        return { size, domainMin, domainMax, data };
    }

    function cubeSampleTrilinear(lut, r, g, b) {
        const size = lut.size;
        const data = lut.data;
        const dmin = lut.domainMin;
        const dmax = lut.domainMax;

        const scale = (v, mn, mx) => (v - mn) / (mx - mn);

        let rr = scale(r, dmin[0], dmax[0]);
        let gg = scale(g, dmin[1], dmax[1]);
        let bb = scale(b, dmin[2], dmax[2]);

        rr = clamp(rr, 0, 1);
        gg = clamp(gg, 0, 1);
        bb = clamp(bb, 0, 1);

        const rf = rr * (size - 1);
        const gf = gg * (size - 1);
        const bf = bb * (size - 1);

        const r0 = Math.floor(rf), r1 = Math.min(r0 + 1, size - 1);
        const g0 = Math.floor(gf), g1 = Math.min(g0 + 1, size - 1);
        const b0 = Math.floor(bf), b1 = Math.min(b0 + 1, size - 1);

        const tr = rf - r0;
        const tg = gf - g0;
        const tb = bf - b0;

        // .cube standard ordering: R fastest, then G, then B (same as: idx = r + size*g + size*size*b)
        const idx = (ri, gi, bi) => ri + size * gi + size * size * bi;

        const c000 = data[idx(r0, g0, b0)];
        const c100 = data[idx(r1, g0, b0)];
        const c010 = data[idx(r0, g1, b0)];
        const c110 = data[idx(r1, g1, b0)];
        const c001 = data[idx(r0, g0, b1)];
        const c101 = data[idx(r1, g0, b1)];
        const c011 = data[idx(r0, g1, b1)];
        const c111 = data[idx(r1, g1, b1)];

        const lerp = (a, c, t) => a + (c - a) * t;

        const out = [0, 0, 0];
        for (let i = 0; i < 3; i++) {
            const x00 = lerp(c000[i], c100[i], tr);
            const x10 = lerp(c010[i], c110[i], tr);
            const x01 = lerp(c001[i], c101[i], tr);
            const x11 = lerp(c011[i], c111[i], tr);
            const y0 = lerp(x00, x10, tg);
            const y1 = lerp(x01, x11, tg);
            out[i] = lerp(y0, y1, tb);
        }
        return out;
    }

    function cubeTextToMatrix4x5(text, samplesPerAxis = 11, opts = {}) {
        const linearizeIn = !!opts.linearizeIn;
        const delinearizeOut = !!opts.delinearizeOut;

        const lut = parseCubeText(text);
        const X = buildSamplesGrid(clamp(Number(samplesPerAxis || 11), 5, 25), linearizeIn);
        const Y = [];

        for (let i = 0; i < X.length; i++) {
            const x = X[i];
            let out = cubeSampleTrilinear(lut, x[0], x[1], x[2]);
            if (delinearizeOut) out = [_lutLinearToSrgb(out[0]), _lutLinearToSrgb(out[1]), _lutLinearToSrgb(out[2])];
            Y.push(out);
        }

        const M4x3 = fitAffineRGB(X, Y);
        return buildMatrix4x5FromAffine(M4x3);
    }

    async function cubeFileToMatrix4x5(file, opts = {}) {
        const samplesPerAxis = clamp(Number(opts.samplesPerAxis || 11), 5, 25);
        const linearizeIn = !!opts.linearizeIn;
        const delinearizeOut = !!opts.delinearizeOut;

        const text = await file.text();
        const m4x5 = cubeTextToMatrix4x5(text, samplesPerAxis, { linearizeIn, delinearizeOut });
        return { matrix4x5: m4x5, size: (parseCubeText(text).size || 0) };
    }

    function matrixCopyNoBrackets(m20) {
        const arr = Array.isArray(m20) ? m20 : [];
        return arr.map(v => {
            const n = Number(v);
            if (!Number.isFinite(n)) return '0';
            // stable precision for copy/paste
            let s = n.toFixed(10);
            s = s.replace(/0+$/,'').replace(/\.$/,'');
            return s;
        }).join(', ');
    }

    function createNewUserProfile(name) {
        const now = Date.now();
        const newProfile = {
            id: 'profile_' + now + '_' + Math.random().toString(36).slice(2, 11),
            name: String(name || 'New profile').trim() || 'New profile',
            createdAt: now,
            updatedAt: now,
            settings: { ...getCurrentSettings() }
        };
        userProfiles = normalizeUserProfilesForStorage([...(Array.isArray(userProfiles) ? userProfiles : []), newProfile]);
        activeUserProfile = userProfiles.find(p => p.id === newProfile.id) || newProfile;
        saveUserProfiles();
        return activeUserProfile || newProfile;
    }

    function deleteUserProfile(profileId) {
        if (profileId === 'default') {
            log('Cannot delete standard profile');
            return false;
        }

        const index = userProfiles.findIndex(p => p.id === profileId);
        if (index !== -1) {
            userProfiles.splice(index, 1);

            // If active profile has been deleted, switch to default
            if (activeUserProfile && activeUserProfile.id === profileId) {
                switchToUserProfile('default');
            }

            saveUserProfiles();
            return true;
        }
        return false;
    }

    function switchToUserProfile(profileId) {
        const profile = userProfiles.find(p => p.id === profileId);
        if (!profile) {
            logW('Profile not found:', profileId);
            return false;
        }

        if (activeUserProfile && activeUserProfile.id === profile.id) {
            refreshUserProfileManagerUi();
            return true;
        }

        if (_autoSaveProfileTimer) {
            clearTimeout(_autoSaveProfileTimer);
            _autoSaveProfileTimer = null;
        }

        _isSwitchingUserProfile = true;
        try {
            activeUserProfile = profile;
            persistActiveUserProfileSelection(profile.id || 'default');
            try { localStorage.setItem(K.ACTIVE_USER_PROFILE, String(profile.id || 'default')); } catch (_) { }
            try { gmSet(K.ACTIVE_USER_PROFILE, String(profile.id || 'default')); } catch (_) { }
            applyUserProfileSettings(profile.settings || {});
        } finally {
            _isSwitchingUserProfile = false;
        }

        log('Switched to profile:', profile.name);

        showProfileNotification(profile.name);
        refreshUserProfileManagerUi();

        return true;
    }

    // Cycle to next profile (for Shift+Q)
    function cycleToNextProfile() {
        if (!userProfiles || userProfiles.length === 0) return;

        const currentIndex = userProfiles.findIndex(p => p.id === activeUserProfile?.id);
        if (currentIndex === -1) return;

        const nextIndex = (currentIndex + 1) % userProfiles.length;
        const nextProfile = userProfiles[nextIndex];

        switchToUserProfile(nextProfile.id);
    }

    // -------------------------
    // Profile Import / Export (ZIP per-profile JSON)
    // -------------------------
    function sanitizeProfileFilename(name) {
        const base = String(name || 'profile').trim() || 'profile';
        // Keep it Windows-safe and URL-safe
        const safe = base
            .replace(/[<>:"/\\|?*\x00-\x1F]/g, '_')
            .replace(/\s+/g, ' ')
            .trim()
            .slice(0, 80);
        return safe || 'profile';
    }

    function _u16le(v) { return [v & 255, (v >>> 8) & 255]; }
    function _u32le(v) { return [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255]; }

    // CRC32 (for ZIP)
    const _CRC32_TABLE = (() => {
        const t = new Uint32Array(256);
        for (let i = 0; i < 256; i++) {
            let c = i;
            for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
            t[i] = c >>> 0;
        }
        return t;
    })();

    function crc32(u8) {
        let c = 0xFFFFFFFF;
        for (let i = 0; i < u8.length; i++) c = _CRC32_TABLE[(c ^ u8[i]) & 255] ^ (c >>> 8);
        return (c ^ 0xFFFFFFFF) >>> 0;
    }

    function makeZipBlob(fileEntries) {
        // Minimal ZIP writer (store-only, no compression).
        // fileEntries: [{name: "x.json", data: Uint8Array}]
        const parts = [];
        const cdParts = [];
        let offset = 0;

        const enc = new TextEncoder();

        for (const f of fileEntries) {
            const nameBytes = enc.encode(String(f.name || 'file.bin'));
            const dataBytes = (f.data instanceof Uint8Array) ? f.data : new Uint8Array(f.data || []);
            const c = crc32(dataBytes);
            const size = dataBytes.length >>> 0;

            // Local file header
            const local = [];
            local.push(..._u32le(0x04034b50)); // sig
            local.push(..._u16le(20));         // ver
            local.push(..._u16le(0));          // flags
            local.push(..._u16le(0));          // method=store
            local.push(..._u16le(0));          // mod time
            local.push(..._u16le(0));          // mod date
            local.push(..._u32le(c));          // crc
            local.push(..._u32le(size));       // comp size
            local.push(..._u32le(size));       // uncomp size
            local.push(..._u16le(nameBytes.length)); // name len
            local.push(..._u16le(0));          // extra len

            const localHdr = new Uint8Array(local);
            parts.push(localHdr, nameBytes, dataBytes);

            // Central directory header
            const cd = [];
            cd.push(..._u32le(0x02014b50)); // sig
            cd.push(..._u16le(20));         // ver made
            cd.push(..._u16le(20));         // ver needed
            cd.push(..._u16le(0));          // flags
            cd.push(..._u16le(0));          // method
            cd.push(..._u16le(0));          // mod time
            cd.push(..._u16le(0));          // mod date
            cd.push(..._u32le(c));          // crc
            cd.push(..._u32le(size));       // comp size
            cd.push(..._u32le(size));       // uncomp size
            cd.push(..._u16le(nameBytes.length)); // name len
            cd.push(..._u16le(0));          // extra len
            cd.push(..._u16le(0));          // comment len
            cd.push(..._u16le(0));          // disk start
            cd.push(..._u16le(0));          // int attrs
            cd.push(..._u32le(0));          // ext attrs
            cd.push(..._u32le(offset));     // local hdr offset

            const cdHdr = new Uint8Array(cd);
            cdParts.push(cdHdr, nameBytes);

            offset += localHdr.length + nameBytes.length + dataBytes.length;
        }

        const cdStart = offset;
        for (const part of cdParts) {
            parts.push(part);
            offset += part.length;
        }
        const cdSize = offset - cdStart;

        // EOCD
        const eocd = [];
        eocd.push(..._u32le(0x06054b50)); // sig
        eocd.push(..._u16le(0)); // disk
        eocd.push(..._u16le(0)); // cd start disk
        eocd.push(..._u16le(fileEntries.length)); // entries this disk
        eocd.push(..._u16le(fileEntries.length)); // entries total
        eocd.push(..._u32le(cdSize)); // cd size
        eocd.push(..._u32le(cdStart)); // cd offset
        eocd.push(..._u16le(0)); // comment len

        parts.push(new Uint8Array(eocd));

        return new Blob(parts, { type: 'application/zip' });
    }

    function _zipName(prefix) {
        const d = new Date();
        const pad = (n) => String(n).padStart(2, '0');
        return `${prefix}_${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}_${pad(d.getHours())}-${pad(d.getMinutes())}-${pad(d.getSeconds())}.zip`;
    }

    function exportAllUserProfilesAsZip() {
        // Always persist latest current settings into active profile before export
        try { updateCurrentProfileSettings(); } catch (_) { }

        const enc = new TextEncoder();
        const entries = [];

        (userProfiles || []).forEach((p) => {
            try {
                const fileBase = sanitizeProfileFilename(p && p.name);
                const fileName = `${fileBase}.json`;
                const jsonStr = JSON.stringify(p, null, 2);
                entries.push({ name: fileName, data: enc.encode(jsonStr) });
            } catch (_) { }
        });


        if (!entries.length) return null;
        return makeZipBlob(entries);
    }


    function exportAllLutProfilesAsZip() {
        const enc = new TextEncoder();
        const entries = [];

        (lutProfiles || []).forEach((p) => {
            try {
                const name = String(p && p.name || '').trim();
                if (!name) return;

                const fileBase = sanitizeProfileFilename(name);
                const grp = (p && p.group) ? String(p.group).trim() : '';
                const grpBase = grp ? sanitizeProfileFilename(grp) : '';
                const fileName = grpBase ? `${grpBase}__${fileBase}.json` : `${fileBase}.json`;

                const payload = {
                    schema: 'gvf-lut-profile',
                    ver: 1,
                    name,
                    group: (p && p.group) ? String(p.group) : undefined,
                    createdAt: (p && p.createdAt) || Date.now(),
                    updatedAt: (p && p.updatedAt) || Date.now(),
                    matrix4x5: (p && p.matrix4x5) || null
                };

                const jsonStr = JSON.stringify(payload, null, 2);
                entries.push({ name: fileName, data: enc.encode(jsonStr) });
            } catch (_) { }
        });

        // Export group list as separate file to support empty groups
        try {
            const set = new Set();
            for (const g0 of (Array.isArray(lutGroups) ? lutGroups : [])) {
                const g = String(g0 || '').trim();
                if (g) set.add(g);
            }
            for (const p of (Array.isArray(lutProfiles) ? lutProfiles : [])) {
                const g = (p && p.group) ? String(p.group).trim() : '';
                if (g) set.add(g);
            }
            const groupsOut = Array.from(set).sort((a, b) => a.localeCompare(b));
            if (groupsOut.length) {
                const payload = { schema: 'gvf-lut-groups', ver: 1, groups: groupsOut, exportedAt: Date.now() };
                entries.push({ name: '_lut_groups.json', data: enc.encode(JSON.stringify(payload, null, 2)) });
            }
        } catch (_) { }

        if (!entries.length) return null;
        return makeZipBlob(entries);
    }

function downloadBlob(blob, filename) {
        try {
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            a.remove();
            setTimeout(() => URL.revokeObjectURL(url), 1500);
        } catch (_) { }
    }

    async function importProfilesFromZipOrJsonFile(file, statusEl) {
        const name = String(file && file.name || '').toLowerCase();
        const isZip = name.endsWith('.zip') || (file && file.type === 'application/zip');

        const setStatus = (t) => { if (statusEl) statusEl.textContent = t; };

        try {
            const buf = await file.arrayBuffer();
            if (!buf || !buf.byteLength) { setStatus('Import failed (empty file).'); return false; }

            if (!isZip) {
                // Single JSON profile
                const raw = new TextDecoder().decode(new Uint8Array(buf));
                const obj = JSON.parse(String(raw || '').trim());

                const ok = importSingleUserProfileObject(obj, setStatus);
                if (ok) {
                    saveUserProfiles();
                    updateProfileList();
                    const activeInfo = document.getElementById('gvf-active-profile-info');
                    if (activeInfo) setActiveProfileInfo(activeInfo, activeUserProfile?.name);
                }
                return ok;
            }

            // ZIP (store-only or deflate if DecompressionStream supports it)
            const files = await unzipToFiles(new Uint8Array(buf), setStatus);
            if (!files || !files.length) { setStatus('Import failed (no files in zip).'); return false; }

            let imported = 0;
            for (const f of files) {
                if (!f || !f.name || !f.data) continue;
                if (!String(f.name).toLowerCase().endsWith('.json')) continue;
                try {
                    const raw = new TextDecoder().decode(f.data);
                    const obj = JSON.parse(String(raw || '').trim());
                    if (importSingleUserProfileObject(obj, setStatus)) imported++;
                } catch (_) { }
            }

            if (imported > 0) {
                saveUserProfiles();
                updateProfileList();
                const activeInfo = document.getElementById('gvf-active-profile-info');
                if (activeInfo) setActiveProfileInfo(activeInfo, activeUserProfile?.name);
                setStatus(`Imported ${imported} profile(s) from ZIP.`);
                return true;
            }

            setStatus('Import failed (no valid profile JSON found).');
            return false;
        } catch (e) {
            logW('Profile import error:', e);
            setStatus('Import failed (invalid file).');
            return false;
        }
    }


    async function importLutProfilesFromZipOrJsonFile(file) {
        const name = String(file && file.name || '').toLowerCase();
        const isZip = name.endsWith('.zip') || (file && file.type === 'application/zip');

        try {
            const buf = await file.arrayBuffer();
            if (!buf || !buf.byteLength) return { ok: false, msg: 'Import failed (empty file).' };

            if (!isZip) {
                // Single JSON LUT profile
                const raw = new TextDecoder().decode(new Uint8Array(buf));
                const obj = JSON.parse(String(raw || '').trim());
                let ok = false;
                if (obj && obj.schema === 'gvf-lut-groups' && Array.isArray(obj.groups)) {
                    if (!Array.isArray(lutGroups)) lutGroups = [];
                    for (const g0 of obj.groups) {
                        const g = String(g0 || '').trim();
                        if (g && !lutGroups.some(x => String(x).trim() === g)) lutGroups.push(g);
                    }
                    saveLutGroups();
                    ok = true;
                } else {
                    ok = importSingleLutProfileObject(obj);
                }
                if (ok) {
                    saveLutProfiles();
                    try { updateLutProfileList(); } catch (_) { }
                    try { setActiveLutInfo(); } catch (_) { }
                    try { setActiveLutProfile(activeLutProfileKey); } catch (_) { }
                }
                return ok ? { ok: true, msg: (obj && obj.schema === 'gvf-lut-groups') ? 'Imported LUT groups.' : 'Imported 1 LUT profile.' } : { ok: false, msg: 'Import failed (invalid LUT JSON).' };
            }

            // ZIP
            const files = await unzipToFiles(new Uint8Array(buf), null);
            if (!files || !files.length) return { ok: false, msg: 'Import failed (no files in zip).' };

            // Clear all existing LUT profiles and groups before importing
            lutProfiles = [];
            lutGroups = [];
            activeLutProfileKey = 'none';
            activeLutMatrix4x5 = null;

            let imported = 0;
            for (const f of files) {
                if (!f || !f.name || !f.data) continue;
                if (!String(f.name).toLowerCase().endsWith('.json')) continue;
                try {
                    const raw = new TextDecoder().decode(f.data);
                    const obj = JSON.parse(String(raw || '').trim());
                    if (obj && obj.schema === 'gvf-lut-groups' && Array.isArray(obj.groups)) {
                        // Merge group list
                        if (!Array.isArray(lutGroups)) lutGroups = [];
                        for (const g0 of obj.groups) {
                            const g = String(g0 || '').trim();
                            if (g && !lutGroups.some(x => String(x).trim() === g)) lutGroups.push(g);
                        }
                        saveLutGroups();
                    } else {
                        if (importSingleLutProfileObject(obj)) imported++;
                    }
                } catch (_) { }
            }

            if (imported > 0) {
                saveLutProfiles();
                try { updateLutProfileList(); } catch (_) { }
                try { setActiveLutInfo(); } catch (_) { }
                try { setActiveLutProfile(activeLutProfileKey); } catch (_) { }
                return { ok: true, msg: `Imported ${imported} LUT profile(s) from ZIP.` };
            }

            return { ok: false, msg: 'Import failed (no valid LUT JSON found).' };
        } catch (e) {
            logW('LUT import error:', e);
            return { ok: false, msg: 'Import failed (invalid file).' };
        }
    }

    function importSingleLutProfileObject(obj) {
        if (!obj || typeof obj !== 'object') return false;

        // Accept {schema:'gvf-lut-profile', name, matrix4x5, group?} or {name, matrix4x5, group?}
        const name = String(obj.name || '').trim();
        const m = obj.matrix4x5;
        const groupRaw = (Object.prototype.hasOwnProperty.call(obj, 'group')) ? obj.group : undefined;
        let group = (groupRaw === undefined || groupRaw === null) ? undefined : String(groupRaw).trim();
        if (group === '') group = undefined;

        if (!name) return false;
        if (!Array.isArray(m) || m.length !== 20) return false;

        // Normalize to numbers
        const mat = m.map(v => Number(v));
        if (mat.some(v => !isFinite(v))) return false;

        // Overwrite on duplicate name (upsert behavior)
        upsertLutProfile({ name, group, matrix4x5: mat });
        return true;
    }

    function importSingleUserProfileObject(obj, setStatus) {
        if (!obj || typeof obj !== 'object') return false;

        const isProfileObj = (obj && typeof obj.name === 'string' && obj.settings && typeof obj.settings === 'object');
        const settingsObj = isProfileObj ? obj.settings : obj;

        if (!settingsObj || typeof settingsObj !== 'object' || (!('renderMode' in settingsObj) && !('profile' in settingsObj))) {
            return false;
        }

        const profileName = sanitizeProfileFilename(isProfileObj ? obj.name : ('Imported ' + new Date().toLocaleString()));
        const norm = (s) => sanitizeProfileFilename(String(s || '')).toLowerCase();
        const targetNorm = norm(profileName);

        let existingIdx = -1;
        for (let i = 0; i < (userProfiles || []).length; i++) {
            const p = userProfiles[i];
            if (norm(p && p.name) === targetNorm) { existingIdx = i; break; }
        }

        const now = Date.now();
        const baseSettings = buildImportedUserProfileSettings(settingsObj);
        let nextProfile = null;

        if (existingIdx >= 0) {
            const prev = userProfiles[existingIdx];
            const preservedId = prev && prev.id ? String(prev.id) : ((isProfileObj && obj.id) ? String(obj.id) : ('profile_' + now + '_' + Math.random().toString(36).slice(2, 11)));
            const preservedCreatedAt = (prev && Number(prev.createdAt)) ? Number(prev.createdAt) : ((isProfileObj && Number(obj.createdAt)) ? Number(obj.createdAt) : now);
            nextProfile = {
                id: preservedId,
                name: profileName,
                createdAt: preservedCreatedAt,
                updatedAt: now,
                settings: baseSettings
            };
            userProfiles.splice(existingIdx, 1, nextProfile);
        } else {
            nextProfile = {
                id: (isProfileObj && obj.id) ? String(obj.id) : ('profile_' + now + '_' + Math.random().toString(36).slice(2, 11)),
                name: profileName,
                createdAt: (isProfileObj && Number(obj.createdAt)) ? Number(obj.createdAt) : now,
                updatedAt: now,
                settings: baseSettings
            };
            userProfiles = normalizeUserProfilesForStorage([...(Array.isArray(userProfiles) ? userProfiles : []), nextProfile]);
            nextProfile = userProfiles.find(p => p.id === nextProfile.id) || nextProfile;
        }

        if (activeUserProfile && nextProfile && activeUserProfile.id === nextProfile.id) {
            activeUserProfile = nextProfile;
            try { applyUserProfileSettings(nextProfile.settings); } catch (_) { }
        }

        if (typeof setStatus === 'function') {
            setStatus(`${existingIdx >= 0 ? 'Replaced' : 'Imported'}: ${profileName}`);
        }

        log(existingIdx >= 0 ? 'Profile replaced:' : 'Profile imported:', profileName);
        return true;
    }

    // Minimal ZIP reader (supports STORE, and DEFLATE when DecompressionStream is available).
    async function unzipToFiles(zipU8, setStatus) {
        const dv = new DataView(zipU8.buffer, zipU8.byteOffset, zipU8.byteLength);
        const u16 = (o) => dv.getUint16(o, true);
        const u32 = (o) => dv.getUint32(o, true);

        // Find EOCD by scanning from end (max comment 64k)
        let eocd = -1;
        for (let i = zipU8.length - 22; i >= Math.max(0, zipU8.length - 22 - 65535); i--) {
            if (u32(i) === 0x06054b50) { eocd = i; break; }
        }
        if (eocd < 0) return [];

        const cdSize = u32(eocd + 12);
        const cdOff = u32(eocd + 16);

        let ptr = cdOff;
        const files = [];
        const dec = new TextDecoder();

        const canInflate = (typeof DecompressionStream !== 'undefined');

        while (ptr < cdOff + cdSize) {
            if (u32(ptr) !== 0x02014b50) break;

            const method = u16(ptr + 10);
            const cSize = u32(ptr + 20);
            const uSize = u32(ptr + 24);
            const nLen = u16(ptr + 28);
            const xLen = u16(ptr + 30);
            const cLen = u16(ptr + 32);
            const lho = u32(ptr + 42);

            const name = dec.decode(zipU8.subarray(ptr + 46, ptr + 46 + nLen));

            // Local header
            if (u32(lho) !== 0x04034b50) { ptr += 46 + nLen + xLen + cLen; continue; }
            const lnLen = u16(lho + 26);
            const lxLen = u16(lho + 28);
            const dataOff = lho + 30 + lnLen + lxLen;

            const comp = zipU8.subarray(dataOff, dataOff + cSize);

            let out;
            if (method === 0) {
                out = comp;
            } else if (method === 8 && canInflate) {
                // deflate (raw)
                try {
                    const ds = new DecompressionStream('deflate-raw');
                    const stream = new Blob([comp]).stream().pipeThrough(ds);
                    const ab = await new Response(stream).arrayBuffer();
                    out = new Uint8Array(ab);
                } catch (e) {
                    if (typeof setStatus === 'function') setStatus('Import failed (ZIP deflate unsupported).');
                    out = null;
                }
            } else {
                if (typeof setStatus === 'function') setStatus('Import failed (ZIP compression not supported).');
                out = null;
            }

            if (out && (uSize === 0 || out.length === uSize)) {
                files.push({ name, data: out });
            }

            ptr += 46 + nLen + xLen + cLen;
        }

        return files;
    }

    // Profile notification system
    let notificationTimeout = null;

    function createNotificationElement() {
        let notif = document.getElementById(NOTIFICATION_ID);
        if (notif) return notif;

        notif = document.createElement('div');
        notif.id = NOTIFICATION_ID;
        notif.style.cssText = `
            position: fixed;
            top: 20px;
            left: 20px;
            background: rgba(0, 0, 0, 0.85);
            color: #fff;
            padding: 12px 24px;
            border-radius: 30px;
            font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
            font-size: 16px;
            font-weight: 900;
            z-index: 2147483647;
            display: none;
            align-items: center;
            gap: 12px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
            border: 2px solid #2a6fdb;
            backdrop-filter: blur(5px);
            pointer-events: none;
            transform: translateZ(0);
            letter-spacing: 0.5px;
        `;

        const icon = document.createElement('span');
        icon.textContent = '🎬';
        icon.style.cssText = `
            font-size: 20px;
            filter: drop-shadow(0 0 5px #2a6fdb);
        `;

        const text = document.createElement('span');
        text.id = 'gvf-notification-text';
        text.textContent = 'Profile: Default';

        notif.appendChild(icon);
        notif.appendChild(text);
        document.body.appendChild(notif);

        return notif;
    }

    function clearNotificationTextNode(node) {
        if (!node) return;
        while (node.firstChild) node.removeChild(node.firstChild);
    }

    function prettySettingName(key) {
        const map = {
            gvf_enabled: 'Enabled',
            gvf_moody: 'Dark Moody',
            gvf_teal: 'Teal Orange',
            gvf_vib: 'Vibrant Saturation',
            gvf_icons: 'Icons',
            gvf_sl: 'Sharpen Level',
            gvf_sr: 'Sharpen Radius',
            gvf_bl: 'Black Level',
            gvf_wl: 'White Level',
            gvf_dn: 'Denoise',
            gvf_edge: 'Edge Detection',
            gvf_hdr: 'HDR',
            gvf_profile: 'Profile',
            gvf_g_hud: 'Grading HUD',
            gvf_i_hud: 'IO HUD',
            gvf_s_hud: 'Scopes HUD',
            gvf_render_mode: 'Render Mode',
            gvf_u_contrast: 'Contrast',
            gvf_u_black: 'Black',
            gvf_u_white: 'White',
            gvf_u_highlights: 'Highlights',
            gvf_u_shadows: 'Shadows',
            gvf_u_saturation: 'Saturation',
            gvf_u_vibrance: 'Vibrance',
            gvf_u_sharpen: 'Sharpen',
            gvf_u_gamma: 'Gamma',
            gvf_u_grain: 'Grain',
            gvf_u_hue: 'Hue',
            gvf_u_r_gain: 'Red Gain',
            gvf_u_g_gain: 'Green Gain',
            gvf_u_b_gain: 'Blue Gain',
            gvf_auto_on: 'Auto-Scene-Match',
            gvf_auto_strength: 'Auto Strength',
            gvf_auto_lock_wb: 'Auto Lock WB',
            gvf_notify: 'Notify',
            gvf_logs: 'Logs',
            gvf_debug: 'Debug',
            gvf_cb_filter: 'Color Blind Filter',
            enabled: 'Enabled',
            darkMoody: 'Dark Moody',
            tealOrange: 'Teal Orange',
            vibrantSat: 'Vibrant Saturation',
            sl: 'Sharpen Level',
            sr: 'Sharpen Radius',
            bl: 'Black Level',
            wl: 'White Level',
            dn: 'Denoise',
            edge: 'Edge Detection',
            hdr: 'HDR',
            profile: 'Profile',
            renderMode: 'Render Mode',
            autoOn: 'Auto-Scene-Match',
            autoStrength: 'Auto Strength',
            autoLockWB: 'Auto Lock WB',
            notify: 'Notify',
            logs: 'Logs',
            debug: 'Debug',
            cbFilter: 'Color Blind Filter'
        };
        if (Object.prototype.hasOwnProperty.call(map, key)) return map[key];
        return String(key || 'Setting')
            .replace(/^gvf_/, '')
            .replace(/_/g, ' ')
            .replace(/\b\w/g, c => c.toUpperCase());
    }

    function resolveReasonValue(reason, currentSettings) {
        const key = String(reason || '').trim();
        if (!key) return undefined;

        const gmToInternal = {
            gvf_enabled: 'enabled',
            gvf_moody: 'darkMoody',
            gvf_teal: 'tealOrange',
            gvf_vib: 'vibrantSat',
            gvf_icons: 'iconsShown',
            gvf_sl: 'sl',
            gvf_sr: 'sr',
            gvf_bl: 'bl',
            gvf_wl: 'wl',
            gvf_dn: 'dn',
            gvf_edge: 'edge',
            gvf_hdr: 'hdr',
            gvf_profile: 'profile',
            gvf_g_hud: 'gradingHudShown',
            gvf_i_hud: 'ioHudShown',
            gvf_s_hud: 'scopesHudShown',
            gvf_render_mode: 'renderMode',
            gvf_u_contrast: 'u_contrast',
            gvf_u_black: 'u_black',
            gvf_u_white: 'u_white',
            gvf_u_highlights: 'u_highlights',
            gvf_u_shadows: 'u_shadows',
            gvf_u_saturation: 'u_sat',
            gvf_u_vibrance: 'u_vib',
            gvf_u_sharpen: 'u_sharp',
            gvf_u_gamma: 'u_gamma',
            gvf_u_grain: 'u_grain',
            gvf_u_hue: 'u_hue',
            gvf_u_r_gain: 'u_r_gain',
            gvf_u_g_gain: 'u_g_gain',
            gvf_u_b_gain: 'u_b_gain',
            gvf_auto_on: 'autoOn',
            gvf_auto_strength: 'autoStrength',
            gvf_auto_lock_wb: 'autoLockWB',
            gvf_notify: 'notify',
            gvf_logs: 'logs',
            gvf_debug: 'debug',
            gvf_cb_filter: 'cbFilter'
        };

        const directKey = gmToInternal[key] || key;
        if (currentSettings && Object.prototype.hasOwnProperty.call(currentSettings, directKey)) {
            return currentSettings[directKey];
        }

        switch (directKey) {
            case 'enabled': return enabled;
            case 'darkMoody': return darkMoody;
            case 'tealOrange': return tealOrange;
            case 'vibrantSat': return vibrantSat;
            case 'autoOn': return autoOn;
            case 'autoLockWB': return autoLockWB;
            case 'notify': return notify;
            case 'logs': return logs;
            case 'debug': return debug;
            default: return undefined;
        }
    }

    function showScreenNotification(message, options = null) {
        const notif = createNotificationElement();
        const textEl = document.getElementById('gvf-notification-text');

        if (textEl) {
            clearNotificationTextNode(textEl);

            if (options && typeof options === 'object') {
                const titleLine = document.createElement('div');
                titleLine.textContent = String(options.title || message || '').trim() || 'Saved';
                textEl.appendChild(titleLine);

                const detail = String(options.detail || '').trim();
                if (detail) {
                    const detailLine = document.createElement('div');
                    detailLine.textContent = detail;
                    detailLine.style.marginTop = '4px';
                    detailLine.style.fontSize = '14px';
                    detailLine.style.fontWeight = '900';
                    if (options.detailColor) detailLine.style.color = String(options.detailColor);
                    textEl.appendChild(detailLine);
                }
            } else {
                textEl.textContent = String(message || '').trim() || 'Saved';
            }
        }

        if (notificationTimeout) {
            clearTimeout(notificationTimeout);
        }

        notif.style.display = 'flex';

        notificationTimeout = setTimeout(() => {
            notif.style.display = 'none';
            notificationTimeout = null;
        }, 3000);
    }

    function showProfileNotification(profileName) {
        showScreenNotification(`Profile: ${profileName}`);
    }

    function showToggleNotification(label, isEnabled, detail = '') {
        showScreenNotification('', {
            title: String(label || 'Toggle').trim() || 'Toggle',
            detail: detail || ((isEnabled ? 'Enabled' : 'Disabled')),
            detailColor: isEnabled ? '#4cff6a' : '#ff4c4c'
        });
    }

    function showValueNotification(label, valueText, color = '#88ccff') {
        showScreenNotification('', {
            title: String(label || 'Value').trim() || 'Value',
            detail: String(valueText || '').trim(),
            detailColor: color
        });
    }

    function showProfileCycleNotification(profileName) {
        showScreenNotification('', {
            title: 'Profile Cycle',
            detail: String(profileName || 'Default').trim() || 'Default',
            detailColor: '#88ccff'
        });
    }

    function showAutoSaveNotification(profileName, reason, currentSettings) {
        const readableName = prettySettingName(reason);
        const resolvedValue = resolveReasonValue(reason, currentSettings);
        const reasonKey = String(reason || '').trim();
        const isHdrReason = (reasonKey === 'hdr' || reasonKey === K.HDR);
        const isRenderModeReason = (reasonKey === 'renderMode' || reasonKey === K.RENDER_MODE);
        let detailText = readableName;
        let detailColor = '#ffffff';

        if (typeof resolvedValue === 'boolean') {
            if (resolvedValue) {
                detailText = `${readableName} enabled`;
                detailColor = '#4cff6a';
            } else {
                detailText = `${readableName} disabled`;
                detailColor = '#ff4c4c';
            }
        } else if (isHdrReason) {
            const hdrValue = Number(resolvedValue);
            if (Number.isFinite(hdrValue) && Math.abs(hdrValue) > 0.0001) {
                detailText = `${readableName} enabled (${hdrValue.toFixed(2)})`;
                detailColor = '#4cff6a';
            } else {
                detailText = `${readableName} disabled`;
                detailColor = '#ff4c4c';
            }
        } else if (isRenderModeReason) {
            const modeValue = String(resolvedValue || '').toLowerCase();
            if (modeValue === 'gpu') {
                detailText = `${readableName}: GPU / WebGL2 Canvas Pipeline`;
                detailColor = '#4cff6a';
            } else {
                detailText = `${readableName}: SVG`;
                detailColor = '#88ccff';
            }
        }

        showScreenNotification('', {
            title: `Profile "${profileName}" saved`,
            detail: detailText,
            detailColor: detailColor
        });
    }

    let _autoSaveProfileTimer = null;
    let _autoSaveProfileReason = '';

    function writeCurrentSettingsIntoActiveProfile(saveToStorage = false) {
        if (!activeUserProfile) return null;

        const currentSettings = stripUiOnlySettings(getCurrentSettings());
        const prevSettings = stripUiOnlySettings(activeUserProfile && activeUserProfile.settings ? activeUserProfile.settings : {});
        const changed = !settingsEqualNormalized(prevSettings, currentSettings);

        const nextProfile = {
            ...activeUserProfile,
            settings: JSON.parse(JSON.stringify(currentSettings)),
            updatedAt: changed ? Date.now() : Number(activeUserProfile && activeUserProfile.updatedAt ? activeUserProfile.updatedAt : Date.now())
        };

        const idx = Array.isArray(userProfiles)
            ? userProfiles.findIndex(p => p && p.id === nextProfile.id)
            : -1;

        if (idx >= 0) {
            userProfiles[idx] = nextProfile;
            activeUserProfile = userProfiles[idx];
        } else {
            userProfiles = normalizeUserProfilesForStorage([...(Array.isArray(userProfiles) ? userProfiles : []), nextProfile]);
            activeUserProfile = userProfiles.find(p => p.id === nextProfile.id) || nextProfile;
        }

        if (saveToStorage && changed) {
            saveUserProfiles();
        }

        return changed ? activeUserProfile : null;
    }

    function updateCurrentProfileSettings(force = false) {
        void force;
        if (!activeUserProfile || _isSwitchingUserProfile) return false;

        const changed = !!writeCurrentSettingsIntoActiveProfile(true);
        return changed;
    }

    function scheduleAutoSaveCurrentProfile(reason = '') {
        void reason;
        if (_autoSaveProfileTimer) {
            clearTimeout(_autoSaveProfileTimer);
            _autoSaveProfileTimer = null;
        }
        _autoSaveProfileReason = '';
        return false;
    }

    function getCurrentSettings() {
        return {
            enabled: enabled,
            notify: notify,
            darkMoody: darkMoody,
            tealOrange: tealOrange,
            vibrantSat: vibrantSat,
            sl: sl,
            sr: sr,
            bl: bl,
            wl: wl,
            dn: dn,
            edge: edge,
            hdr: hdr,
            profile: profile,
            renderMode: renderMode,
            lutProfile: activeLutProfileKey,
            autoOn: autoOn,
            autoStrength: autoStrength,
            autoLockWB: autoLockWB,
            u_contrast: u_contrast,
            u_black: u_black,
            u_white: u_white,
            u_highlights: u_highlights,
            u_shadows: u_shadows,
            u_sat: u_sat,
            u_vib: u_vib,
            u_sharp: u_sharp,
            u_gamma: u_gamma,
            u_grain: u_grain,
            u_hue: u_hue,
            u_r_gain: u_r_gain,
            u_g_gain: u_g_gain,
            u_b_gain: u_b_gain,
            debug: debug,
            logs: logs,
            cbFilter: cbFilter
        };
    }

    function applyUserProfileSettings(settings) {
        _suspendSync = true;
        _inSync = true;
        _isApplyingUserProfileSettings = true;
        suppressValueSync(700);

        try {
            enabled = settings.enabled ?? enabled;
            notify = settings.notify ?? notify;
            darkMoody = settings.darkMoody ?? darkMoody;
            tealOrange = settings.tealOrange ?? tealOrange;
            vibrantSat = settings.vibrantSat ?? vibrantSat;

            sl = settings.sl ?? sl;
            sr = settings.sr ?? sr;
            bl = settings.bl ?? bl;
            wl = settings.wl ?? wl;
            dn = settings.dn ?? dn;
            edge = settings.edge ?? edge;

            hdr = settings.hdr ?? hdr;
            profile = settings.profile ?? profile;
            renderMode = settings.renderMode ?? renderMode;

            // Restore LUT profile for this user profile (if present)
            if (Object.prototype.hasOwnProperty.call(settings, 'lutProfile')) {
                const lpRaw = String(settings.lutProfile || 'none').trim() || 'none';
                try { setActiveLutProfile(lpRaw, undefined, { skipProfileSave: true, skipVisualApply: true }); } catch (_) { }
            }

            autoOn = settings.autoOn ?? autoOn;
            autoStrength = settings.autoStrength ?? autoStrength;
            autoLockWB = settings.autoLockWB ?? autoLockWB;

            u_contrast = settings.u_contrast ?? u_contrast;
            u_black = settings.u_black ?? u_black;
            u_white = settings.u_white ?? u_white;
            u_highlights = settings.u_highlights ?? u_highlights;
            u_shadows = settings.u_shadows ?? u_shadows;
            u_sat = settings.u_sat ?? u_sat;
            u_vib = settings.u_vib ?? u_vib;
            u_sharp = settings.u_sharp ?? u_sharp;
            u_gamma = settings.u_gamma ?? u_gamma;
            u_grain = settings.u_grain ?? u_grain;
            u_hue = settings.u_hue ?? u_hue;

            u_r_gain = settings.u_r_gain ?? u_r_gain;
            u_g_gain = settings.u_g_gain ?? u_g_gain;
            u_b_gain = settings.u_b_gain ?? u_b_gain;

            cbFilter = settings.cbFilter ?? cbFilter;

            // Save in GM
            gmSet(K.enabled, enabled);
            gmSet(K.NOTIFY, notify);
            gmSet(K.moody, darkMoody);
            gmSet(K.teal, tealOrange);
            gmSet(K.vib, vibrantSat);
            gmSet(K.icons, iconsShown);

            gmSet(K.SL, sl);
            gmSet(K.SR, sr);
            gmSet(K.BL, bl);
            gmSet(K.WL, wl);
            gmSet(K.DN, dn);
            gmSet(K.EDGE, edge);

            gmSet(K.HDR, hdr);
            if (hdr !== 0) gmSet(K.HDR_LAST, hdr);

            gmSet(K.PROF, profile);
            gmSet(K.RENDER_MODE, renderMode);
            gmSet(K.NOTIFY, notify);

            gmSet(K.AUTO_ON, autoOn);
            gmSet(K.AUTO_STRENGTH, autoStrength);
            gmSet(K.AUTO_LOCK_WB, autoLockWB);

            gmSet(K.U_CONTRAST, u_contrast);
            gmSet(K.U_BLACK, u_black);
            gmSet(K.U_WHITE, u_white);
            gmSet(K.U_HIGHLIGHTS, u_highlights);
            gmSet(K.U_SHADOWS, u_shadows);
            gmSet(K.U_SAT, u_sat);
            gmSet(K.U_VIB, u_vib);
            gmSet(K.U_SHARP, u_sharp);
            gmSet(K.U_GAMMA, u_gamma);
            gmSet(K.U_GRAIN, u_grain);
            gmSet(K.U_HUE, u_hue);

            gmSet(K.U_R_GAIN, u_r_gain);
            gmSet(K.U_G_GAIN, u_g_gain);
            gmSet(K.U_B_GAIN, u_b_gain);

            gmSet(K.LOGS, logs);
            gmSet(K.DEBUG, debug);
            gmSet(K.CB_FILTER, cbFilter);

            // Apply filter
            if (renderMode === 'gpu') {
                applyGpuFilter();
            } else {
                regenerateSvgImmediately();
            }

            setAutoOn(autoOn, { silent: true });
            scheduleOverlayUpdate();

            log('Profile settings applied');
        } finally {
            _isApplyingUserProfileSettings = false;
            _inSync = false;
            _suspendSync = false;
        }
    }

    // -------------------------
    // INSTANT SVG REGENERATION
    // -------------------------
    let _svgNeedsRegeneration = false;

    function regenerateSvgImmediately() {
        if (_svgNeedsRegeneration) return;
        _svgNeedsRegeneration = true;
        try {
            ensureSvgFilter(true);
            applyFilter({ skipSvgIfPossible: false });
        } finally {
            _svgNeedsRegeneration = false;
        }
        updateCustomWebglOverlays();
        updateCustomCanvas2DOverlays();
    }

    /**
     * Renders a video frame onto destCanvas with the given profile settings applied,
     * WITHOUT touching live global variables or the live SVG filter in the DOM.
     * Uses a temporary hidden SVG filter that is inserted and immediately removed.
     * @param {HTMLCanvasElement} destCanvas  - target canvas (should be 1280x720)
     * @param {HTMLCanvasElement} srcCanvas   - pre-captured raw frame canvas
     * @param {object} settings               - profile.settings object
     */
    function renderFrameWithSettings(destCanvas, srcCanvas, settings) {
        if (!destCanvas || !srcCanvas) return;
        const s = settings || {};
        const LW = destCanvas.width, LH = destCanvas.height;

        // --- Build temporary SVG filter from settings without changing globals ---
        const tmpSvgId  = 'gvf-preview-tmp-svg-' + Math.random().toString(36).slice(2);
        const tmpFiltId = 'gvf-preview-tmp-filt';

        // Compute all the values locally from settings (mirrors ensureSvgFilter logic)
        const _clamp  = (n,a,b) => Math.min(b, Math.max(a, n));
        const _round  = (n,s) => Math.round(n/s)*s;
        const _snap0  = (n,e) => Math.abs(n) <= e ? 0 : n;
        const _normSL = () => _snap0(_round(_clamp(Number(s.sl)||0,-2,2),0.01),0.005);
        const _normSR = () => _snap0(_round(_clamp(Number(s.sr)||0,-2,2),0.01),0.005);
        const _normBL = () => _snap0(_round(_clamp(Number(s.bl)||0,-2,2),0.01),0.005);
        const _normWL = () => _snap0(_round(_clamp(Number(s.wl)||0,-2,2),0.01),0.005);
        const _normDN = () => _snap0(_round(_clamp(Number(s.dn)||0,-1.5,1.5),0.01),0.005);
        const _normHDR= () => _snap0(_round(_clamp(Number(s.hdr)||0,-1,2),0.01),0.005);
        const _normED = () => _snap0(_round(_clamp(Number(s.edge)||0,0,1),0.01),0.005);
        const _normU  = (v) => _round(_clamp(Number(v)||0,-10,10),0.1);
        const _normRGB= (v) => _clamp(Math.round(Number(v)||128),0,255);

        const SL  = Number(_normSL().toFixed(1));
        const SR  = Number(_normSR().toFixed(1));
        const R   = Number(Math.max(0.1, Math.abs(_normSR())).toFixed(1));
        const A   = Number(Math.max(0, _normSL()).toFixed(3));
        const BS  = Number(Math.max(0, -_normSL()).toFixed(3));
        const BL  = Number(_normBL().toFixed(1));
        const WL  = Number(_normWL().toFixed(1));
        const DN  = Number(_normDN().toFixed(1));
        const HDR = Number(_normHDR().toFixed(2));
        const EDGE= Number(_normED().toFixed(2));
        const P   = String(s.profile || 'off');
        const CB  = String(s.cbFilter || 'none');
        const _bOff = _clamp(BL,-2,2)*0.04;
        const _wAdj = _clamp(WL,-2,2)*0.06;

        const moody = !!s.darkMoody, teal = !!s.tealOrange, vib = !!s.vibrantSat;

        // Pick combo filter id
        const comboSuffix = (moody?'m':'')+(teal?'t':'')+(vib?'v':'');
        const tmpComboId = tmpFiltId + (comboSuffix ? '_' + comboSuffix : '');

        // Build temp SVG with the needed combo filter
        const tmpSvg = document.createElementNS(svgNS, 'svg');
        tmpSvg.id = tmpSvgId;
        tmpSvg.setAttribute('width','0'); tmpSvg.setAttribute('height','0');
        tmpSvg.style.cssText = 'position:absolute;left:-99999px;top:-99999px;pointer-events:none;';

        try {
            buildFilter(tmpSvg, tmpComboId, { moody, teal, vib }, R, A, BS, _bOff, _wAdj, DN, EDGE, HDR, P);
        } catch(_) {}

        (document.body || document.documentElement).appendChild(tmpSvg);

        // Build CSS filter string
        const baseTone = (s.enabled !== false) ? ' brightness(1.02) contrast(1.05) saturate(1.21)' : '';
        let profTone = '';
        if (P==='film')    profTone = ' brightness(1.01) contrast(1.08) saturate(1.08)';
        if (P==='anime')   profTone = ' brightness(1.03) contrast(1.10) saturate(1.16)';
        if (P==='gaming')  profTone = ' brightness(1.01) contrast(1.12) saturate(1.06)';
        if (P==='eyecare') profTone = ' brightness(1.05) contrast(0.96) saturate(0.88) hue-rotate(-12deg)';
        let userTone = '';
        if (P==='user') {
            const uc=_normU(s.u_contrast),us=_normU(s.u_sat),uv=_normU(s.u_vib),uh=_normU(s.u_hue);
            const ub=_normU(s.u_black),uw=_normU(s.u_white),ush=_normU(s.u_shadows),uhi=_normU(s.u_highlights),ug=_normU(s.u_gamma);
            const c   = _clamp(1.0+uc*0.04,0.6,1.6);
            const sat = _clamp(1.0+us*0.05,0.4,1.8);
            const vb  = _clamp(1.0+uv*0.02,0.7,1.35);
            const hue = _clamp(uh*3.0,-30,30);
            const blk = _clamp(ub*0.012,-0.12,0.12);
            const wht = _clamp(uw*0.012,-0.12,0.12);
            const sh  = _clamp(ush*0.010,-0.10,0.10);
            const hi  = _clamp(uhi*0.010,-0.10,0.10);
            const br  = _clamp(1.0+(-blk+wht+sh+hi)*0.6,0.7,1.35);
            const g   = _clamp(1.0+ug*0.025,0.6,1.6);
            const gBr = _clamp(1.0+(1.0-g)*0.18,0.85,1.2);
            const gCt = _clamp(1.0+(g-1.0)*0.10,0.9,1.15);
            userTone = ` brightness(${(br*gBr).toFixed(3)}) contrast(${(c*gCt).toFixed(3)}) saturate(${(sat*vb).toFixed(3)}) hue-rotate(${hue.toFixed(1)}deg)`;
        }

        const filterStr = `url("#${tmpComboId}")${baseTone}${profTone}${userTone}`;

        // Render srcCanvas → destCanvas with filter
        try {
            const ctx = destCanvas.getContext('2d', { alpha: false });
            if (ctx) {
                ctx.save();
                ctx.filter = filterStr;
                ctx.drawImage(srcCanvas, 0, 0, LW, LH);
                ctx.restore();
            }
        } catch(_) {} finally {
            // Always remove temp SVG immediately
            try { tmpSvg.remove(); } catch(_) {}
        }
    }


    /**
     * Determines the currently active filter string for screenshots/recordings
     */
    function getCurrentFilterString() {
        try {
            // First, try to extract the current CSS filter from the style element.
            const style = document.getElementById(STYLE_ID);
            if (style && style.textContent) {
                const match = style.textContent.match(/filter:\s*([^!;]+)/);
                if (match && match[1]) {
                    return match[1].trim();
                }
            }

            // Fallback: Create the filter string based on the current settings
            if (renderMode === 'gpu') {
                return getGpuFilterString();
            } else {
                // For SVG mode: Return the URL filter
                const comboId = pickComboId();
                return `url("#${comboId}")${getBaseToneString()}${getProfileToneString()}${getUserToneString()}`;
            }
        } catch (e) {
            logW('Error determining the filter string:', e);
            return 'none';
        }
    }

    function getBaseToneString() {
        return enabled ? ' brightness(1.02) contrast(1.05) saturate(1.21)' : '';
    }

    function getProfileToneString() {
        if (profile === 'film') return ' brightness(1.01) contrast(1.08) saturate(1.08)';
        if (profile === 'anime') return ' brightness(1.03) contrast(1.10) saturate(1.16)';
        if (profile === 'gaming') return ' brightness(1.01) contrast(1.12) saturate(1.06)';
        if (profile === 'eyecare') return ' brightness(1.05) contrast(0.96) saturate(0.88) hue-rotate(-12deg)';
        return '';
    }

    function getUserToneString() {
        if (profile !== 'user') return '';

        const c = clamp(1.0 + (uDelta(u_contrast) * 0.04), 0.60, 1.60);
        const sat = clamp(1.0 + (uDelta(u_sat) * 0.05), 0.40, 1.80);
        const vib = clamp(1.0 + (uDelta(u_vib) * 0.02), 0.70, 1.35);
        const hue = clamp(uDelta(u_hue) * 3.0, -30, 30);

        const blk = clamp(uDelta(u_black) * 0.012, -0.12, 0.12);
        const wht = clamp(uDelta(u_white) * 0.012, -0.12, 0.12);
        const sh = clamp(uDelta(u_shadows) * 0.010, -0.10, 0.10);
        const hi = clamp(uDelta(u_highlights) * 0.010, -0.10, 0.10);

        const br = clamp(1.0 + (-blk + wht + sh + hi) * 0.6, 0.70, 1.35);

        const g = clamp(1.0 + (uDelta(u_gamma) * 0.025), 0.60, 1.60);
        const gBr = clamp(1.0 + (1.0 - g) * 0.18, 0.85, 1.20);
        const gCt = clamp(1.0 + (g - 1.0) * 0.10, 0.90, 1.15);

        return ` brightness(${(br * gBr).toFixed(3)}) contrast(${(c * gCt).toFixed(3)}) saturate(${(sat * vib).toFixed(3)}) hue-rotate(${hue.toFixed(1)}deg)`;
    }


    // -------------------------
    // Screenshot / Recording helpers
    // -------------------------

    /**
     * Draws all active GLSL (WebGL-type) Custom Filter Code canvases onto a 2D ctx.
     * Must be called AFTER ctx.drawImage(video, ...) so the overlay composites on top.
     * Uses 'source-over' (default) to respect each shader's alpha output.
     */
    function bakeWebglOverlaysOntoCanvas(ctx, w, h) {
        try {
            document.querySelectorAll('[data-gvf-custom-webgl]').forEach(webglCanvas => {
                try {
                    ctx.save();
                    ctx.globalCompositeOperation = 'source-over';
                    ctx.drawImage(webglCanvas, 0, 0, w, h);
                    ctx.restore();
                } catch (_) { }
            });
        } catch (_) { }
    }

    function bakeCanvas2DOverlaysOntoCanvas(ctx, w, h) {
        try {
            document.querySelectorAll('[data-gvf-custom-canvas2d]').forEach(c2dCanvas => {
                try {
                    if (c2dCanvas.style.display === 'none') return;
                    ctx.save();
                    ctx.globalCompositeOperation = 'source-over';
                    ctx.drawImage(c2dCanvas, 0, 0, w, h);
                    ctx.restore();
                } catch (_) { }
            });
        } catch (_) { }
    }

    function dlBlob(blob, filename) {
        try {
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            a.remove();
            setTimeout(() => URL.revokeObjectURL(url), 1500);
        } catch (_) { }
    }

    function tsName(prefix, ext) {
        const d = new Date();
        const pad = (n) => String(n).padStart(2, '0');
        return `${prefix}_${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}_${pad(d.getHours())}-${pad(d.getMinutes())}-${pad(d.getSeconds())}.${ext}`;
    }

    function getActiveVideoForCapture() {
        try {
            const v = (typeof choosePrimaryVideo === 'function') ? choosePrimaryVideo() : null;
            if (v) return v;
        } catch (_) { }
        return document.querySelector('video');
    }

    function getAppliedCssFilterString(video) {
        try {
            const cs = window.getComputedStyle(video);
            let f = String(cs.filter || '').trim();
            if (!f || f === 'none') return '';
            f = f.replace(/url\([^)]+\)/g, '').replace(/\s+/g, ' ').trim();
            if (!f || f === 'none') return '';
            return f;
        } catch (_) {
            return '';
        }
    }

    function canBakeToCanvas(video) {
        try {
            const w = Math.max(2, video.videoWidth || 0);
            const h = Math.max(2, video.videoHeight || 0);
            if (!w || !h) return { ok: false, reason: 'Video not ready.' };

            const c = document.createElement('canvas');
            c.width = 2; c.height = 2;
            const ctx = c.getContext('2d');
            ctx.drawImage(video, 0, 0, 2, 2);
            ctx.getImageData(0, 0, 1, 1);
            return { ok: true, reason: '' };
        } catch (_) {
            return { ok: false, reason: 'Blocked (DRM/cross-origin).' };
        }
    }

    // -------------------------
    // Firefox audio tap
    // -------------------------
    const AUDIO_TAPS = new WeakMap();

    function ensureAudioTap(video) {
        try {
            if (!video) return null;

            const existing = AUDIO_TAPS.get(video);
            if (existing && existing.dest && existing.dest.stream) {
                const tracks = existing.dest.stream.getAudioTracks ? existing.dest.stream.getAudioTracks() : [];
                if (tracks && tracks.length) return { tracks, note: 'webaudio' };
            }

            const AC = window.AudioContext || window.webkitAudioContext;
            if (!AC) return null;

            const ctx = new AC({ latencyHint: 'interactive' });
            const src = ctx.createMediaElementSource(video);
            const gain = ctx.createGain();
            gain.gain.value = 1.0;

            const dest = ctx.createMediaStreamDestination();

            src.connect(gain);
            gain.connect(ctx.destination);
            gain.connect(dest);

            const tap = { ctx, src, gain, dest };
            AUDIO_TAPS.set(video, tap);

            const tracks = dest.stream.getAudioTracks ? dest.stream.getAudioTracks() : [];
            if (!tracks || !tracks.length) return null;

            tracks.forEach(t => { try { t.__gvfNoStop = true; } catch (_) { } });
            return { tracks, note: 'webaudio' };
        } catch (_) {
            return null;
        }
    }

    async function resumeAudioContextsFor(video) {
        try {
            const tap = AUDIO_TAPS.get(video);
            if (tap && tap.ctx && tap.ctx.state === 'suspended') {
                await tap.ctx.resume();
            }
        } catch (_) { }
    }

    // ---------- Canvas pipeline for recording ----------
    const REC_PIPE = {
        active: false,
        v: null,
        canvas: null,
        ctx: null,
        raf: 0,
        stream: null,
        lastDraw: 0,
        fps: 60,
        audioTracks: [],
        stopFn: null
    };

    function stopCanvasRecorderPipeline() {
        try { if (REC_PIPE.raf) cancelAnimationFrame(REC_PIPE.raf); } catch (_) { }
        REC_PIPE.raf = 0;

        try {
            REC_PIPE.audioTracks.forEach(t => {
                try { if (t && !t.__gvfNoStop) t.stop(); } catch (_) { }
            });
        } catch (_) { }
        REC_PIPE.audioTracks = [];

        try {
            if (REC_PIPE.stream) {
                REC_PIPE.stream.getTracks().forEach(t => {
                    try { if (t && !t.__gvfNoStop) t.stop(); } catch (_) { }
                });
            }
        } catch (_) { }

        REC_PIPE.active = false;
        REC_PIPE.v = null;
        REC_PIPE.stream = null;
        REC_PIPE.canvas = null;
        REC_PIPE.ctx = null;
        REC_PIPE.lastDraw = 0;
        REC_PIPE.stopFn = null;
    }

    function startCanvasRecorderPipeline(video, statusEl) {
        const w = Math.max(2, video.videoWidth || 0);
        const h = Math.max(2, video.videoHeight || 0);
        if (!w || !h) return null;

        const c = document.createElement('canvas');
        c.width = w;
        c.height = h;

        const ctx = c.getContext('2d', { alpha: false, desynchronized: true });
        if (!ctx) return null;

        ctx.imageSmoothingEnabled = true;
        try { ctx.imageSmoothingQuality = 'high'; } catch (_) { }

        // FIX: Use the correct filter string for recording
        const filterString = getCurrentFilterString();
        log('Using filter for recording:', filterString);

        const draw = (t) => {
            if (!REC_PIPE.active) return;

            const dt = t - (REC_PIPE.lastDraw || 0);
            const minDt = 1000 / Math.max(10, REC_PIPE.fps);
            if (dt < (minDt * 0.55)) {
                REC_PIPE.raf = requestAnimationFrame(draw);
                return;
            }
            REC_PIPE.lastDraw = t;

            try {
                ctx.save();
                ctx.filter = filterString || 'none';
                ctx.drawImage(video, 0, 0, w, h);
                ctx.restore();
                // Composite active GLSL (WebGL-type) Custom Filter Code overlays onto recording frame
                bakeWebglOverlaysOntoCanvas(ctx, w, h);
                // Composite active Canvas 2D overlays onto recording frame
                bakeCanvas2DOverlaysOntoCanvas(ctx, w, h);
            } catch (e) {
                if (statusEl) statusEl.textContent = 'Recording stopped: blocked (DRM/cross-origin).';
                // FIX: Evaluate REC.stopRequested
                REC.stopRequested = true;
                if (REC.mr && REC.mr.state === 'recording') {
                    try { REC.mr.stop(); } catch (_) { }
                }
                return;
            }

            REC_PIPE.raf = requestAnimationFrame(draw);
        };

        let stream = null;
        try { stream = c.captureStream(REC_PIPE.fps); } catch (_) { return null; }

        let audioTracks = [];
        let audioNote = '';
        try {
            if (isFirefox()) {
                const tap = ensureAudioTap(video);
                if (tap && tap.tracks && tap.tracks.length) {
                    audioTracks = tap.tracks.slice();
                    audioNote = 'Audio: WebAudio tap';
                }
            }
            if (!audioTracks.length) {
                const vs = (video.captureStream && video.captureStream()) || (video.mozCaptureStream && video.mozCaptureStream());
                if (vs) {
                    const at = vs.getAudioTracks ? vs.getAudioTracks() : [];
                    if (at && at.length) {
                        audioTracks = at.slice();
                        audioNote = 'Audio: captureStream';
                    }
                }
            }
        } catch (_) { }

        try {
            (audioTracks || []).forEach(at => {
                try {
                    stream.addTrack(at);
                    REC_PIPE.audioTracks.push(at);
                } catch (_) { }
            });
        } catch (_) { }

        REC_PIPE.active = true;
        REC_PIPE.v = video;
        REC_PIPE.canvas = c;
        REC_PIPE.ctx = ctx;
        REC_PIPE.stream = stream;
        REC_PIPE.lastDraw = 0;

        REC_PIPE.raf = requestAnimationFrame(draw);

        if (statusEl && audioTracks.length && audioNote) {
            if (statusEl.textContent && statusEl.textContent.startsWith('Tip:')) {
                statusEl.textContent = audioNote;
            }
        }

        return stream;
    }

    // ---------- Robust recorder ----------
    const REC = {
        active: false,
        stopRequested: false,
        mr: null,
        chunks: [],
        v: null,
        mime: '',
        ext: 'webm',
        startTime: 0,
        timerInterval: null,
        currentVideo: null  // Track current video for HUD positioning
    };

    function pickRecorderMime(hasAudio) {
        const mp4Audio = [
            'video/mp4;codecs=avc1.4D401F,mp4a.40.2',
            'video/mp4;codecs=avc1.4D401F,mp4a.40.2',
            'video/mp4'
        ];
        const webmAudio = [
            'video/webm;codecs=vp9,opus',
            'video/webm;codecs=vp8,opus',
            'video/webm;codecs=vp9',
            'video/webm;codecs=vp8',
            'video/webm'
        ];
        const mp4NoAudio = [
            'video/mp4;codecs=avc1.4D401F',
            'video/mp4;codecs=avc1.4D401F',
            'video/mp4'
        ];
        const webmNoAudio = [
            'video/webm;codecs=vp9',
            'video/webm;codecs=vp8',
            'video/webm'
        ];
        const cands = hasAudio ? [...mp4Audio, ...webmAudio] : [...mp4NoAudio, ...webmNoAudio];
        for (const m of cands) {
            try { if (window.MediaRecorder && MediaRecorder.isTypeSupported && MediaRecorder.isTypeSupported(m)) return m; } catch (_) { }
        }
        return '';
    }

    function getExtFromMime(mime) {
        const m = String(mime || '').toLowerCase();
        if (m.includes('video/mp4')) return 'mp4';
        return 'webm';
    }

    function safeBlobTypeFromRecorder(mr, fallback) {
        try {
            const mt = (mr && mr.mimeType) ? String(mr.mimeType) : '';
            if (mt) return mt;
        } catch (_) { }
        return fallback || 'video/webm';
    }

    // Recording HUD functions
    function createRecordingHUD() {
        let hud = document.getElementById(RECORDING_HUD_ID);
        if (hud) return hud;

        hud = document.createElement('div');
        hud.id = RECORDING_HUD_ID;
        hud.style.cssText = `
            position: absolute;
            top: 10px;
            left: 10px;
            background: rgba(0, 0, 0, 0.8);
            color: #ff4444;
            padding: 6px 12px;
            border-radius: 20px;
            font-family: monospace;
            font-size: 14px;
            font-weight: bold;
            z-index: 2147483647;
            display: none;
            align-items: center;
            gap: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.5);
            border: 1px solid rgba(255, 68, 68, 0.6);
            backdrop-filter: blur(4px);
            pointer-events: none;
            transform: translateZ(0);
            letter-spacing: 0.5px;
        `;

        const redDot = document.createElement('span');
        redDot.style.cssText = `
            width: 12px;
            height: 12px;
            background: #ff4444;
            border-radius: 50%;
            display: inline-block;
            animation: gvf-record-pulse 1.2s ease-in-out infinite;
            box-shadow: 0 0 10px rgba(255, 68, 68, 0.8);
        `;

        const timeDisplay = document.createElement('span');
        timeDisplay.id = 'gvf-record-time';
        timeDisplay.textContent = '00:00';
        timeDisplay.style.cssText = `
            text-shadow: 0 0 5px rgba(255, 68, 68, 0.5);
        `;

        hud.appendChild(redDot);
        hud.appendChild(timeDisplay);

        // Add animation style if not already present
        if (!document.getElementById('gvf-record-style')) {
            const style = document.createElement('style');
            style.id = 'gvf-record-style';
            style.textContent = `
                @keyframes gvf-record-pulse {
                    0% { opacity: 1; transform: scale(1); }
                    50% { opacity: 0.6; transform: scale(1.2); }
                    100% { opacity: 1; transform: scale(1); }
                }
            `;
            document.head.appendChild(style);
        }

        return hud;
    }

    function positionRecordingHUD(video) {
        const hud = document.getElementById(RECORDING_HUD_ID);
        if (!hud || !video) return;

        // Keep HUD in document.body so it shares the same stacking context as
        // the fixed GLSL overlay canvases (z-index:2147483645). That guarantees
        // our z-index:2147483647 actually wins and the timer stays visible.
        if (hud.parentNode !== document.body) {
            document.body.appendChild(hud);
        }

        const r = video.getBoundingClientRect();
        hud.style.position = 'fixed';
        hud.style.top = (r.top + 10) + 'px';
        hud.style.left = (r.left + 10) + 'px';
        hud.style.right = 'auto';
        hud.style.bottom = 'auto';
        hud.style.transform = 'none';
    }

    function updateRecordingTimer() {
        if (!REC.active) return;

        const hud = document.getElementById(RECORDING_HUD_ID);
        if (!hud) return;

        const timeDisplay = document.getElementById('gvf-record-time');
        if (!timeDisplay) return;

        const elapsed = Math.floor((Date.now() - REC.startTime) / 1000);
        const minutes = Math.floor(elapsed / 60);
        const seconds = elapsed % 60;
        timeDisplay.textContent = `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;


        if (REC.currentVideo && REC.currentVideo.parentNode) {
            positionRecordingHUD(REC.currentVideo);
        }
    }

    function startRecordingTimer(video) {
        REC.startTime = Date.now();
        REC.currentVideo = video;


        const hud = createRecordingHUD();

        document.body.appendChild(hud);
        positionRecordingHUD(video);

        hud.style.display = 'flex';

        if (REC.timerInterval) clearInterval(REC.timerInterval);
        REC.timerInterval = setInterval(updateRecordingTimer, 100);

        log('Recording HUD started');
    }

    function stopRecordingTimer() {
        if (REC.timerInterval) {
            clearInterval(REC.timerInterval);
            REC.timerInterval = null;
        }

        const hud = document.getElementById(RECORDING_HUD_ID);
        if (hud) {
            hud.style.display = 'none';

            if (hud.parentNode) {
                hud.parentNode.removeChild(hud);
            }
        }
        REC.currentVideo = null;
        log('Recording HUD stopped');
    }

    async function takeVideoScreenshot(statusEl) {
        const v = getActiveVideoForCapture();
        if (!v) { if (statusEl) statusEl.textContent = 'No video found.'; return; }

        const w = Math.max(2, v.videoWidth || 0);
        const h = Math.max(2, v.videoHeight || 0);
        if (!w || !h) { if (statusEl) statusEl.textContent = 'Video not ready.'; return; }

        const chk = canBakeToCanvas(v);
        if (!chk.ok) { if (statusEl) statusEl.textContent = `Screenshot blocked: ${chk.reason}`; return; }

        const c = document.createElement('canvas');
        c.width = w;
        c.height = h;
        const ctx = c.getContext('2d', { alpha: false, desynchronized: true });
        if (!ctx) { if (statusEl) statusEl.textContent = 'Canvas unavailable.'; return; }

        ctx.imageSmoothingEnabled = true;
        try { ctx.imageSmoothingQuality = 'high'; } catch (_) { }

        // FIX: Use the correct filter string for screenshots
        const filterString = getCurrentFilterString();
        log('Using filter for screenshot:', filterString);

        try {
            ctx.save();
            ctx.filter = filterString || 'none';
            ctx.drawImage(v, 0, 0, w, h);
            ctx.restore();
            ctx.getImageData(0, 0, 1, 1);
        } catch (_) {
            if (statusEl) statusEl.textContent = 'Screenshot blocked (cross-origin/DRM).';
            return;
        }

        // Composite active GLSL (WebGL-type) Custom Filter Code overlays onto screenshot
        bakeWebglOverlaysOntoCanvas(ctx, w, h);
        // Composite active Canvas 2D overlays onto screenshot
        bakeCanvas2DOverlaysOntoCanvas(ctx, w, h);

        c.toBlob((blob) => {
            if (!blob) { if (statusEl) statusEl.textContent = 'Screenshot failed.'; return; }
            const name = tsName('gvf_screenshot', 'png');
            dlBlob(blob, name);
            if (statusEl) statusEl.textContent = `Screenshot saved: ${name}`;
        }, 'image/png');
    }

    async function toggleVideoRecord(statusEl, btnEl) {
        if (REC.active) {
            try {
                REC.stopRequested = true;

                if (btnEl) {
                    btnEl.textContent = 'Stopping...';
                    btnEl.disabled = true;
                    btnEl.style.opacity = '0.6';
                    btnEl.style.cursor = 'not-allowed';
                }
                if (statusEl) statusEl.textContent = 'Finalizing recording...';

                if (REC.mr && REC.mr.state === 'recording') {
                    try { REC.mr.requestData(); } catch (_) { }
                    setTimeout(() => {
                        try { REC.mr.stop(); } catch (_) { }
                    }, 700);
                } else {
                    try { REC.mr && REC.mr.stop(); } catch (_) { }
                }

                stopRecordingTimer();
            } catch (_) { }
            return;
        }

        const v = getActiveVideoForCapture();
        if (!v) { if (statusEl) statusEl.textContent = 'No video found.'; return; }

        const chk = canBakeToCanvas(v);
        if (!chk.ok) {
            if (statusEl) statusEl.textContent = `Recording disabled: ${chk.reason}`;
            if (btnEl) {
                btnEl.disabled = true;
                btnEl.textContent = 'DRM blocked';
                btnEl.style.opacity = '0.55';
                btnEl.style.cursor = 'not-allowed';
            }
            return;
        }

        if (!window.MediaRecorder) {
            if (statusEl) statusEl.textContent = 'MediaRecorder not supported.';
            return;
        }

        try { if (isFirefox()) await resumeAudioContextsFor(v); } catch (_) { }

        const filteredStream = startCanvasRecorderPipeline(v, statusEl);
        if (!filteredStream) {
            if (statusEl) statusEl.textContent = 'Recording not supported (canvas capture failed).';
            return;
        }

        const hasAudio = (() => {
            try { return filteredStream.getAudioTracks && filteredStream.getAudioTracks().length > 0; } catch (_) { }
            return false;
        })();

        const mime = pickRecorderMime(hasAudio);
        if (!mime) {
            stopCanvasRecorderPipeline();
            if (statusEl) statusEl.textContent = 'No supported recording format (mp4/webm).';
            return;
        }

        const ext = getExtFromMime(mime);

        REC.active = true;
        REC.stopRequested = false;
        REC.v = v;
        REC.mime = mime;
        REC.ext = ext;
        REC.chunks = [];

        if (btnEl) {
            btnEl.disabled = false;
            btnEl.textContent = 'Stop Record';
            btnEl.style.opacity = '1';
            btnEl.style.cursor = 'pointer';
        }

        if (statusEl) {
            if (hasAudio) statusEl.textContent = `Recording... (${ext.toUpperCase()})`;
            else statusEl.textContent = `Recording... (${ext.toUpperCase()} (no audio)) — site may block audio capture.`;
        }

        startRecordingTimer(v);

        let mr;
        try {
            const opts = {
                mimeType: mime,
                videoBitsPerSecond: 6_000_000,
                audioBitsPerSecond: 96_000
            };
            mr = new MediaRecorder(filteredStream, opts);
        } catch (_) {
            stopCanvasRecorderPipeline();
            REC.active = false;
            stopRecordingTimer();
            if (btnEl) btnEl.textContent = 'Record';
            if (statusEl) statusEl.textContent = 'Recorder init failed.';
            return;
        }

        REC.mr = mr;

        mr.ondataavailable = (ev) => {
            if (ev && ev.data && ev.data.size > 0) REC.chunks.push(ev.data);
        };

        mr.onerror = () => {
            try { mr.stop(); } catch (_) { }
        };

        mr.onstop = () => {
            setTimeout(() => {
                try {
                    const type = safeBlobTypeFromRecorder(mr, (REC.ext === 'mp4' ? 'video/mp4' : 'video/webm'));
                    const blob = new Blob(REC.chunks, { type });

                    if (!blob || blob.size < 50_000) {
                        if (statusEl) statusEl.textContent = 'Save failed (empty/too small). DRM/cross-origin or tab slept.';
                    } else {
                        const name = tsName('gvf_record', REC.ext);
                        dlBlob(blob, name);

                        if (statusEl) {
                            const note = (REC.ext === 'webm')
                                ? 'Saved (WebM). If Windows player refuses: open with VLC.'
                                : 'Saved (MP4).';
                            statusEl.textContent = `Saved: ${name} — ${note}`;
                        }
                    }
                } catch (e) {
                    if (statusEl) statusEl.textContent = 'Save failed.';
                }

                stopCanvasRecorderPipeline();

                REC.active = false;
                REC.mr = null;
                REC.chunks = [];
                REC.v = null;
                REC.mime = '';
                REC.ext = 'webm';
                REC.stopRequested = false;

                if (btnEl) {
                    btnEl.disabled = false;
                    btnEl.style.opacity = '1';
                    btnEl.style.cursor = 'pointer';
                    btnEl.textContent = 'Record';
                }
            }, 250);
        };

        try { mr.start(); } catch (_) {
            stopCanvasRecorderPipeline();
            stopRecordingTimer();
            if (statusEl) statusEl.textContent = 'Recorder start failed.';
            try { mr.stop(); } catch (__) { }
        }
    }

    // -------------------------
    // DEBUG / LOGGING
    // -------------------------
    const LOG = {
        on: !!logs,
        tag: '[GVF]',
        lastTickMs: 0,
        tickEveryMs: 1000,
        lastToneMs: 0,
        toneEveryMs: 800
    };

    function log(...a) { if (!LOG.on) return; try { console.log(LOG.tag, ...a); } catch (_) { } }
    function logW(...a) { if (!LOG.on) return; try { console.warn(LOG.tag, ...a); } catch (_) { } }
    function logToggle(name, state, extra) { log(`${name}:`, state ? 'ON' : 'OFF', extra || ''); }

    // Debug Toggle Function
    function toggleDebug() {
        debug = !debug;
        logs = debug; // Sync logs with debug
        gmSet(K.DEBUG, debug);
        gmSet(K.LOGS, logs);

        LOG.on = logs;

        logToggle('Debug Mode', debug);
        logToggle('Console Logs', logs);

        // Update Auto-Dot immediately
        setAutoDotState(autoOn ? (debug ? 'idle' : 'off') : 'off');
        scheduleOverlayUpdate();

        // Short confirmation in console
        if (debug) {
            console.log('%c[GVF] Debug Mode ACTIVATED - Visual debug dots visible', 'color: #00ff00; font-weight: bold');
        } else {
            console.log('%c[GVF] Debug Mode DEACTIVATED - Visual debug dots hidden', 'color: #ff6666; font-weight: bold');
        }
    }

    // -------------------------
    // Global state
    // -------------------------
    let enabled = !!gmGet(K.enabled, true);
    let darkMoody = !!gmGet(K.moody, true);
    let tealOrange = !!gmGet(K.teal, false);
    let vibrantSat = !!gmGet(K.vib, false);
    let iconsShown = !!gmGet(K.icons, false);

    const isFirefoxBrowser = isFirefox();

    if (isFirefoxBrowser) {
        var sl = Number(gmGet(K.SL, 1.3));
        var sr = Number(gmGet(K.SR, -1.1));
        var bl = Number(gmGet(K.BL, 0.3));
        var wl = Number(gmGet(K.WL, 0.2));
        var dn = Number(gmGet(K.DN, 0.6));
        var profile = String(gmGet(K.PROF, 'off')).toLowerCase();
    } else {
        var sl = Number(gmGet(K.SL, 1.0));
        var sr = Number(gmGet(K.SR, 0.5));
        var bl = Number(gmGet(K.BL, -1.2));
        var wl = Number(gmGet(K.WL, 0.2));
        var dn = Number(gmGet(K.DN, -0.6));
        var profile = String(gmGet(K.PROF, 'user')).toLowerCase();
    }

    let hdr = Number(gmGet(K.HDR, 0.0));
    let edge = Number(gmGet(K.EDGE, 0.0));

    if (!['off', 'film', 'anime', 'gaming', 'eyecare', 'user'].includes(profile)) profile = 'off';

    let renderMode = String(gmGet(K.RENDER_MODE, 'svg')).toLowerCase();
    if (!['svg', 'gpu'].includes(renderMode)) renderMode = 'svg';

    let gradingHudShown = !!gmGet(K.G_HUD, false);
    let ioHudShown = !!gmGet(K.I_HUD, false);
    let scopesHudShown = !!gmGet(K.S_HUD, false);

    let u_contrast = Number(gmGet(K.U_CONTRAST, 0.0));
    let u_black = Number(gmGet(K.U_BLACK, 0.0));
    let u_white = Number(gmGet(K.U_WHITE, 0.0));
    let u_highlights = Number(gmGet(K.U_HIGHLIGHTS, 0.0));
    let u_shadows = Number(gmGet(K.U_SHADOWS, 0.0));
    let u_sat = Number(gmGet(K.U_SAT, 0.0));
    let u_vib = Number(gmGet(K.U_VIB, 0.0));
    let u_sharp = Number(gmGet(K.U_SHARP, 0.0));
    let u_gamma = Number(gmGet(K.U_GAMMA, 0.0));
    let u_grain = Number(gmGet(K.U_GRAIN, 0.0));
    let u_hue = Number(gmGet(K.U_HUE, 0.0));

    let u_r_gain = Number(gmGet(K.U_R_GAIN, 128));
    let u_g_gain = Number(gmGet(K.U_G_GAIN, 128));
    let u_b_gain = Number(gmGet(K.U_B_GAIN, 128));

    let autoOn = !!gmGet(K.AUTO_ON, true);
    let notify = !!gmGet(K.NOTIFY, true);
    let autoStrength = Number(gmGet(K.AUTO_STRENGTH, 0.65));
    autoStrength = clamp(autoStrength, 0, 1);
    let autoLockWB = !!gmGet(K.AUTO_LOCK_WB, true);

    // Color blindness filter
    let cbFilter = String(gmGet(K.CB_FILTER, 'none')).toLowerCase();
    if (!['none', 'protanopia', 'deuteranopia', 'tritanomaly'].includes(cbFilter)) cbFilter = 'none';

    // Initialize Profile Management
    loadUserProfiles();
    loadLutProfiles();

    const HK = { base: 'b', moody: 'd', teal: 'o', vib: 'v', icons: 'h' };

    function normSL() { return snap0(roundTo(clamp(Number(sl) || 0, -2, 2), 0.01), 0.005); }
    function normSR() { return snap0(roundTo(clamp(Number(sr) || 0, -2, 2), 0.01), 0.005); }
    function normBL() { return snap0(roundTo(clamp(Number(bl) || 0, -2, 2), 0.01), 0.005); }
    function normWL() { return snap0(roundTo(clamp(Number(wl) || 0, -2, 2), 0.01), 0.005); }
    function normDN() { return snap0(roundTo(clamp(Number(dn) || 0, -1.5, 1.5), 0.01), 0.005); }
    function normHDR() { return snap0(roundTo(clamp(Number(hdr) || 0, -1.0, 2.0), 0.01), 0.005); }
    function normEDGE() { return snap0(roundTo(clamp(Number(edge) || 0, 0, 1.0), 0.01), 0.005); }
    function normU(v) { return roundTo(clamp(Number(v) || 0, -10, 10), 0.1); }
    function uDelta(v) { return normU(v); }
    function normRGB(v) { return clamp(Math.round(Number(v) || 128), 0, 255); }
    function rgbGainToFactor(v) { return (normRGB(v) / 128); }

    function getSharpenA() { return Math.max(0, normSL()) * 1.0; }
    function getBlurSigma() { return Math.max(0, -normSL()) * 1.0; }
    function getRadius() { return Math.max(0.1, Math.abs(normSR())); }
    function blackToOffset(v) { return clamp(v, -2, 2) * 0.04; }
    function whiteToHiAdj(v) { return clamp(v, -2, 2) * 0.06; }
    function dnToDenoiseMix(v) { return clamp(v, 0, 1.5) * 0.5; }
    function dnToDenoiseSigma(v) { return clamp(v, 0, 1.5) * 0.8; }
    function dnToGrainAlpha(v) { return clamp(-v, 0, 1.5) * (0.20 / 1.5); }

    const PROF = {
        off: { name: 'Off', color: 'transparent' },
        film: { name: 'Movie', color: '#00b050' },
        anime: { name: 'Anime', color: '#1e6fff' },
        gaming: { name: 'Gaming', color: '#ff2a2a' },
        eyecare: { name: 'EyeCare', color: '#ffaa33' },
        user: { name: 'User', color: '#bfbfbf' }
    };

    const PROFILE_VIDEO_OUTLINE = false;

    // -------------------------
    // Color blindness filter matrices
    // -------------------------
    function getColorBlindnessMatrix(type, strength = 1.0) {

    const k = Math.max(0, Math.min(1, strength));

    // Helper: lerp a matrix towards identity by strength (so k=0 => no change)
    const I = matIdentity4x5();
    const mix = (M) => M.map((v, i) => I[i] + (v - I[i]) * k);

    if (type === 'protanopia') {

        const M = [
            0.9715365562, 0.0000451198, 0.0000221655, 0, 0.0054466531,
            0.4906507166, 0.4721896523, 0.0000465477, 0, 0.0045839181,
            0.4515750655, -0.3950199862, 0.7845041177, 0, 0.0772238079,
            0, 0, 0, 1, 0
        ];
        return mix(M);
    }

    if (type === 'deuteranopia') {

        const M = [
            1.1313510788, -0.2442083804, 0.0000487006, 0, 0.0570148323,
            0.0001729681, 0.9711718674, 0.0000761152, 0, 0.0057285327,
            -0.1474623881, 0.1563820548, 0.9401548784, 0, 0.0166599033,
            0, 0, 0, 1, 0
        ];
        return mix(M);
    }

    if (type === 'tritanomaly' || type === 'tritanopia') {

        const M = [
            0.92, -0.06, 0.00, 0, 0,
            0.04, 1.05, 0.00, 0, 0,
            0.00, -0.02, 1.02, 0, 0,
            0, 0, 0, 1, 0
        ];
        return mix(M);
    }


    return matIdentity4x5();
    }

    // -------------------------
    // 5x5 Color Matrix utils
    // -------------------------
    const LUMA = { r: 0.2126, g: 0.7152, b: 0.0722 };

    function matIdentity4x5() {
        return [
            1, 0, 0, 0, 0,
            0, 1, 0, 0, 0,
            0, 0, 1, 0, 0,
            0, 0, 0, 1, 0
        ];
    }

    function matMul4x5(a, b) {
        const out = new Array(20);
        for (let row = 0; row < 4; row++) {
            for (let col = 0; col < 4; col++) {
                let s = 0;
                for (let k = 0; k < 4; k++) s += a[row * 5 + k] * b[k * 5 + col];
                out[row * 5 + col] = s;
            }
            let o = a[row * 5 + 4];
            for (let k = 0; k < 4; k++) o += a[row * 5 + k] * b[k * 5 + 4];
            out[row * 5 + 4] = o;
        }
        return out;
    }

    function matBrightnessContrast(br, ct) {
        const g = br * ct;
        const off = br * 0.5 * (1 - ct);
        return [
            g, 0, 0, 0, off,
            0, g, 0, 0, off,
            0, 0, g, 0, off,
            0, 0, 0, 1, 0
        ];
    }

    function matSaturation(s) {
        const ir = (1 - s) * LUMA.r;
        const ig = (1 - s) * LUMA.g;
        const ib = (1 - s) * LUMA.b;
        return [
            ir + s, ig, ib, 0, 0,
            ir, ig + s, ib, 0, 0,
            ir, ig, ib + s, 0, 0,
            0, 0, 0, 1, 0
        ];
    }

    function matHueRotate(deg) {
        const rad = (deg * Math.PI) / 180;
        const cosA = Math.cos(rad);
        const sinA = Math.sin(rad);
        const lr = LUMA.r, lg = LUMA.g, lb = LUMA.b;

        const a00 = lr + cosA * (1 - lr) + sinA * (-lr);
        const a01 = lg + cosA * (-lg) + sinA * (-lg);
        const a02 = lb + cosA * (-lb) + sinA * (1 - lb);
        const a10 = lr + cosA * (-lr) + sinA * (0.143);
        const a11 = lg + cosA * (1 - lg) + sinA * (0.140);
        const a12 = lb + cosA * (-lb) + sinA * (-0.283);
        const a20 = lr + cosA * (-lr) + sinA * (-(1 - lr));
        const a21 = lg + cosA * (-lg) + sinA * (lg);
        const a22 = lb + cosA * (1 - lb) + sinA * (lb);

        return [
            a00, a01, a02, 0, 0,
            a10, a11, a12, 0, 0,
            a20, a21, a22, 0, 0,
            0, 0, 0, 1, 0
        ];
    }

    function matRGBGain(rGain, gGain, bGain) {
        return [
            rGain, 0, 0, 0, 0,
            0, gGain, 0, 0, 0,
            0, 0, bGain, 0, 0,
            0, 0, 0, 1, 0
        ];
    }

    function matToSvgValues(m) {
        return m.map(x => (Math.abs(x) < 1e-10 ? '0' : Number(x).toFixed(6))).join(' ');
    }

    let autoMatrixStr = matToSvgValues(matIdentity4x5());
    let _autoLastMatrixStr = autoMatrixStr;

    function updateAutoMatrixInSvg(valuesStr) {
        try {
            const svg = document.getElementById(SVG_ID);
            if (!svg) return;
            const nodes = svg.querySelectorAll('feColorMatrix[data-gvf-auto="1"]');
            if (!nodes || !nodes.length) return;
            nodes.forEach(n => {
                try {
                    if (n) {
                        n.setAttribute('values', valuesStr);
                    }
                } catch (_) { }
            });
        } catch (_) { }
    }

    // -------------------------
    // BRANCHLESS SHADER LOGIC
    // -------------------------
    function branchlessClamp(x, min, max) {
        return Math.min(max, Math.max(min, x));
    }

    function branchlessSign(x) {
        return (x > 0) - (x < 0);
    }

    function branchlessStep(edge, x) {
        return (x >= edge) | 0;
    }

    function branchlessMix(a, b, t) {
        return a + (b - a) * t;
    }

    function branchlessSmoothStep(edge0, edge1, x) {
        const t = Math.min(1, Math.max(0, (x - edge0) / (edge1 - edge0)));
        return t * t * (3 - 2 * t);
    }

    function branchlessGamma(x, gamma) {
        const g = branchlessClamp(gamma, 0.5, 2.0);
        if (g < 1.0) {
            return x * (1.0 + (1.0 - g) * (1.0 - x) * 0.5);
        } else {
            return x * (1.0 - (g - 1.0) * x * 0.3);
        }
    }

    function branchlessRGBGain(r, g, b, rGain, gGain, bGain) {
        return [
            r * rGain / 128.0,
            g * gGain / 128.0,
            b * bGain / 128.0
        ];
    }

    function branchlessSaturation(r, g, b, sat) {
        const luma = LUMA.r * r + LUMA.g * g + LUMA.b * b;
        return [
            branchlessMix(luma, r, sat),
            branchlessMix(luma, g, sat),
            branchlessMix(luma, b, sat)
        ];
    }

    function branchlessContrast(r, g, b, contrast) {
        const factor = (259.0 * (contrast * 255.0 + 255.0)) / (255.0 * (259.0 - contrast * 255.0));
        return [
            branchlessClamp(factor * (r - 128) + 128, 0, 255),
            branchlessClamp(factor * (g - 128) + 128, 0, 255),
            branchlessClamp(factor * (b - 128) + 128, 0, 255)
        ];
    }

    function branchlessHDRToneMap(r, g, b, exposure) {
        const luma = LUMA.r * r + LUMA.g * g + LUMA.b * b;
        const scale = (luma * exposure + 1.0) / (luma + 1.0);
        return [r * scale, g * scale, b * scale];
    }

    function branchlessSharpen(original, blurred, amount) {
        return [
            branchlessClamp(original[0] * (1.0 + amount) - blurred[0] * amount, 0, 255),
            branchlessClamp(original[1] * (1.0 + amount) - blurred[1] * amount, 0, 255),
            branchlessClamp(original[2] * (1.0 + amount) - blurred[2] * amount, 0, 255)
        ];
    }

    function branchlessMotionDetect(curr, prev) {
        let diff = 0;
        for (let i = 0; i < curr.length; i++) {
            diff += Math.abs(curr[i] - prev[i]);
        }
        return diff / curr.length;
    }

    function branchlessAdaptiveFps(motionScore, currentFps, minFps, maxFps) {
        const targetFps = minFps + motionScore * (maxFps - minFps);
        const alpha = 0.2;
        return currentFps * (1 - alpha) + targetFps * alpha;
    }

    // ===================== REAL WEBGL2 CANVAS PIPELINE =====================
    let webglPipeline = null;

    class WebGL2Pipeline {
        constructor() {
            this.canvas = null;
            this.gl = null;
            this.program = null;
            this.videoTexture = null;
            this.video = null;
            this.active = false;
            this.rafId = null;

            // Uniform locations
            this.uResolution = null;
            this.uVideoTex = null;
            this.uParams = null;
            this.uParams2 = null;
            this.uRGBGain = null;
            this.uHueRotate = null;
            this.uProfileMatrix = null;
            this.uAutoMatrix = null;

            // Attribute locations
            this.aPosition = null;
            this.aTexCoord = null;

            // Buffers
            this.vertexBuffer = null;
            this.texCoordBuffer = null;

            // Original video parent and styles
            this.originalParent = null;
            this.originalNextSibling = null;
            this.originalStyle = null;
            this.originalParentPosition = null;
            this.wrapper = null;
            this.firstFrameDrawn = false;

            // Parameter cache
            this.params = {
                contrast: 1.0,
                saturation: 1.0,
                brightness: 1.0,
                sharpen: 0.0,
                gamma: 1.0,
                grain: 0.0,
                hdr: 0.0,
                rGain: 1.0,
                gGain: 1.0,
                bGain: 1.0,
                hue: 0.0,
                cosHue: 1.0,
                sinHue: 0.0,
                vibrance: 1.0,
                black: 0.0,
                white: 1.0
            };

            // HDR startup smoothing / GPU protection
            this.hdrWarmupUntil = 0;
            this.hdrWarmupDurationMs = 2200;
            this.hdrStartDelayUntil = 0;
            this.hdrStartDelayMs = 650;
            this._boundWarmupHandler = null;
            this._boundVisibilityHandler = null;
        }

        markHdrWarmup(durationMs) {
            const now = nowMs();
            const ms = Math.max(700, Number(durationMs) || this.hdrWarmupDurationMs || 2200);
            this.hdrStartDelayUntil = now + Math.max(250, this.hdrStartDelayMs || 650);
            this.hdrWarmupUntil = this.hdrStartDelayUntil + ms;
        }

        getHdrWarmupFactor() {
            const hdrTarget = Math.max(0, Number(normHDR()) || 0);
            if (hdrTarget <= 0.0001) return 1.0;

            const now = nowMs();
            if (this.hdrStartDelayUntil > now) return 0.0;

            const left = this.hdrWarmupUntil - now;
            if (left <= 0) return 1.0;

            const dur = Math.max(700, this.hdrWarmupDurationMs || 2200);
            const progress = clamp(1 - (left / dur), 0, 1);

            // Very soft HDR ramp to avoid start spikes and sustained GPU overload.
            return clamp(progress * progress * 0.9, 0.0, 0.9);
        }

        bindHdrWarmupEvents(video) {
            if (!video) return;
            if (this._boundWarmupHandler) return;

            const warm = () => {
                if ((Number(normHDR()) || 0) > 0.0001) {
                    this.markHdrWarmup();
                }
            };

            this._boundWarmupHandler = warm;
            ['play', 'playing', 'seeking', 'seeked', 'loadeddata', 'canplay'].forEach((evt) => {
                try { video.addEventListener(evt, warm, true); } catch (_) { }
            });

            this._boundVisibilityHandler = () => {
                if (!document.hidden && video && !video.paused && (Number(normHDR()) || 0) > 0.0001) {
                    this.markHdrWarmup(900);
                }
            };
            try { document.addEventListener('visibilitychange', this._boundVisibilityHandler, true); } catch (_) { }
        }

        unbindHdrWarmupEvents(video) {
            if (video && this._boundWarmupHandler) {
                ['play', 'playing', 'seeking', 'seeked', 'loadeddata', 'canplay'].forEach((evt) => {
                    try { video.removeEventListener(evt, this._boundWarmupHandler, true); } catch (_) { }
                });
            }
            if (this._boundVisibilityHandler) {
                try { document.removeEventListener('visibilitychange', this._boundVisibilityHandler, true); } catch (_) { }
            }
            this._boundWarmupHandler = null;
            this._boundVisibilityHandler = null;
        }

        init() {
            try {
                // Create visible canvas that will replace the video
                this.canvas = document.createElement('canvas');
                this.canvas.id = WEBGL_CANVAS_ID;
                this.canvas.style.position = 'absolute';
                this.canvas.style.inset = '0';
                this.canvas.style.width = '100%';
                this.canvas.style.height = '100%';
                this.canvas.style.objectFit = 'contain';
                this.canvas.style.transform = 'none';
                this.canvas.style.display = 'block';
                this.canvas.style.pointerEvents = 'none';
                this.canvas.style.zIndex = '2147483646';
                this.canvas.style.opacity = '0';

                // Try WebGL2 first, fallback to WebGL1
                let gl = this.canvas.getContext('webgl2', {
                    alpha: false,
                    antialias: false,
                    preserveDrawingBuffer: false,
                    powerPreference: 'high-performance'
                });

                if (!gl) {
                    gl = this.canvas.getContext('webgl', {
                        alpha: false,
                        antialias: false,
                        preserveDrawingBuffer: false
                    }) || this.canvas.getContext('experimental-webgl', {
                        alpha: false,
                        antialias: false,
                        preserveDrawingBuffer: false
                    });
                }

                                this._isWebGL2 = (typeof WebGL2RenderingContext !== 'undefined' && gl instanceof WebGL2RenderingContext);

if (!gl) {
                    logW('WebGL not available');
                    return false;
                }

                this.gl = gl;

                if (!this.setupShaders()) {
                    return false;
                }

                this.setupBuffers();
                this.active = true;
                log('WebGL2 Canvas Pipeline initialized successfully');
                return true;
            } catch (e) {
                logW('WebGL init error:', e);
                return false;
            }
        }

        getVertexShader() {
            const src100 = `#version 100
                attribute vec2 aPosition;
                attribute vec2 aTexCoord;
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = vec4(aPosition, 0.0, 1.0);
                    vTexCoord = aTexCoord;
                }
            `;
            if (!this._isWebGL2) return src100;

            // WebGL2: upgrade GLSL100 -> GLSL300 ES
            return src100
                .replace('#version 100', '#version 300 es')
                .replace(/\battribute\b/g, 'in')
                .replace(/\bvarying\b/g, 'out')
                .replace(/\btexture2D\b/g, 'texture');
        }

        getFragmentShader() {
            const src100 = `#version 100
                precision highp float;
                varying vec2 vTexCoord;
                uniform sampler2D uVideoTex;
                uniform vec2 uResolution;

                uniform vec4 uParams;      // x:contrast, y:saturation, z:brightness, w:sharpen
                uniform vec4 uParams2;      // x:gamma, y:grain, z:vibrance, w:hdr
                uniform vec4 uRGBGain;      // x:rGain, y:gGain, z:bGain, w:unused
                uniform vec2 uHueRotate;    // x:cosHue, y:sinHue
                uniform mat4 uProfileMatrix;
                uniform mat4 uAutoMatrix;
                uniform float uEdge;

                const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

                float sampleLuma(vec2 uv) {
                    return dot(texture2D(uVideoTex, uv).rgb, LUMA);
                }

                float clampFast(float x, float minVal, float maxVal) {
                    return min(max(x, minVal), maxVal);
                }



                // --- HDR helpers (linear-light + ACES tonemapping) ---
                vec3 srgbToLinear(vec3 c) {
                    c = clamp(c, 0.0, 1.0);
                    vec3 low = c / 12.92;
                    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
                    vec3 t = step(vec3(0.04045), c);
                    return mix(low, high, t);
                }

                vec3 linearToSrgb(vec3 c) {
                    c = max(c, vec3(0.0));
                    vec3 low = c * 12.92;
                    vec3 high = 1.055 * pow(c, vec3(1.0/2.4)) - 0.055;
                    vec3 t = step(vec3(0.0031308), c);
                    return clamp(mix(low, high, t), 0.0, 1.0);
                }

                vec3 RRTAndODTFit(vec3 v) {
                    vec3 a = v * (v + 0.0245786) - 0.000090537;
                    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
                    return a / b;
                }

                vec3 tonemapACES(vec3 color) {
                    const mat3 ACESInputMat = mat3(
                      0.59719, 0.07600, 0.02840,
                      0.35458, 0.90834, 0.13383,
                      0.04823, 0.01566, 0.83777
                    );
                    const mat3 ACESOutputMat = mat3(
                        1.60475, -0.10208, -0.00327,
                        -0.53108, 1.10813, -0.07276,
                        -0.07367, -0.00605, 1.07602
                    );
                    color = ACESInputMat * color;
                    color = RRTAndODTFit(color);
                    color = ACESOutputMat * color;
                    return clamp(color, 0.0, 1.0);
                }
                vec3 applyHueRotate(vec3 color, float cosHue, float sinHue) {
                    float lr = LUMA.r, lg = LUMA.g, lb = LUMA.b;
                    float a00 = lr + cosHue*(1.0-lr) + sinHue*(-lr);
                    float a01 = lg + cosHue*(-lg) + sinHue*(-lg);
                    float a02 = lb + cosHue*(-lb) + sinHue*(1.0-lb);
                    float a10 = lr + cosHue*(-lr) + sinHue*(0.143);
                    float a11 = lg + cosHue*(1.0-lg) + sinHue*(0.140);
                    float a12 = lb + cosHue*(-lb) + sinHue*(-0.283);
                    float a20 = lr + cosHue*(-lr) + sinHue*(-(1.0-lr));
                    float a21 = lg + cosHue*(-lg) + sinHue*(lg);
                    float a22 = lb + cosHue*(1.0-lb) + sinHue*(lb);
                    return vec3(
                        a00*color.r + a01*color.g + a02*color.b,
                        a10*color.r + a11*color.g + a12*color.b,
                        a20*color.r + a21*color.g + a22*color.b
                    );
                }

                vec3 applyColorMatrix(vec3 color, mat4 m) {
                    return vec3(
                        m[0][0]*color.r + m[1][0]*color.g + m[2][0]*color.b + m[3][0],
                        m[0][1]*color.r + m[1][1]*color.g + m[2][1]*color.b + m[3][1],
                        m[0][2]*color.r + m[1][2]*color.g + m[2][2]*color.b + m[3][2]
                    );
                }

                void main() {
                    vec4 texColor = texture2D(uVideoTex, vTexCoord);
                    vec3 color = texColor.rgb;

                    // RGB Gain
                    color.r *= uRGBGain.x;
                    color.g *= uRGBGain.y;
                    color.b *= uRGBGain.z;

                    // Profile Matrix
                    color = applyColorMatrix(color, uProfileMatrix);

                    // Auto Matrix
                    color = applyColorMatrix(color, uAutoMatrix);

                    // Hue Rotate
                    color = applyHueRotate(color, uHueRotate.x, uHueRotate.y);

                    // Vibrance
                    float luma = dot(color, LUMA);
                    vec3 delta = color - luma;
                    color = luma + delta * uParams2.z;

                    // Saturation
                    color = luma + uParams.y * (color - luma);

                    // Contrast & Brightness
                    color = (color - 0.5) * uParams.x + 0.5;
                    color *= uParams.z;

                    // Gamma
                    float gInv = 1.0 / clampFast(uParams2.x, 0.5, 2.0);
                    color.r = pow(color.r, gInv);
                    color.g = pow(color.g, gInv);
                    color.b = pow(color.b, gInv);

                    // HDR (WebGL HDR-like: linear-light + exposure lift + ACES tonemapping)
                    float hdr = clampFast(uParams2.w, 0.0, 1.0);
                    if (hdr > 0.0001) {
                        // Keep the low-end response soft, but make exposure changes visibly affect the image.
                        vec3 lin = srgbToLinear(color);
                        float sceneLuma = dot(lin, LUMA);
                        float hdrCurve = pow(hdr, 1.85);
                        float exposureStops = hdrCurve * 1.10;
                        float exposure = pow(2.0, exposureStops);
                        float shadowMask = 1.0 - clampFast(sceneLuma * 2.25, 0.0, 1.0);
                        float shadowLift = 1.0 + hdrCurve * 0.85 * shadowMask;
                        lin *= exposure * shadowLift;
                        lin = tonemapACES(lin);
                        color = linearToSrgb(lin);
                        float postLift = 1.0 + hdrCurve * 0.14;
                        color = clamp(color * postLift, 0.0, 1.0);
                    }
                    // Grain
                    float noise = fract(sin(vTexCoord.x * 12.9898 + vTexCoord.y * 78.233) * 43758.5453);
                    noise = (noise - 0.5) * uParams2.y;
                    color += vec3(noise);

                    // Sharpen
                    if (uParams.w > 0.0) {
                        float lumaSharp = dot(color, LUMA);
                        vec3 sharpColor = color + (color - lumaSharp) * uParams.w;
                        color = vec3(
                            clampFast(sharpColor.r, 0.0, 1.0),
                            clampFast(sharpColor.g, 0.0, 1.0),
                            clampFast(sharpColor.b, 0.0, 1.0)
                        );
                    }

                    // Real edge detection: Sobel on source luma, then darken only true edges.
                    // Use a non-linear strength curve so tiny slider values stay subtle and controllable.
                    if (uEdge > 0.0001) {
                        float edgeStrength = pow(clamp(uEdge, 0.0, 1.0), 2.2);
                        vec2 px = vec2(1.0 / max(uResolution.x, 1.0), 1.0 / max(uResolution.y, 1.0));
                        float tl = sampleLuma(vTexCoord + px * vec2(-1.0, -1.0));
                        float  t = sampleLuma(vTexCoord + px * vec2( 0.0, -1.0));
                        float tr = sampleLuma(vTexCoord + px * vec2( 1.0, -1.0));
                        float  l = sampleLuma(vTexCoord + px * vec2(-1.0,  0.0));
                        float  r = sampleLuma(vTexCoord + px * vec2( 1.0,  0.0));
                        float bl = sampleLuma(vTexCoord + px * vec2(-1.0,  1.0));
                        float  b = sampleLuma(vTexCoord + px * vec2( 0.0,  1.0));
                        float br = sampleLuma(vTexCoord + px * vec2( 1.0,  1.0));

                        float gx = -tl + tr - 2.0*l + 2.0*r - bl + br;
                        float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
                        float edgeMag = length(vec2(gx, gy));
                        float edgeMask = smoothstep(0.18, 0.60, edgeMag) * edgeStrength;
                        float darken = 1.0 - edgeMask * 0.92;
                        color *= darken;
                    }

                    gl_FragColor = vec4(clampFast(color.r, 0.0, 1.0),
                                        clampFast(color.g, 0.0, 1.0),
                                        clampFast(color.b, 0.0, 1.0),
                                        texColor.a);
                }
            `;
            if (!this._isWebGL2) return src100;

            // WebGL2: upgrade GLSL100 -> GLSL300 ES
            // - varying -> in
            // - gl_FragColor -> outColor
            // - texture2D -> texture
            let s = src100
                .replace('#version 100', '#version 300 es')
                .replace(/\bvarying\b/g, 'in')
                .replace(/\btexture2D\b/g, 'texture')
                .replace(/\bgl_FragColor\b/g, 'outColor');

            // Ensure fragment output exists
            if (!/\bout\s+vec4\s+outColor\s*;/.test(s)) {
                s = s.replace(/precision\s+highp\s+float\s*;\s*/m, (m) => m + '\n                out vec4 outColor;\n');
            }
            return s;
        }

        setupShaders() {
            const gl = this.gl;

            const vertexShader = gl.createShader(gl.VERTEX_SHADER);
            gl.shaderSource(vertexShader, this.getVertexShader());
            gl.compileShader(vertexShader);

            if (!gl.getShaderParameter(vertexShader, gl.COMPILE_STATUS)) {
                logW('Vertex shader compile error:', gl.getShaderInfoLog(vertexShader));
                return false;
            }

            const fragmentShader = gl.createShader(gl.FRAGMENT_SHADER);
            gl.shaderSource(fragmentShader, this.getFragmentShader());
            gl.compileShader(fragmentShader);

            if (!gl.getShaderParameter(fragmentShader, gl.COMPILE_STATUS)) {
                logW('Fragment shader compile error:', gl.getShaderInfoLog(fragmentShader));
                return false;
            }

            this.program = gl.createProgram();
            gl.attachShader(this.program, vertexShader);
            gl.attachShader(this.program, fragmentShader);
            gl.linkProgram(this.program);

            if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
                logW('Program link error:', gl.getProgramInfoLog(this.program));
                return false;
            }

            gl.useProgram(this.program);

            this.uResolution = gl.getUniformLocation(this.program, 'uResolution');
            this.uVideoTex = gl.getUniformLocation(this.program, 'uVideoTex');
            this.uParams = gl.getUniformLocation(this.program, 'uParams');
            this.uParams2 = gl.getUniformLocation(this.program, 'uParams2');
            this.uRGBGain = gl.getUniformLocation(this.program, 'uRGBGain');
            this.uHueRotate = gl.getUniformLocation(this.program, 'uHueRotate');
            this.uProfileMatrix = gl.getUniformLocation(this.program, 'uProfileMatrix');
            this.uAutoMatrix = gl.getUniformLocation(this.program, 'uAutoMatrix');
            this.uEdge = gl.getUniformLocation(this.program, 'uEdge');

            this.aPosition = gl.getAttribLocation(this.program, 'aPosition');
            this.aTexCoord = gl.getAttribLocation(this.program, 'aTexCoord');

            gl.uniform1i(this.uVideoTex, 0);

            return true;
        }

        setupBuffers() {
            const gl = this.gl;

            const vertices = new Float32Array([
                -1.0, -1.0,
                 1.0, -1.0,
                -1.0,  1.0,
                 1.0,  1.0
            ]);

            this.vertexBuffer = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
            gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);

            // Use standard top-left texture coordinates and flip exactly once on upload.
            // The previous mixed approach caused the GPU image to look wrong again.
            const texCoords = new Float32Array([
                0.0, 0.0,
                1.0, 0.0,
                0.0, 1.0,
                1.0, 1.0
            ]);

            this.texCoordBuffer = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, this.texCoordBuffer);
            gl.bufferData(gl.ARRAY_BUFFER, texCoords, gl.STATIC_DRAW);
        }

        attachToVideo(video) {
            if (!this.active && !this.init()) {
                return false;
            }

            if (this.video && this.video !== video) {
                this.shutdown();
                if (!this.init()) return false;
            }

            this.video = video;
            this.firstFrameDrawn = false;
            this.bindHdrWarmupEvents(video);
            this.markHdrWarmup();

            this.originalParent = video.parentNode;
            this.originalNextSibling = video.nextSibling;
            this.originalStyle = video.style.cssText;

            if (!this.originalParent) return false;

            const parentStyle = window.getComputedStyle(this.originalParent);
            this.originalParentPosition = this.originalParent.style.position || '';
            if (parentStyle.position === 'static') {
                this.originalParent.style.position = 'relative';
            }

            if (!this.videoTexture) {
                const gl = this.gl;
                this.videoTexture = gl.createTexture();
                gl.bindTexture(gl.TEXTURE_2D, this.videoTexture);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
                gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
                gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            }

            if (!this.wrapper || !this.wrapper.isConnected) {
                const wrapper = document.createElement('div');
                wrapper.setAttribute(WEBGL_WRAPPER_ATTR, '1');
                wrapper.style.position = 'absolute';
                wrapper.style.inset = '0';
                wrapper.style.pointerEvents = 'none';
                wrapper.style.zIndex = '2147483646';
                wrapper.style.overflow = 'hidden';
                this.wrapper = wrapper;
            }

            if (this.canvas.parentNode !== this.wrapper) {
                this.wrapper.appendChild(this.canvas);
            }
            if (this.wrapper.parentNode !== this.originalParent) {
                this.originalParent.appendChild(this.wrapper);
            }

            this.canvas.width = video.videoWidth || 640;
            this.canvas.height = video.videoHeight || 360;
            this.canvas.style.opacity = '0';
            video.style.opacity = video.style.opacity || '';
            video.style.pointerEvents = video.style.pointerEvents || '';
            this.startRenderLoop();
            this.render();
            return true;
        }

        updateParams() {
            let contrast = 1.0 + (u_contrast * 0.04);
            let saturation = 1.0 + (u_sat * 0.05);
            let brightness = 1.0 + (u_black * -0.012) + (u_white * 0.012);

            let rGain = u_r_gain / 128.0;
            let gGain = u_g_gain / 128.0;
            let bGain = u_b_gain / 128.0;

            if (enabled) {
                contrast *= 1.05;
                saturation *= 1.21;
                brightness *= 1.02;
            }

            if (profile === 'film') {
                contrast *= 1.08;
                saturation *= 1.08;
            } else if (profile === 'anime') {
                contrast *= 1.10;
                saturation *= 1.16;
                brightness *= 1.03;
            } else if (profile === 'gaming') {
                contrast *= 1.12;
                saturation *= 1.06;
            } else if (profile === 'eyecare') {
                saturation *= 0.85;
                brightness *= 1.06;
            }

            if (darkMoody) {
                saturation *= 0.92;
                brightness *= 0.96;
            }

            if (vibrantSat) {
                saturation *= 1.35;
            }

            let sharpen = Math.max(0, normSL() * 0.3);
            let grain = Math.max(0, -normDN() * 0.2);
            let gamma = 1.0 + u_gamma * 0.025;
            let vibrance = 1.0 + u_vib * 0.02;
            let hdrVal = normHDR();
            let edgeVal = normEDGE();

            let hue = u_hue * 3;
            if (tealOrange) {
                hue += -5;
            }
            let hueRad = hue * Math.PI / 180;

            const hdrWarmupFactor = this.getHdrWarmupFactor();
            const activeVisibleVideos = this.getActiveRenderableVideoCount();
            let effectiveHdr = clamp(hdrVal, -1.0, 2.0);
            if (effectiveHdr > 0) {
                // Clamp HDR intensity in GPU mode so the shader does not run at the most expensive path.
                effectiveHdr = Math.min(effectiveHdr, activeVisibleVideos >= 2 ? 0.42 : 0.65);
                effectiveHdr *= hdrWarmupFactor;
            }

            this.params = {
                contrast: clamp(contrast, 0.5, 2.0),
                saturation: clamp(saturation, 0.0, 3.0),
                brightness: clamp(brightness, 0.5, 2.0),
                sharpen: clamp(sharpen, 0.0, 2.0),
                gamma: clamp(gamma, 0.5, 2.0),
                grain: clamp(grain, 0.0, 0.5),
                vibrance: clamp(vibrance, 0.0, 2.0),
                hdr: effectiveHdr,
                edge: clamp(edgeVal, 0.0, 1.0),
                rGain: clamp(rGain, 0.0, 2.0),
                gGain: clamp(gGain, 0.0, 2.0),
                bGain: clamp(bGain, 0.0, 2.0),
                hue: hue,
                cosHue: Math.cos(hueRad),
                sinHue: Math.sin(hueRad)
            };

            if (LOG.on && (performance.now() - LOG.lastTickMs) > 5000) {
                log('RGB Gain:', this.params.rGain.toFixed(2), this.params.gGain.toFixed(2), this.params.bGain.toFixed(2));
            }
        }

        getActiveRenderableVideoCount() {
            try {
                return Array.from(document.querySelectorAll('video')).filter(v => isVideoRenderable(v)).length;
            } catch (_) {
                return 1;
            }
        }

        getRenderThrottleMs() {
            const hdrActive = normHDR() > 0.0001;
            const activeVisibleVideos = this.getActiveRenderableVideoCount();
            const hdrWarmupFactor = this.getHdrWarmupFactor();
            let throttle = RENDER_THROTTLE;

            if (activeVisibleVideos >= 2) throttle = Math.max(throttle, 60);

            if (hdrActive) {
                throttle = Math.max(throttle, 95);
                if (activeVisibleVideos >= 2) throttle = Math.max(throttle, 145);
                if (hdrWarmupFactor < 0.999) throttle = Math.max(throttle, activeVisibleVideos >= 2 ? 185 : 150);
                if (this.hdrStartDelayUntil > nowMs()) throttle = Math.max(throttle, activeVisibleVideos >= 2 ? 210 : 170);
            }

            return throttle;
        }

        getRenderScale(srcWidth, srcHeight) {
            const hdrActive = normHDR() > 0.0001;
            const activeVisibleVideos = this.getActiveRenderableVideoCount();
            const pixelCount = Math.max(1, (srcWidth || 0) * (srcHeight || 0));
            const hdrWarmupFactor = this.getHdrWarmupFactor();

            let scale = 1.0;

            if (hdrActive) scale = Math.min(scale, 0.56);
            if (pixelCount >= (2560 * 1440)) scale = Math.min(scale, hdrActive ? 0.38 : 0.82);
            else if (pixelCount >= (1920 * 1080)) scale = Math.min(scale, hdrActive ? 0.46 : 0.9);
            else if (pixelCount >= (1280 * 720)) scale = Math.min(scale, hdrActive ? 0.54 : 0.95);

            if (activeVisibleVideos >= 2) scale = Math.min(scale, hdrActive ? 0.34 : 0.8);

            if (hdrActive) {
                if (this.hdrStartDelayUntil > nowMs()) {
                    scale = Math.min(scale, activeVisibleVideos >= 2 ? 0.28 : 0.34);
                } else if (hdrWarmupFactor < 0.999) {
                    const startupScale = activeVisibleVideos >= 2 ? 0.30 : 0.36;
                    scale = Math.min(scale, startupScale + (0.14 * hdrWarmupFactor));
                }
            }

            return clamp(scale, 0.28, 1.0);
        }

        shouldRenderNow() {
            if (!this.active || !this.video) return false;
            if (document.hidden) return false;
            if (!isVideoRenderable(this.video)) return false;
            return true;
        }

        render() {
            if (!this.active || !this.gl || !this.video) return;

            const gl = this.gl;
            const video = this.video;

            if (video.readyState < 2 || video.videoWidth === 0 || video.videoHeight === 0) {
                return;
            }

            try {
                const width = video.videoWidth;
                const height = video.videoHeight;
                const renderScale = this.getRenderScale(width, height);
                const targetWidth = Math.max(2, Math.round(width * renderScale));
                const targetHeight = Math.max(2, Math.round(height * renderScale));

                if (this.canvas.width !== targetWidth || this.canvas.height !== targetHeight) {
                    this.canvas.width = targetWidth;
                    this.canvas.height = targetHeight;
                    gl.viewport(0, 0, targetWidth, targetHeight);
                    if (this.uResolution) {
                        gl.uniform2f(this.uResolution, targetWidth, targetHeight);
                    }
                }

                this.updateParams();

                gl.useProgram(this.program);

                gl.uniform4f(this.uParams,
                    this.params.contrast,
                    this.params.saturation,
                    this.params.brightness,
                    this.params.sharpen
                );

                gl.uniform4f(this.uParams2,
                    this.params.gamma,
                    this.params.grain,
                    this.params.vibrance,
                    this.params.hdr
                );

                gl.uniform4f(this.uRGBGain,
                    this.params.rGain,
                    this.params.gGain,
                    this.params.bGain,
                    1.0
                );

                if (this.uEdge !== null) {
                    gl.uniform1f(this.uEdge, this.params.edge);
                }

                gl.uniform2f(this.uHueRotate,
                    this.params.cosHue,
                    this.params.sinHue
                );

                // Profile matrix (Identity for now)
                let profMatrix = new Float32Array(16);
                profMatrix[0] = 1; profMatrix[5] = 1; profMatrix[10] = 1; profMatrix[15] = 1;
                gl.uniformMatrix4fv(this.uProfileMatrix, false, profMatrix);

                // Auto matrix (Identity for now)
                let autoMatrix = new Float32Array(16);
                autoMatrix[0] = 1; autoMatrix[5] = 1; autoMatrix[10] = 1; autoMatrix[15] = 1;
                gl.uniformMatrix4fv(this.uAutoMatrix, false, autoMatrix);

                gl.activeTexture(gl.TEXTURE0);
                gl.bindTexture(gl.TEXTURE_2D, this.videoTexture);
                // HTML5 video frames need a single Y-flip on upload in the GPU path.
                // Setting this to false here inverted the live image again.
                gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
                gl.clearColor(0.0, 0.0, 0.0, 0.0);
                gl.clear(gl.COLOR_BUFFER_BIT);
                gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);

                gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
                gl.enableVertexAttribArray(this.aPosition);
                gl.vertexAttribPointer(this.aPosition, 2, gl.FLOAT, false, 0, 0);

                gl.bindBuffer(gl.ARRAY_BUFFER, this.texCoordBuffer);
                gl.enableVertexAttribArray(this.aTexCoord);
                gl.vertexAttribPointer(this.aTexCoord, 2, gl.FLOAT, false, 0, 0);

                gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
                if (!this.firstFrameDrawn) {
                    this.firstFrameDrawn = true;
                    this.canvas.style.opacity = '1';
                }

            } catch (e) {
                logW('WebGL render error:', e);
            }
        }

        startRenderLoop() {
            this.stopRenderLoop();

            const canRVFC = this.video && typeof this.video.requestVideoFrameCallback === 'function';

            if (canRVFC) {
                const onFrame = (now) => {
                    if (!this.active || !this.video) { this.rafId = null; return; }
                    const throttle = this.getRenderThrottleMs();
                    if (this.shouldRenderNow() && (now - lastRenderTime >= throttle)) {
                        lastRenderTime = now;
                        this.render();
                    }
                    this.rafId = this.video.requestVideoFrameCallback(onFrame);
                };
                this.rafId = this.video.requestVideoFrameCallback(onFrame);
                return;
            }

            const loop = (timestamp) => {
                if (!this.active || !this.video) { this.rafId = null; return; }
                const throttle = this.getRenderThrottleMs();
                if (this.shouldRenderNow() && (timestamp - lastRenderTime >= throttle)) {
                    lastRenderTime = timestamp;
                    this.render();
                }
                this.rafId = requestAnimationFrame(loop);
            };

            this.rafId = requestAnimationFrame(loop);
        }

        stopRenderLoop() {
            if (!this.rafId) return;
            try {
                // Could be an rAF id or a requestVideoFrameCallback id
                if (this.video && typeof this.video.cancelVideoFrameCallback === 'function') {
                    try { this.video.cancelVideoFrameCallback(this.rafId); } catch (_) { }
                }
                cancelAnimationFrame(this.rafId);
            } catch (_) { }
            this.rafId = null;
        }

        shutdown() {
            this.active = false;
            this.stopRenderLoop();
            this.unbindHdrWarmupEvents(this.video);

            if (this.video) {
                this.video.style.cssText = this.originalStyle || '';
            }
            if (this.originalParent) {
                this.originalParent.style.position = this.originalParentPosition || '';
            }
            if (this.canvas && this.canvas.parentNode) {
                this.canvas.parentNode.removeChild(this.canvas);
            }
            if (this.wrapper && this.wrapper.parentNode) {
                this.wrapper.parentNode.removeChild(this.wrapper);
            }

            if (this.gl && this.program) {
                const gl = this.gl;
                gl.deleteProgram(this.program);
                if (this.videoTexture) gl.deleteTexture(this.videoTexture);
                if (this.vertexBuffer) gl.deleteBuffer(this.vertexBuffer);
                if (this.texCoordBuffer) gl.deleteBuffer(this.texCoordBuffer);
            }

            this.canvas = null;
            this.gl = null;
            this.program = null;
            this.videoTexture = null;
            this.vertexBuffer = null;
            this.texCoordBuffer = null;
            this.wrapper = null;
            this.video = null;
            this.firstFrameDrawn = false;
        }
    }

    // GPU Mode Manager
    function activateWebGLMode() {
        const video = getGpuPrimaryVideo();
        if (!video) return;
        if (!webglPipeline) {
            webglPipeline = new WebGL2Pipeline();
        }
        if (webglPipeline.video && webglPipeline.video !== video) {
            webglPipeline.shutdown();
            webglPipeline = new WebGL2Pipeline();
        }
        document.querySelectorAll('video').forEach(v => { delete v.__gvf_webgl_attached; });
        video.__gvf_webgl_attached = true;
        webglPipeline.attachToVideo(video);
    }

    function deactivateWebGLMode() {
        if (webglPipeline) {
            webglPipeline.shutdown();
            webglPipeline = null;
        }
        document.querySelectorAll('video').forEach(video => {
            delete video.__gvf_webgl_attached;
        });
    }

    // -------------------------
    // GPU PIPELINE MODE - Fallback
    // -------------------------
    function getGpuFilterString() {
        if (webglPipeline && webglPipeline.active) {
            return 'none';
        }

        const filters = [];

        if (enabled) {
            filters.push('brightness(1.02)');
            filters.push('contrast(1.05)');
            filters.push('saturate(1.21)');
        }

        const slVal = normSL();
        if (slVal > 0) {
            const sharpAmount = Math.min(2, slVal * 0.3);
            filters.push(`contrast(${1 + sharpAmount})`);
        } else if (slVal < 0) {
            const blurAmount = Math.abs(slVal) * 0.5;
            filters.push(`blur(${blurAmount.toFixed(1)}px)`);
        }

        const blVal = normBL();
        if (blVal !== 0) {
            const blackAdj = 1 + (blVal * 0.03);
            filters.push(`brightness(${blackAdj.toFixed(2)})`);
        }

        const wlVal = normWL();
        if (wlVal !== 0) {
            const whiteAdj = 1 + (wlVal * 0.04);
            filters.push(`contrast(${whiteAdj.toFixed(2)})`);
        }

        const hdrVal = normHDR();
        if (hdrVal > 0) {
            const hdrContrast = 1 + (hdrVal * 0.15);
            const hdrSaturate = 1 + (hdrVal * 0.1);
            filters.push(`contrast(${hdrContrast.toFixed(2)})`);
            filters.push(`saturate(${hdrSaturate.toFixed(2)})`);
        } else if (hdrVal < 0) {
            const softContrast = 1 + (hdrVal * 0.1);
            filters.push(`contrast(${softContrast.toFixed(2)})`);
        }

        if (darkMoody) {
            filters.push('brightness(0.96)');
            filters.push('saturate(0.92)');
        }

        if (tealOrange) {
            filters.push('sepia(0.15)');
            filters.push('hue-rotate(-5deg)');
            filters.push('saturate(1.1)');
        }

        if (vibrantSat) {
            filters.push('saturate(1.35)');
        }

        if (profile === 'film') {
            filters.push('brightness(1.01) contrast(1.08) saturate(1.08)');
        } else if (profile === 'anime') {
            filters.push('brightness(1.03) contrast(1.10) saturate(1.16)');
            // Soft CSS filters for GPU mode (avoid artifacts)
            filters.push('contrast(1.15) brightness(0.98)');
        } else if (profile === 'gaming') {
            filters.push('brightness(1.01) contrast(1.12) saturate(1.06)');
        } else if (profile === 'eyecare') {
            filters.push('brightness(1.06) contrast(0.94) saturate(0.85) hue-rotate(-18deg) sepia(0.25)');
        }

        if (profile === 'user') {
            if (u_contrast !== 0) {
                const c = 1 + (u_contrast * 0.04);
                filters.push(`contrast(${c.toFixed(2)})`);
            }
            if (u_sat !== 0) {
                const sat = 1 + (u_sat * 0.05);
                filters.push(`saturate(${sat.toFixed(2)})`);
            }
            if (u_vib !== 0) {
                const vib = 1 + (u_vib * 0.02);
                filters.push(`saturate(${vib.toFixed(2)})`);
            }
            if (u_hue !== 0) {
                const hue = u_hue * 3;
                filters.push(`hue-rotate(${hue.toFixed(1)}deg)`);
            }
            if (u_black !== 0 || u_white !== 0) {
                const blk = u_black * 0.012;
                const wht = u_white * 0.012;
                const br = 1 + (-blk + wht) * 0.6;
                filters.push(`brightness(${br.toFixed(2)})`);
            }
            if (u_gamma !== 0) {
                const g = 1 + (u_gamma * 0.025);
                filters.push(`brightness(${g.toFixed(2)})`);
            }
        }

        // Color blindness filter for GPU mode
        if (cbFilter !== 'none') {
            if (cbFilter === 'protanopia') {
                filters.push('contrast(1.05) saturate(0.9)');
            } else if (cbFilter === 'deuteranopia') {
                filters.push('contrast(1.05) saturate(0.9) hue-rotate(5deg)');
            } else if (cbFilter === 'tritanomaly') {
                filters.push('contrast(1.02) saturate(0.95) hue-rotate(-5deg)');
            }
        }

        if (autoOn && AUTO.cur) {
            if (AUTO.cur.br !== 1.0) filters.push(`brightness(${AUTO.cur.br.toFixed(2)})`);
            if (AUTO.cur.ct !== 1.0) filters.push(`contrast(${AUTO.cur.ct.toFixed(2)})`);
        }

        const uniqueFilters = [...new Set(filters.filter(f => f && f.length > 0))];
        return uniqueFilters.length > 0 ? uniqueFilters.join(' ') : 'none';
    }

    // -------------------------
    // Auto Scene Match (UNCHANGED)
    // -------------------------
    let _autoLastStyleStamp = 0;
    const AUTO_LEVELS = [2, 4, 6, 8, 10];
    const ADAPTIVE_FPS = {
        MIN: 2,
        MAX: 10,
        current: 2,
        lastAdjust: 0,
        history: [],
        historySize: 5
    };
    const AUTO = {
        baseFps: 2,
        boostMs: 800,
        minBoostIdx: 3,
        minBoostEarlyMs: 700,
        minBoostEarlyIdx: 4,
        minArea: 64 * 64,
        canvasW: 96,
        canvasH: 54,
        running: false,
        tBoostUntil: 0,
        tBoostStart: 0,
        lastSig: null,
        cur: { br: 1.0, ct: 1.0, sat: 1.0, hue: 0.0 },
        tgt: { br: 1.0, ct: 1.0, sat: 1.0, hue: 0.0 },

        scoreEma: 0,
        scoreAlpha: 0.16,

        lastLuma: null,
        motionEma: 0,
        motionAlpha: 0.20,
        motionThresh: 0.015,
        motionMinFrames: 8,
        motionFrames: 0,

        lastAppliedMs: 0,

        statsEma: null,
        statsAlpha: 0.06,
        lastStatsMs: 0,

        blink: false,

        drmBlocked: false,
        blockUntilMs: 0,
        lastGoodMatrixStr: autoMatrixStr,

        lastFrameTime: 0,
        frameIntervals: [],
        maxFrameIntervals: 10
    };

    function calculateAdaptiveFps(changeScore) {
        ADAPTIVE_FPS.history.push(changeScore);
        if (ADAPTIVE_FPS.history.length > ADAPTIVE_FPS.historySize) {
            ADAPTIVE_FPS.history.shift();
        }

        const avgChange = ADAPTIVE_FPS.history.reduce((a, b) => a + b, 0) / ADAPTIVE_FPS.history.length;

        const t1 = Math.min(avgChange, 0.1) / 0.1;
        const t2 = Math.max(0, Math.min(avgChange - 0.1, 0.2)) / 0.2;
        const t3 = Math.max(0, Math.min(avgChange - 0.3, 0.7)) / 0.7;

        const targetFps =
            (avgChange < 0.1 ? 2 + t1 * 2 : 0) +
            (avgChange >= 0.1 && avgChange < 0.3 ? 4 + t2 * 3 : 0) +
            (avgChange >= 0.3 ? 7 + t3 * 3 : 0);

        const clamped = Math.max(ADAPTIVE_FPS.MIN, Math.min(targetFps, ADAPTIVE_FPS.MAX));
        const rounded = Math.round(clamped * 2) / 2;

        const fpsDiff = rounded - ADAPTIVE_FPS.current;
        const step = Math.max(-1, Math.min(fpsDiff, 1));
        ADAPTIVE_FPS.current += step;

        return ADAPTIVE_FPS.current;
    }

    const overlaysAutoDot = new WeakMap();
    let autoDotMode = 'off';

    function mkAutoDotOverlay() {
        const d = document.createElement('div');
        d.className = 'gvf-auto-dot';
        d.style.cssText = `
      position: fixed;
      width: 8px;
      height: 8px;
      border-radius: 999px;
      z-index: 2147483647;
      pointer-events: none;
      opacity: 0.95;
      display: none;
      transform: translateZ(0);
      box-shadow: 0 0 0 1px rgba(0,0,0,0.75), 0 0 10px rgba(0,255,0,0.18);
      background: #0b3d17;
    `;
        (document.body || document.documentElement).appendChild(d);
        return d;
    }

    function setAutoDotState(mode) {
        if (!debug) return;
        autoDotMode = mode || 'off';
        scheduleOverlayUpdate();
    }

    function applyAutoDotStyle(dotEl) {
        if (!dotEl) return;

        if (!autoOn || autoDotMode === 'off' || !debug) {
            dotEl.style.display = 'none';
            return;
        }

        dotEl.style.display = 'block';

        const t = nowMs();
        const staleMs = 10000;
        const isStale = (AUTO.lastAppliedMs > 0) && ((t - AUTO.lastAppliedMs) >= staleMs);
        if (isStale) {
            dotEl.style.background = '#ff2a2a';
            dotEl.style.boxShadow = '0 0 0 1px rgba(0,0,0,0.80), 0 0 16px rgba(255,42,42,0.55)';
            return;
        }

        if (autoDotMode === 'idle') {
            dotEl.style.background = '#0b3d17';
            dotEl.style.boxShadow = '0 0 0 1px rgba(0,0,0,0.75), 0 0 10px rgba(0,255,0,0.12)';
            return;
        }

        if (autoDotMode === 'workBright') {
            dotEl.style.background = '#38ff64';
            dotEl.style.boxShadow = '0 0 0 1px rgba(0,0,0,0.75), 0 0 14px rgba(56,255,100,0.45)';
            return;
        }

        if (autoDotMode === 'workDark') {
            dotEl.style.background = '#0f7a2b';
            dotEl.style.boxShadow = '0 0 0 1px rgba(0,0,0,0.75), 0 0 12px rgba(56,255,100,0.22)';
            return;
        }
    }

    function isActuallyVisible(v) {
        try {
            const cs = window.getComputedStyle(v);
            if (!cs) return true;
            if (cs.display === 'none') return false;
            if (cs.visibility === 'hidden') return false;
            if (Number(cs.opacity || '1') <= 0) return false;
            return true;
        } catch (_) { return true; }
    }

    function getVideoRect(v) {
        try {
            const r = v.getBoundingClientRect();
            if (r && r.width > 0 && r.height > 0) return r;
        } catch (_) { }
        const w = (v.offsetWidth || 0);
        const h = (v.offsetHeight || 0);
        return { top: 0, left: 0, right: w, bottom: h, width: w, height: h };
    }

    function isPlayableCandidate(v) {
        if (!v) return false;
        const hasDecoded = (v.videoWidth > 0 && v.videoHeight > 0);
        const hasTime = (Number.isFinite(v.currentTime) && v.currentTime > 0) || (Number.isFinite(v.duration) && v.duration > 0);
        const hasData = hasDecoded || hasTime || (v.readyState >= 1);
        if (!hasData) return false;
        if (v.ended) return false;
        if (!isActuallyVisible(v)) return false;
        const r = getVideoRect(v);
        if (!r || r.width < 80 || r.height < 60) return false;
        const area = r.width * r.height;
        if (area < AUTO.minArea) return false;
        return true;
    }

    function choosePrimaryVideo() {
        let best = null;
        let bestScore = 0;

        const vids = Array.from(document.querySelectorAll('video'));
        for (const v of vids) {
            try {
                if (!isPlayableCandidate(v)) continue;

                const r = getVideoRect(v);
                const area = r.width * r.height;

                const inView = !(r.bottom < 0 || r.right < 0 || r.top > (window.innerHeight || 0) || r.left > (window.innerWidth || 0));
                const playing = (!v.paused && !v.seeking);

                const score = area * (inView ? 1.25 : 0.90) * (playing ? 1.20 : 1.00);
                if (score > bestScore) { best = v; bestScore = score; }
            } catch (_) { }
        }
        return best;
    }

    function computeFrameStats(imgData) {
        const d = imgData.data;

        let sumR = 0, sumG = 0, sumB = 0;
        let sumY = 0, sumY2 = 0;
        let sumCh = 0;

        const stepPx = 2;
        const w = imgData.width;
        const h = imgData.height;
        const stride = w * 4;

        let count = 0;
        for (let y = 0; y < h; y += stepPx) {
            let idx = y * stride;
            for (let x = 0; x < w; x += stepPx) {
                const i = idx + x * 4;
                const r = d[i] / 255;
                const g = d[i + 1] / 255;
                const b = d[i + 2] / 255;

                const Y = LUMA.r * r + LUMA.g * g + LUMA.b * b;

                sumR += r; sumG += g; sumB += b;
                sumY += Y; sumY2 += Y * Y;

                const mx = Math.max(r, g, b);
                const mn = Math.min(r, g, b);
                sumCh += (mx - mn);

                count++;
            }
        }

        const inv = 1 / Math.max(1, count);
        const mR = sumR * inv;
        const mG = sumG * inv;
        const mB = sumB * inv;
        const mY = sumY * inv;
        const vY = Math.max(0, (sumY2 * inv) - (mY * mY));
        const sdY = Math.sqrt(vY);
        const mCh = sumCh * inv;

        return { mR, mG, mB, mY, sdY, mCh };
    }

    function computeMotionFromImage(imgData) {
        const d = imgData.data;
        const stepPx = 2;
        const w = imgData.width;
        const h = imgData.height;
        const stride = w * 4;

        const sw = Math.ceil(w / stepPx);
        const sh = Math.ceil(h / stepPx);
        const n = sw * sh;
        const cur = new Uint8Array(n);

        let k = 0;
        for (let y = 0; y < h; y += stepPx) {
            let idx = y * stride;
            for (let x = 0; x < w; x += stepPx) {
                const i = idx + x * 4;
                const r = d[i];
                const g = d[i + 1];
                const b = d[i + 2];
                const y8 = (r * 54 + g * 183 + b * 19) >> 8;
                cur[k++] = y8;
            }
        }

        const prev = AUTO.lastLuma;
        AUTO.lastLuma = cur;

        if (!prev || prev.length !== cur.length) return 1.0;

        return branchlessMotionDetect(cur, prev);
    }

    function detectCut(sig, lastSig) {
        if (!lastSig) return false;
        const dY = Math.abs(sig.mY - lastSig.mY);
        const dCh = Math.abs(sig.mCh - lastSig.mCh);
        const dRB = Math.abs((sig.mR - sig.mB) - (lastSig.mR - lastSig.mB));
        const dGB = Math.abs((sig.mG - sig.mB) - (lastSig.mG - lastSig.mB));

        const score = (dY * 1.1) + (dCh * 0.9) + (dRB * 0.7) + (dGB * 0.7);
        sig.__cutScore = score;
        return score > 0.14;
    }

    function wrapHueDeg(deg) {
        let d = deg;
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return d;
    }

    function approach(cur, tgt, a, dead=0.002) {
        const d = tgt - cur;
        if (Math.abs(d) < dead) return tgt;
        return cur + d * a;
    }

    function updateStatsAveraging(sig) {
        const a = clamp(AUTO.statsAlpha, 0.05, 0.95);
        if (!AUTO.statsEma) {
            AUTO.statsEma = { ...sig };
            return AUTO.statsEma;
        }
        const e = AUTO.statsEma;
        e.mR = e.mR * (1 - a) + sig.mR * a;
        e.mG = e.mG * (1 - a) + sig.mG * a;
        e.mB = e.mB * (1 - a) + sig.mB * a;
        e.mY = e.mY * (1 - a) + sig.mY * a;
        e.sdY = e.sdY * (1 - a) + sig.sdY * a;
        e.mCh = e.mCh * (1 - a) + sig.mCh * a;
        e.__cutScore = sig.__cutScore;
        return e;
    }

    function updateAutoTargetsFromStats(sig) {
        const s = clamp(autoStrength, 0, 1);

        const targetY = 0.50;
        const errY = clamp(targetY - sig.mY, -0.22, 0.22);
        const br = clamp(1.0 + errY * 0.85, 0.78, 1.22);

        const targetSd = 0.23;
        const errSd = clamp(targetSd - sig.sdY, -0.18, 0.18);
        const ct = clamp(1.0 + (-errSd) * 0.85, 0.82, 1.30);

        const targetCh = 0.12;
        const errCh = clamp(targetCh - sig.mCh, -0.20, 0.20);
        const sat = clamp(1.0 + (-errCh) * 0.90, 0.80, 1.45);

        let hue = 0.0;
        if (autoLockWB) {
            const rb = clamp(sig.mR - sig.mB, -0.18, 0.18);
            hue = clamp((-rb) * 28.0, -10.0, 10.0);
        }

        AUTO.tgt.br = clamp(1.0 + (br - 1.0) * s, 0.78, 1.22);
        AUTO.tgt.ct = clamp(1.0 + (ct - 1.0) * s, 0.82, 1.30);
        AUTO.tgt.sat = clamp(1.0 + (sat - 1.0) * s, 0.80, 1.45);
        AUTO.tgt.hue = clamp(0.0 + (hue - 0.0) * s, -12.0, 12.0);
    }

    function updateAutoSmoothing(isCut) {
        const a = isCut ? 0.10 : 0.025;
        AUTO.cur.br  = approach(AUTO.cur.br,  AUTO.tgt.br,  a, 0.003);
        AUTO.cur.ct  = approach(AUTO.cur.ct,  AUTO.tgt.ct,  a, 0.003);
        AUTO.cur.sat = approach(AUTO.cur.sat, AUTO.tgt.sat, a, 0.004);
        AUTO.cur.hue = approach(AUTO.cur.hue, AUTO.tgt.hue, a, 0.06);
        AUTO.cur.hue = wrapHueDeg(AUTO.cur.hue);
    }

    function buildAutoMatrixValues() {
        if (!autoOn) return matIdentity4x5();

        const br = clamp(AUTO.cur.br, 0.78, 1.22);
        const ct = clamp(AUTO.cur.ct, 0.82, 1.30);
        const sat = clamp(AUTO.cur.sat, 0.80, 1.45);
        const hue = clamp(AUTO.cur.hue, -12, 12);

        let m = matIdentity4x5();

        m = matMul4x5(matHueRotate(hue), m);
        m = matMul4x5(matSaturation(sat), m);
        m = matMul4x5(matBrightnessContrast(br, ct), m);

        return m;
    }

    function setAutoMatrixAndApply() {
        const m = buildAutoMatrixValues();
        const valuesStr = matToSvgValues(m);

        if (valuesStr === _autoLastMatrixStr) return;
        autoMatrixStr = valuesStr;
        _autoLastMatrixStr = valuesStr;

        AUTO.lastGoodMatrixStr = _autoLastMatrixStr;
        AUTO.lastAppliedMs = nowMs();

        const t = nowMs();
        if ((t - _autoLastStyleStamp) < 300) return;
        _autoLastStyleStamp = t;

        if (LOG.on && (t - LOG.lastToneMs) >= LOG.toneEveryMs) {
            LOG.lastToneMs = t;
            log('AutoMatrix updated:', autoMatrixStr);
        }

        updateAutoMatrixInSvg(autoMatrixStr);
        applyFilter({ skipSvgIfPossible: true });
    }

    function primeAutoOnVideoActivity() {
        try {
            const resetAuto = () => {
                if (!autoOn) return;
                AUTO.lastSig = null;
                AUTO.lastLuma = null;
                AUTO.motionEma = 0;
                AUTO.motionFrames = 0;
                AUTO.scoreEma = 0;
                AUTO.statsEma = null;
                AUTO.tBoostStart = nowMs();
                AUTO.tBoostUntil = AUTO.tBoostStart + AUTO.boostMs;
                AUTO.drmBlocked = false;
                AUTO.blockUntilMs = 0;
                AUTO.blink = false; // FIX: Reset blink
                ADAPTIVE_FPS.current = ADAPTIVE_FPS.MIN;
                ADAPTIVE_FPS.history = [];
            };

            document.addEventListener('play', resetAuto, true);
            document.addEventListener('playing', resetAuto, true);
            document.addEventListener('loadeddata', () => {
                if (!autoOn) return;
                AUTO.lastSig = null;
                AUTO.lastLuma = null;
                AUTO.motionEma = 0;
                AUTO.motionFrames = 0;
                AUTO.scoreEma = 0;
                AUTO.statsEma = null;
                AUTO.drmBlocked = false;
                AUTO.blockUntilMs = 0;
                AUTO.blink = false;
                ADAPTIVE_FPS.current = ADAPTIVE_FPS.MIN;
                ADAPTIVE_FPS.history = [];
            }, true);
        } catch (_) { }
    }

    function scoreToIdx(score) {
        if (score < 0.020) return 0;
        if (score < 0.045) return 1;
        if (score < 0.075) return 2;
        if (score < 0.115) return 3;
        return 4;
    }

    function pickAutoFps(nowT, cutScore) {
        const a = clamp(AUTO.scoreAlpha, 0.05, 0.95);
        AUTO.scoreEma = (AUTO.scoreEma * (1 - a)) + (cutScore * a);

        const adaptiveFps = calculateAdaptiveFps(cutScore);

        if (nowT < AUTO.tBoostUntil) {
            const age = nowT - (AUTO.tBoostStart || nowT);
            const early = age >= 0 && age < AUTO.minBoostEarlyMs;
            const boostFps = early ? 10 : 8;
            return Math.max(adaptiveFps, boostFps);
        }

        return adaptiveFps;
    }

    function ensureAutoLoop() {
        if (AUTO.running) return;
        AUTO.running = true;

        const c = document.createElement('canvas');
        c.width = AUTO.canvasW;
        c.height = AUTO.canvasH;

        let ctx = null;
        try { ctx = c.getContext('2d', { willReadFrequently: true }); }
        catch (_) { try { ctx = c.getContext('2d'); } catch (__) { } }

        const scheduleNext = (fps) => {
            const ms = Math.max(80, Math.round(1000 / Math.max(1, fps)));
            setTimeout(loop, ms);
        };

        const loop = () => {
            if (!AUTO.running) return;

            if (!autoOn) {
                AUTO.lastSig = null;
                AUTO.lastLuma = null;
                AUTO.scoreEma = 0;
                AUTO.motionEma = 0;
                AUTO.motionFrames = 0;
                AUTO.statsEma = null;
                AUTO.drmBlocked = false;
                AUTO.blockUntilMs = 0;
                AUTO.lastAppliedMs = 0;
                AUTO.blink = false;
                setAutoDotState('off');
                scheduleNext(ADAPTIVE_FPS.MIN);
                return;
            }

            const tNow = nowMs();
            if (AUTO.drmBlocked && tNow < (AUTO.blockUntilMs || 0)) {
                setAutoDotState('idle');
                scheduleNext(ADAPTIVE_FPS.MIN);
                return;
            }

            const v = choosePrimaryVideo();
            if (!v || !ctx) {
                AUTO.lastSig = null;
                AUTO.lastLuma = null;
                AUTO.motionEma = 0;
                AUTO.motionFrames = 0;
                AUTO.statsEma = null;
                setAutoDotState('idle');

                const t = nowMs();
                if (LOG.on && (t - LOG.lastTickMs) >= LOG.tickEveryMs) {
                    LOG.lastTickMs = t;
                    log('Auto(A) running: no playable video found.');
                }
                scheduleNext(ADAPTIVE_FPS.MIN);
                return;
            }

            if (v.paused || v.seeking) {
                AUTO.motionFrames = 0;
                setAutoDotState('idle');
                scheduleNext(ADAPTIVE_FPS.MIN);
                return;
            }

            try {
                ctx.drawImage(v, 0, 0, AUTO.canvasW, AUTO.canvasH);
                const img = ctx.getImageData(0, 0, AUTO.canvasW, AUTO.canvasH);

                if (AUTO.drmBlocked) {
                    AUTO.drmBlocked = false;
                    AUTO.blockUntilMs = 0;
                }

                const motion = computeMotionFromImage(img);
                const ma = clamp(AUTO.motionAlpha, 0.05, 0.95);
                AUTO.motionEma = (AUTO.motionEma * (1 - ma)) + (motion * ma);

                const hasMotionNow = (AUTO.motionEma >= AUTO.motionThresh);
                AUTO.motionFrames = hasMotionNow ? (AUTO.motionFrames + 1) : 0;

                const sigRaw = computeFrameStats(img);
                const isCut = detectCut(sigRaw, AUTO.lastSig);
                AUTO.lastSig = sigRaw;

                const sig = updateStatsAveraging(sigRaw);

                if (isCut) {
                    AUTO.tBoostStart = nowMs();
                    AUTO.tBoostUntil = AUTO.tBoostStart + AUTO.boostMs;
                }

                const t = nowMs();
                const rawScore = clamp(sigRaw.__cutScore || 0, 0, 1);

                const hasMotion = (AUTO.motionFrames >= AUTO.motionMinFrames);
                const allowUpdate = isCut || hasMotion;

                let fps = ADAPTIVE_FPS.current;
                if (allowUpdate) {
                    fps = pickAutoFps(t, rawScore);
                }

                if (allowUpdate) {
                    updateAutoTargetsFromStats(sig);
                    updateAutoSmoothing(isCut);
                    setAutoMatrixAndApply();

                    AUTO.blink = !AUTO.blink;
                    setAutoDotState(AUTO.blink ? 'workBright' : 'workDark');
                } else {
                    setAutoDotState('idle');
                }

                if (LOG.on && (t - LOG.lastTickMs) >= LOG.tickEveryMs) {
                    LOG.lastTickMs = t;
                    log(
                        `Auto(A) tick @${fps.toFixed(1)}fps`,
                        `adaptive=${ADAPTIVE_FPS.current.toFixed(1)}fps`,
                        `update=${allowUpdate ? 'YES' : 'NO'}`,
                        `motion=${motion.toFixed(4)} ema=${AUTO.motionEma.toFixed(4)} thr=${AUTO.motionThresh.toFixed(3)} frames=${AUTO.motionFrames}/${AUTO.motionMinFrames}`,
                        `raw=${rawScore.toFixed(3)} emaScore=${AUTO.scoreEma.toFixed(3)}`,
                        `avgY=${(sig.mY || 0).toFixed(3)} avgSd=${(sig.sdY || 0).toFixed(3)} avgCh=${(sig.mCh || 0).toFixed(3)}`
                    );
                }

                scheduleNext(fps);
            } catch (e) {
                AUTO.drmBlocked = true;

                const t = nowMs();
                const nextWait = (AUTO.blockUntilMs && (t - AUTO.blockUntilMs) < 2000) ? 5000 : 2000;
                AUTO.blockUntilMs = t + nextWait;

                AUTO.lastSig = null;
                AUTO.lastLuma = null;
                AUTO.motionEma = 0;
                AUTO.motionFrames = 0;
                AUTO.statsEma = null;
                AUTO.blink = false; // FIX: Reset blink

                const keep = AUTO.lastGoodMatrixStr || _autoLastMatrixStr || autoMatrixStr || matToSvgValues(matIdentity4x5());
                autoMatrixStr = keep;
                _autoLastMatrixStr = keep;
                updateAutoMatrixInSvg(keep);

                setAutoDotState('idle');

                if (LOG.on && (t - LOG.lastTickMs) >= LOG.tickEveryMs) {
                    LOG.lastTickMs = t;
                    logW('Auto(A) DRM/cross-origin: pixels blocked. Using last AutoMatrix (static) + backoff.', e && e.message ? e.message : e);
                }

                scheduleNext(ADAPTIVE_FPS.MIN);
            }
        };

        log(`Auto analyzer loop created with ADAPTIVE FPS (2-10fps). levels=${AUTO_LEVELS.join(',')} canvas=${AUTO.canvasW}x${AUTO.canvasH} motionThresh=${AUTO.motionThresh}`);
        scheduleNext(ADAPTIVE_FPS.MIN);
    }

    function setAutoOn(on, opts = {}) {
        const silent = !!opts.silent;
        const next = !!on;

        if (next === autoOn && AUTO.running) {
            if (!silent) {
                scheduleOverlayUpdate();
                setAutoDotState(next ? 'idle' : 'off');
            }
            return;
        }

        autoOn = next;
        if (!_inSync) gmSet(K.AUTO_ON, autoOn);

        logToggle('Auto Scene Match (Ctrl+Alt+A)', autoOn, `(strength=${autoStrength.toFixed(2)}, lockWB=${autoLockWB ? 'yes' : 'no'}, adaptive FPS 2-10)`);
        if (!silent) showToggleNotification('Auto-Scene-Match', autoOn);

        if (!autoOn) {
            AUTO.lastSig = null;
            AUTO.lastLuma = null;
            AUTO.motionEma = 0;
            AUTO.motionFrames = 0;
            AUTO.scoreEma = 0;
            AUTO.statsEma = null;
            AUTO.tBoostUntil = 0;
            AUTO.tBoostStart = 0;
            AUTO.tgt = { br: 1.0, ct: 1.0, sat: 1.0, hue: 0.0 };
            AUTO.drmBlocked = false;
            AUTO.blockUntilMs = 0;
            AUTO.lastAppliedMs = 0;
            AUTO.blink = false;

            autoMatrixStr = matToSvgValues(matIdentity4x5());
            _autoLastMatrixStr = autoMatrixStr;
            AUTO.lastGoodMatrixStr = autoMatrixStr;
            updateAutoMatrixInSvg(autoMatrixStr);

            setAutoDotState('off');

            if (!silent) {
                applyFilter({ skipSvgIfPossible: true });
                scheduleOverlayUpdate();
            }
            return;
        }

        AUTO.lastAppliedMs = 0;
        ADAPTIVE_FPS.current = ADAPTIVE_FPS.MIN;
        ADAPTIVE_FPS.history = [];

        setAutoDotState('idle');
        ensureAutoLoop();

        if (!silent) {
            applyFilter({ skipSvgIfPossible: false });
            setAutoMatrixAndApply();
            scheduleOverlayUpdate();
        } else {
            autoMatrixStr = matToSvgValues(buildAutoMatrixValues());
            _autoLastMatrixStr = autoMatrixStr;
            AUTO.lastGoodMatrixStr = autoMatrixStr;
            updateAutoMatrixInSvg(autoMatrixStr);
        }
    }

    // -------------------------
    // Config Menu (User Profile Management)
    // -------------------------
    let configMenuVisible = false;

    function createConfigMenu() {
        // Remove existing menu, if any
        let existingMenu = document.getElementById(CONFIG_MENU_ID);
        if (existingMenu) {
            existingMenu.remove();
        }

        const menu = document.createElement('div');
        menu.id = CONFIG_MENU_ID;
        menu.style.cssText = `
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            width: 820px;
            max-width: 98vw;
            max-height: 88vh;
            background: rgba(20, 20, 20, 0.98);
            backdrop-filter: blur(10px);
            border: 2px solid #2a6fdb;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0, 0, 0, 0.8), 0 0 0 1px rgba(255,255,255,0.1) inset;
            color: #eaeaea;
            font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
            z-index: 2147483647;
            display: none;
            flex-direction: column;
            padding: 20px;
            user-select: none;
            pointer-events: auto;
        `;
        stopEventsOn(menu);

        // Header
        const header = document.createElement('div');
        header.style.cssText = `
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 2px solid #2a6fdb;
        `;

        const title = document.createElement('div');
        title.style.cssText = `
            font-size: 20px;
            font-weight: 900;
            color: #fff;
            text-shadow: 0 0 10px #2a6fdb;
        `;
        title.textContent = '👤 User Profile Manager';

        const closeBtn = document.createElement('button');
        closeBtn.textContent = '✕';
        closeBtn.style.cssText = `
            background: rgba(255, 255, 255, 0.1);
            border: none;
            color: #fff;
            font-size: 20px;
            cursor: pointer;
            width: 36px;
            height: 36px;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s;
            border: 1px solid rgba(255,255,255,0.2);
        `;
        closeBtn.addEventListener('mouseenter', () => {
            closeBtn.style.background = 'rgba(255, 68, 68, 0.3)';
            closeBtn.style.borderColor = '#ff4444';
        });
        closeBtn.addEventListener('mouseleave', () => {
            closeBtn.style.background = 'rgba(255, 255, 255, 0.1)';
            closeBtn.style.borderColor = 'rgba(255,255,255,0.2)';
        });
        closeBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            toggleConfigMenu();
        });

        header.appendChild(title);
        header.appendChild(closeBtn);
        menu.appendChild(header);

        makeFloatingManagerDraggable(menu, header, K.USER_PROFILE_MANAGER_POS);

        // Show active profile
        const activeInfo = document.createElement('div');
        activeInfo.id = 'gvf-active-profile-info';
        activeInfo.style.cssText = `
            background: rgba(42, 111, 219, 0.2);
            border: 1px solid #2a6fdb;
            border-radius: 8px;
            padding: 10px;
            margin-bottom: 15px;
            font-size: 13px;
            display: flex;
            align-items: center;
            gap: 8px;
        `;

        function setActiveProfileInfo(el, profileName) {
            if (!el) return;
            while (el.firstChild) el.removeChild(el.firstChild);

            const name = profileName || 'Default';
            el.append('🔵 Active profile: ');

            const strong = document.createElement('strong');
            strong.textContent = name;
            el.appendChild(strong);
        }

        setActiveProfileInfo(activeInfo, activeUserProfile?.name);
        menu.appendChild(activeInfo);

        // Profile List Container
        const listContainer = document.createElement('div');
        listContainer.style.cssText = `
            flex: 1;
            overflow-y: auto;
            margin-bottom: 20px;
            max-height: 300px;
            background: rgba(0,0,0,0.3);
            border-radius: 8px;
            padding: 5px;
        `;

        const profileList = document.createElement('div');
        profileList.id = 'gvf-profile-list';
        profileList.style.cssText = `
            display: flex;
            flex-direction: column;
            gap: 8px;
        `;

        listContainer.appendChild(profileList);
        menu.appendChild(listContainer);

        // Input for new profile
        const inputContainer = document.createElement('div');
        inputContainer.style.cssText = `
            display: flex;
            gap: 8px;
            margin-bottom: 16px;
        `;

        const newProfileInput = document.createElement('input');
        newProfileInput.type = 'text';
        newProfileInput.placeholder = 'New profile name...';
        newProfileInput.id = 'gvf-new-profile-name';
        newProfileInput.style.cssText = `
            flex: 1;
            background: rgba(0, 0, 0, 0.5);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 8px;
            padding: 10px 12px;
            color: #fff;
            font-size: 14px;
            outline: none;
            transition: border 0.2s;
        `;
        newProfileInput.addEventListener('focus', () => {
            newProfileInput.style.borderColor = '#2a6fdb';
        });
        newProfileInput.addEventListener('blur', () => {
            newProfileInput.style.borderColor = 'rgba(255, 255, 255, 0.2)';
        });

        const addBtn = document.createElement('button');
        addBtn.textContent = '+ Add';
        addBtn.style.cssText = `
            background: #2a6fdb;
            border: none;
            color: #fff;
            padding: 10px 16px;
            border-radius: 8px;
            font-size: 14px;
            font-weight: 900;
            cursor: pointer;
            transition: all 0.2s;
            border: 1px solid transparent;
        `;
        addBtn.addEventListener('mouseenter', () => {
            addBtn.style.background = '#3a7feb';
            addBtn.style.transform = 'scale(1.02)';
        });
        addBtn.addEventListener('mouseleave', () => {
            addBtn.style.background = '#2a6fdb';
            addBtn.style.transform = 'scale(1)';
        });
        addBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            const name = newProfileInput.value.trim();
            if (name) {
                createNewUserProfile(name);
                updateProfileList();
                newProfileInput.value = '';

                // Update active info
                const activeInfo = document.getElementById('gvf-active-profile-info');
                if (activeInfo) {
                    setActiveProfileInfo(activeInfo, activeUserProfile?.name);
                }
            } else {
                alert('Please enter a name!');
            }
        });

        inputContainer.appendChild(newProfileInput);
        inputContainer.appendChild(addBtn);
        menu.appendChild(inputContainer);

        // Buttons
        const buttonContainer = document.createElement('div');
        buttonContainer.style.cssText = `
            display: flex;
            gap: 8px;
            justify-content: flex-end;
            margin-top: 10px;
        `;

        const saveCurrentBtn = document.createElement('button');
        saveCurrentBtn.textContent = '💾 Save current profile';
        saveCurrentBtn.style.cssText = `
            background: rgba(255, 255, 255, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.2);
            color: #fff;
            padding: 10px 16px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 900;
            cursor: pointer;
            transition: all 0.2s;
            flex: 1;
        `;
        saveCurrentBtn.addEventListener('mouseenter', () => {
            saveCurrentBtn.style.background = 'rgba(255, 255, 255, 0.2)';
            saveCurrentBtn.style.borderColor = '#2a6fdb';
        });
        saveCurrentBtn.addEventListener('mouseleave', () => {
            saveCurrentBtn.style.background = 'rgba(255, 255, 255, 0.1)';
            saveCurrentBtn.style.borderColor = 'rgba(255,255,255,0.2)';
        });
        saveCurrentBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (!activeUserProfile && Array.isArray(userProfiles) && userProfiles.length) {
                activeUserProfile = userProfiles[0];
            }
            updateCurrentProfileSettings(true);
            saveUserProfiles();
            updateProfileList();
            showScreenNotification('', {
                title: `Profile "${String(activeUserProfile?.name || 'Default')}" saved`,
                detail: 'User Profile Manager',
                detailColor: '#4cff6a'
            });

            // Brief feedback
            saveCurrentBtn.textContent = '✓ Saved!';
            setTimeout(() => {
                saveCurrentBtn.textContent = '💾 Save current profile';
            }, 1000);
        });


        buttonContainer.appendChild(saveCurrentBtn);

        // Import / Export (profiles as ZIP of per-profile JSON)
        const importExportRow = document.createElement('div');
        importExportRow.style.cssText = `
            display: flex;
            gap: 8px;
            justify-content: space-between;
            margin-top: 8px;
        `;

        const exportProfilesBtn = document.createElement('button');
        exportProfilesBtn.textContent = '📦 Export profiles (ZIP)';
        exportProfilesBtn.style.cssText = `
            background: rgba(42, 111, 219, 0.25);
            border: 1px solid rgba(42, 111, 219, 0.6);
            color: #fff;
            padding: 10px 12px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 900;
            cursor: pointer;
            transition: all 0.2s;
            flex: 1;
        `;

        const importProfilesBtn = document.createElement('button');
        importProfilesBtn.textContent = '📥 Import profiles (ZIP/JSON)';
        importProfilesBtn.style.cssText = `
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid rgba(255, 255, 255, 0.2);
            color: #fff;
            padding: 10px 12px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 900;
            cursor: pointer;
            transition: all 0.2s;
            flex: 1;
        `;

        const profileFileInput = document.createElement('input');
        profileFileInput.type = 'file';
        profileFileInput.accept = '.zip,application/zip,.json,application/json';
        profileFileInput.style.display = 'none';
        stopEventsOn(profileFileInput);

        const setTmpStatus = (msg) => {
            // Reuse existing "activeInfo" line as a small status area without adding new UI noise.
            const el = document.getElementById('gvf-active-profile-info');
            if (!el) return;
            el.title = String(msg || '');
        };

        exportProfilesBtn.addEventListener('mouseenter', () => {
            exportProfilesBtn.style.background = 'rgba(42, 111, 219, 0.35)';
            exportProfilesBtn.style.borderColor = '#2a6fdb';
        });
        exportProfilesBtn.addEventListener('mouseleave', () => {
            exportProfilesBtn.style.background = 'rgba(42, 111, 219, 0.25)';
            exportProfilesBtn.style.borderColor = 'rgba(42, 111, 219, 0.6)';
        });

        importProfilesBtn.addEventListener('mouseenter', () => {
            importProfilesBtn.style.background = 'rgba(255, 255, 255, 0.14)';
            importProfilesBtn.style.borderColor = '#2a6fdb';
        });
        importProfilesBtn.addEventListener('mouseleave', () => {
            importProfilesBtn.style.background = 'rgba(255, 255, 255, 0.08)';
            importProfilesBtn.style.borderColor = 'rgba(255, 255, 255, 0.2)';
        });

        exportProfilesBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();

            try {
                const zipBlob = exportAllUserProfilesAsZip();
                if (!zipBlob) {
                    logW('No profiles to export.');
                    setTmpStatus('No profiles to export.');
                    return;
                }

                const zipName = _zipName('gvf_user_profiles');
                downloadBlob(zipBlob, zipName);

                log('Exported profiles ZIP:', zipName);
                setTmpStatus('Profiles exported as ZIP.');
            } catch (err) {
                logW('Profile export failed:', err);
                setTmpStatus('Profile export failed.');
            }
        });

        importProfilesBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            try { profileFileInput.value = ''; } catch (_) { }
            profileFileInput.click();
        });

        profileFileInput.addEventListener('change', async () => {
            const f = profileFileInput.files && profileFileInput.files[0];
            if (!f) return;
            setTmpStatus('Importing profiles...');
            const ok = await importProfilesFromZipOrJsonFile(f, null);
            setTmpStatus(ok ? 'Profiles imported.' : 'Profile import failed.');
            try { profileFileInput.value = ''; } catch (_) { }
        });

        importExportRow.appendChild(exportProfilesBtn);
        importExportRow.appendChild(importProfilesBtn);

        menu.appendChild(importExportRow);
        menu.appendChild(profileFileInput);
        menu.appendChild(buttonContainer);

        // Add to body (or fullscreen element if active)
        if (document.body) {
            const _fsEl = getFsEl();
            (_fsEl || document.body).appendChild(menu);
            applyManagerPosition(menu, K.USER_PROFILE_MANAGER_POS);
            log('Config menu created and added to the body');
        }

        return menu;
    }

    // -------------------------
    // Shared Slideshow Lightbox
    // entries: Array<{ bigCanvas, bigReady, label }>
    // startIndex: which entry to show first
    // accentColor: CSS color string for title glow
    // -------------------------
    function openSlideshow(entries, startIndex, accentColor) {
        if (!entries || !entries.length) return;
        accentColor = accentColor || '#4a9eff';
        let idx = Math.max(0, Math.min(startIndex || 0, entries.length - 1));

        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position:fixed;inset:0;z-index:2147483647;
            background:rgba(0,0,0,0.88);backdrop-filter:blur(8px);
            display:flex;flex-direction:column;align-items:center;justify-content:center;
            gap:12px;user-select:none;
        `;

        // Title
        const lbTitle = document.createElement('div');
        lbTitle.style.cssText = `color:#fff;font-size:17px;font-weight:900;
            text-shadow:0 0 14px ${accentColor};pointer-events:none;text-align:center;
            max-width:80vw;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;`;

        // Counter  e.g. "3 / 7"
        const lbCounter = document.createElement('div');
        lbCounter.style.cssText = `color:#aaa;font-size:12px;font-weight:700;
            pointer-events:none;letter-spacing:0.05em;`;

        // Image area
        const imgWrap = document.createElement('div');
        imgWrap.style.cssText = `position:relative;display:flex;align-items:center;
            justify-content:center;max-width:90vw;max-height:65vh;`;

        // Canvas slot — we swap canvas references into this wrapper
        const canvasSlot = document.createElement('div');
        canvasSlot.style.cssText = `display:flex;align-items:center;justify-content:center;`;
        imgWrap.appendChild(canvasSlot);

        // Prev / Next nav buttons
        const mkNavBtn = (label) => {
            const b = document.createElement('button');
            b.type = 'button'; b.textContent = label;
            b.style.cssText = `
                position:absolute;top:50%;transform:translateY(-50%);
                background:rgba(0,0,0,0.55);border:1px solid rgba(255,255,255,0.25);
                color:#fff;font-size:22px;font-weight:900;
                width:44px;height:44px;border-radius:50%;
                cursor:pointer;display:flex;align-items:center;justify-content:center;
                transition:background 0.15s;z-index:2;
            `;
            b.addEventListener('mouseenter', () => { b.style.background = `rgba(0,0,0,0.82)`; });
            b.addEventListener('mouseleave', () => { b.style.background = `rgba(0,0,0,0.55)`; });
            b.addEventListener('click', (e) => { e.stopPropagation(); });
            return b;
        };
        const prevBtn = mkNavBtn('‹');
        prevBtn.style.left = '-54px';
        const nextBtn = mkNavBtn('›');
        nextBtn.style.right = '-54px';
        imgWrap.appendChild(prevBtn);
        imgWrap.appendChild(nextBtn);

        // Close button
        const lbClose = document.createElement('button');
        lbClose.type = 'button'; lbClose.textContent = '✕';
        lbClose.style.cssText = `
            position:absolute;top:16px;right:16px;
            background:rgba(255,255,255,0.12);border:1px solid rgba(255,255,255,0.25);
            color:#fff;font-size:18px;font-weight:900;width:36px;height:36px;
            border-radius:8px;cursor:pointer;display:flex;align-items:center;justify-content:center;
        `;
        lbClose.addEventListener('click', (ev) => { ev.stopPropagation(); overlay.remove(); });
        stopEventsOn(lbClose);

        // Dot indicators
        const dotsWrap = document.createElement('div');
        dotsWrap.style.cssText = `display:flex;gap:6px;align-items:center;`;
        const dots = entries.map((_, i) => {
            const d = document.createElement('div');
            d.style.cssText = `width:8px;height:8px;border-radius:50%;cursor:pointer;
                background:rgba(255,255,255,0.25);transition:background 0.2s,transform 0.2s;`;
            d.addEventListener('click', (e) => { e.stopPropagation(); goTo(i); });
            dotsWrap.appendChild(d);
            return d;
        });

        function renderEntry() {
            const entry = entries[idx];
            lbTitle.textContent = entry.label || '';
            lbCounter.textContent = `${idx + 1} / ${entries.length}`;
            prevBtn.style.visibility = entries.length > 1 ? 'visible' : 'hidden';
            nextBtn.style.visibility = entries.length > 1 ? 'visible' : 'hidden';

            // Update dots
            dots.forEach((d, i) => {
                d.style.background = i === idx ? accentColor : 'rgba(255,255,255,0.25)';
                d.style.transform = i === idx ? 'scale(1.3)' : 'scale(1)';
            });

            // Swap canvas content
            while (canvasSlot.firstChild) canvasSlot.removeChild(canvasSlot.firstChild);
            if (entry.bigReady && entry.bigCanvas) {
                entry.bigCanvas.style.cssText = 'display:block;width:auto;height:auto;max-width:90vw;max-height:65vh;border-radius:10px;';
                entry.bigCanvas.addEventListener('click', (e) => e.stopPropagation());
                canvasSlot.appendChild(entry.bigCanvas);
            } else {
                const msg = document.createElement('div');
                msg.textContent = 'Preview loading…';
                msg.style.cssText = 'color:#fff;opacity:0.7;font-size:14px;';
                canvasSlot.appendChild(msg);
                // Poll until ready
                const poll = setInterval(() => {
                    if (entries[idx] === entry && entry.bigReady && entry.bigCanvas) {
                        clearInterval(poll);
                        renderEntry();
                    }
                }, 150);
            }
        }

        function goTo(i) {
            idx = ((i % entries.length) + entries.length) % entries.length;
            renderEntry();
        }

        prevBtn.addEventListener('click', () => goTo(idx - 1));
        nextBtn.addEventListener('click', () => goTo(idx + 1));

        // Swipe support
        let touchStartX = 0;
        overlay.addEventListener('touchstart', (e) => { touchStartX = e.touches[0].clientX; }, { passive: true });
        overlay.addEventListener('touchend', (e) => {
            const dx = e.changedTouches[0].clientX - touchStartX;
            if (Math.abs(dx) > 40) goTo(dx < 0 ? idx + 1 : idx - 1);
        }, { passive: true });

        // Arrow key support
        const onKey = (e) => {
            if (e.key === 'ArrowLeft') { e.stopPropagation(); goTo(idx - 1); }
            if (e.key === 'ArrowRight') { e.stopPropagation(); goTo(idx + 1); }
            if (e.key === 'Escape') { overlay.remove(); document.removeEventListener('keydown', onKey, true); }
        };
        document.addEventListener('keydown', onKey, true);
        overlay.addEventListener('click', () => { overlay.remove(); document.removeEventListener('keydown', onKey, true); });

        overlay.appendChild(lbTitle);
        overlay.appendChild(lbCounter);
        overlay.appendChild(imgWrap);
        if (entries.length > 1) overlay.appendChild(dotsWrap);
        overlay.appendChild(lbClose);
        stopEventsOn(overlay);
        // Re-allow click-to-close on the overlay background
        overlay.addEventListener('click', () => { overlay.remove(); document.removeEventListener('keydown', onKey, true); });
        (document.body || document.documentElement).appendChild(overlay);
        renderEntry();
    }

    function updateProfileList() {
        const list = document.getElementById('gvf-profile-list');
        if (!list) { logW('Profile list not found'); return; }

        while (list.firstChild) list.removeChild(list.firstChild);

        const LW = 1280, LH = 720, PW = 160, PH = 90;

        // Capture raw video frame ONCE — shared base for all profile previews
        let rawCanvas = null;
        (() => {
            try {
                const c = document.createElement('canvas');
                c.width = LW; c.height = LH;
                const ctx = c.getContext('2d', { alpha: false, willReadFrequently: true });
                if (!ctx) return;
                let drew = false;
                let video = getHudPrimaryVideo();
                if (!video) { const all = Array.from(document.querySelectorAll('video')); video = all.find(v => v.readyState >= 2 && v.videoWidth > 0) || null; }
                if (video && video.readyState >= 2 && video.videoWidth > 0) {
                    try { ctx.drawImage(video, 0, 0, LW, LH); ctx.getImageData(0, 0, 1, 1); drew = true; } catch(_) {}
                }
                if (!drew) {
                    const grad = ctx.createLinearGradient(0, 0, LW, LH);
                    grad.addColorStop(0,'#1a3a5c'); grad.addColorStop(0.25,'#c85032');
                    grad.addColorStop(0.5,'#f0c040'); grad.addColorStop(0.75,'#3ab56a'); grad.addColorStop(1,'#8040c0');
                    ctx.fillStyle = grad; ctx.fillRect(0, 0, LW, LH);
                }
                rawCanvas = c;
            } catch(_) {}
        })();

        // Snapshot/restore helpers
        const snapshotGlobals = () => ({
            enabled, darkMoody, tealOrange, vibrantSat, notify,
            sl, sr, bl, wl, dn, edge, hdr, profile, renderMode,
            autoOn, autoStrength, autoLockWB,
            u_contrast, u_black, u_white, u_highlights, u_shadows,
            u_sat, u_vib, u_sharp, u_gamma, u_grain, u_hue,
            u_r_gain, u_g_gain, u_b_gain, cbFilter,
            activeLutProfileKey: String(activeLutProfileKey)
        });

        // Apply profile settings temporarily, render onto destCanvas, restore
        const renderProfilePreview = (profileSettings, destCanvas) => {
            if (!rawCanvas) return;
            try { renderFrameWithSettings(destCanvas, rawCanvas, profileSettings); } catch(_) {}
        };

        const renderQueue = [];
        const slideshowEntries = []; // filled per-profile for slideshow

        userProfiles.forEach(userProf => {
            const isActive = activeUserProfile && activeUserProfile.id === userProf.id;

            const item = document.createElement('div');
            item.style.cssText = `display:flex;align-items:center;gap:12px;padding:10px 12px;
                background:${isActive ? 'rgba(42,111,219,0.3)' : 'rgba(255,255,255,0.05)'};
                border:2px solid ${isActive ? '#2a6fdb' : 'rgba(255,255,255,0.1)'};
                border-radius:8px;margin:2px 0;`;

            // Thumbnail
            const previewWrap = document.createElement('div');
            previewWrap.style.cssText = `flex-shrink:0;border-radius:6px;overflow:hidden;
                border:1px solid rgba(42,111,219,0.45);width:${PW}px;height:${PH}px;background:#111;cursor:zoom-in;`;
            const thumbCanvas = document.createElement('canvas');
            thumbCanvas.width = PW; thumbCanvas.height = PH;
            thumbCanvas.style.cssText = `display:block;width:${PW}px;height:${PH}px;`;
            previewWrap.appendChild(thumbCanvas);

            // Big canvas for lightbox
            const bigCanvas = document.createElement('canvas');
            bigCanvas.width = LW; bigCanvas.height = LH;
            bigCanvas.style.cssText = 'display:block;width:auto;height:auto;max-width:90vw;max-height:65vh;border-radius:10px;';
            let bigReady = false;

            const entry = { bigCanvas, get bigReady() { return bigReady; }, label: '👤 ' + String(userProf.name) + (isActive ? '  · active' : '') };
            slideshowEntries.push(entry);
            renderQueue.push({ settings: userProf.settings || {}, bigCanvas, thumbCanvas, onDone: () => { bigReady = true; } });

            // Slideshow lightbox
            previewWrap.addEventListener('click', (e) => {
                e.stopPropagation();
                openSlideshow(slideshowEntries, slideshowEntries.indexOf(entry), '#2a6fdb');
            });
            stopEventsOn(previewWrap);

            // Info
            const info = document.createElement('div');
            info.style.cssText = `display:flex;flex-direction:column;gap:4px;flex:1;min-width:0;overflow:hidden;`;
            const nameSpan = document.createElement('span');
            nameSpan.style.cssText = `font-weight:${isActive ? '900' : '600'};color:${isActive ? '#fff' : '#ccc'};
                font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;`;
            nameSpan.textContent = userProf.name + (isActive ? ' (active)' : '');
            const dateSpan = document.createElement('span');
            dateSpan.style.cssText = `font-size:11px;color:#888;`;
            dateSpan.textContent = 'Created: ' + new Date(userProf.createdAt).toLocaleDateString('en-US');
            info.appendChild(nameSpan); info.appendChild(dateSpan);

            // Actions
            const actions = document.createElement('div');
            actions.style.cssText = `display:flex;gap:8px;flex-shrink:0;`;

            const mkActionBtn = (text, bg, border, color) => {
                const b = document.createElement('button');
                b.type = 'button'; b.textContent = text;
                b.style.cssText = `background:${bg};border:1px solid ${border};color:${color};
                    padding:6px 12px;border-radius:6px;font-size:12px;font-weight:900;cursor:pointer;`;
                stopEventsOn(b); return b;
            };

            if (!isActive) {
                const activateBtn = mkActionBtn('Activate','rgba(42,111,219,0.3)','#2a6fdb','#fff');
                activateBtn.addEventListener('mouseenter', () => { activateBtn.style.background='rgba(42,111,219,0.5)'; });
                activateBtn.addEventListener('mouseleave', () => { activateBtn.style.background='rgba(42,111,219,0.3)'; });
                activateBtn.addEventListener('click', (e) => {
                    e.preventDefault(); e.stopPropagation();
                    switchToUserProfile(userProf.id); updateProfileList();
                    const ai = document.getElementById('gvf-active-profile-info');
                    if (ai) setActiveProfileInfo(ai, activeUserProfile?.name);
                });
                actions.appendChild(activateBtn);
            }

            // Edit button — floating window, uses same rawCanvas
            const editBtn = mkActionBtn('Edit','rgba(255,255,255,0.08)','rgba(255,255,255,0.3)','#fff');
            editBtn.addEventListener('click', (e) => {
                e.preventDefault(); e.stopPropagation();
                const existingEd = document.getElementById('gvf-userProf-edit-window');
                if (existingEd) existingEd.remove();

                const EW = 680;
                const win = document.createElement('div');
                win.id = 'gvf-userProf-edit-window';
                win.style.cssText = `position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
                    width:${EW}px;max-width:96vw;max-height:90vh;
                    background:rgba(18,18,18,0.98);backdrop-filter:blur(12px);
                    border:2px solid #2a6fdb;border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,0.85);
                    color:#eaeaea;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;
                    z-index:2147483647;display:flex;flex-direction:column;padding:20px;gap:12px;
                    user-select:none;pointer-events:auto;overflow-y:auto;`;
                stopEventsOn(win);

                const edHeader = document.createElement('div');
                edHeader.style.cssText = `display:flex;justify-content:space-between;align-items:center;
                    padding-bottom:10px;border-bottom:2px solid #2a6fdb;flex-shrink:0;cursor:move;`;
                const edTitle = document.createElement('div');
                edTitle.textContent = '✏️ Edit — ' + String(userProf.name);
                edTitle.style.cssText = `font-size:17px;font-weight:900;color:#fff;text-shadow:0 0 10px rgba(42,111,219,0.6);`;
                const edClose = document.createElement('button');
                edClose.type = 'button'; edClose.textContent = '✕';
                edClose.style.cssText = `background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);
                    color:#fff;font-size:18px;font-weight:900;width:34px;height:34px;
                    border-radius:8px;cursor:pointer;display:flex;align-items:center;justify-content:center;`;
                edClose.addEventListener('click', () => win.remove());
                stopEventsOn(edClose);
                edHeader.appendChild(edTitle); edHeader.appendChild(edClose);
                win.appendChild(edHeader);
                makeFloatingManagerDraggable(win, edHeader, null);

                // Preview canvas — rendered once on open, updated only on Apply Preview
                const epW = EW - 44, epH = Math.round(epW * 9/16);
                const previewC = document.createElement('canvas');
                previewC.width = LW; previewC.height = LH;
                previewC.style.cssText = `display:block;width:${epW}px;height:${epH}px;
                    border-radius:8px;border:1px solid rgba(42,111,219,0.4);flex-shrink:0;`;
                const previewLabel = document.createElement('div');
                previewLabel.style.cssText = `font-size:11px;color:#888;text-align:center;margin-top:-4px;`;
                previewLabel.textContent = 'Preview — click Apply Preview to update';
                win.appendChild(previewC); win.appendChild(previewLabel);

                // Render using rawCanvas (already captured, no new video grab, no global changes)
                const edRenderPreview = (settingsObj) => {
                    try {
                        if (rawCanvas) {
                            renderFrameWithSettings(previewC, rawCanvas, settingsObj);
                            previewLabel.textContent = 'Preview — filter applied ✓';
                        } else {
                            // gradient fallback
                            const offC = document.createElement('canvas'); offC.width=LW; offC.height=LH;
                            const oCtx = offC.getContext('2d',{alpha:false});
                            if (oCtx) {
                                const grad = oCtx.createLinearGradient(0,0,LW,LH);
                                grad.addColorStop(0,'#1a3a5c'); grad.addColorStop(0.5,'#f0c040'); grad.addColorStop(1,'#8040c0');
                                oCtx.fillStyle=grad; oCtx.fillRect(0,0,LW,LH);
                            }
                            renderFrameWithSettings(previewC, offC, settingsObj);
                            previewLabel.textContent = 'Preview — no video, gradient used';
                        }
                    } catch(err) { previewLabel.textContent = 'Preview error: ' + err.message; }
                };

                // Render once on open
                setTimeout(() => edRenderPreview(userProf.settings || {}), 0);

                const textarea = document.createElement('textarea');
                textarea.style.cssText = `width:100%;min-height:240px;resize:vertical;flex-shrink:0;
                    background:rgba(0,0,0,0.55);border:1px solid rgba(255,255,255,0.18);
                    border-radius:8px;padding:10px 12px;color:#e8e8e8;
                    font-size:12px;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;
                    line-height:1.4;outline:none;`;
                stopEventsOn(textarea);
                try { textarea.value = JSON.stringify(userProf, null, 2); } catch(_) { textarea.value = '{}'; }
                win.appendChild(textarea);

                const btnRow = document.createElement('div');
                btnRow.style.cssText = 'display:flex;gap:8px;align-items:center;flex-shrink:0;';
                const applyPreviewBtn = mkActionBtn('👁 Apply Preview','rgba(255,138,0,0.2)','#ff8a00','#ffd7a6');
                const saveJsonBtn    = mkActionBtn('💾 Save','rgba(42,111,219,0.35)','#2a6fdb','#fff');
                const cancelBtn      = mkActionBtn('Cancel','rgba(255,255,255,0.08)','rgba(255,255,255,0.2)','#ccc');
                const errMsg = document.createElement('div');
                errMsg.style.cssText = 'font-size:11px;color:#ff6b6b;flex:1;';

                const parseAndValidate = () => {
                    let parsed;
                    try {
                        parsed = JSON.parse(textarea.value);
                    } catch(e) {
                        throw new Error('JSON syntax error: ' + e.message);
                    }
                    if (!parsed || typeof parsed !== 'object') throw new Error('JSON must be an object');
                    parsed.id = userProf.id;
                    if (!parsed.name || typeof parsed.name !== 'string' || !parsed.name.trim()) {
                        parsed.name = userProf.name || 'Profile';
                    }
                    // If settings missing, fall back to original profile settings
                    if (!parsed.settings || typeof parsed.settings !== 'object') {
                        parsed.settings = userProf.settings || {};
                    }
                    try {
                        parsed.settings = buildImportedUserProfileSettings(parsed.settings);
                    } catch(e) {
                        throw new Error('Settings merge failed: ' + e.message);
                    }
                    return parsed;
                };

                applyPreviewBtn.addEventListener('click', (ev) => {
                    ev.preventDefault(); ev.stopPropagation();
                    try { errMsg.textContent = ''; edRenderPreview(parseAndValidate().settings); }
                    catch(err) { errMsg.textContent = 'JSON error: ' + err.message; }
                });
                saveJsonBtn.addEventListener('click', (ev) => {
                    ev.preventDefault(); ev.stopPropagation();
                    errMsg.textContent = '';
                    let parsed;
                    try { parsed = parseAndValidate(); } catch(err) { errMsg.textContent = 'Parse error: ' + err.message; return; }
                    try {
                        parsed.updatedAt = Date.now();
                        parsed.createdAt = userProf.createdAt || Date.now();
                        if (!parsed.name || !String(parsed.name).trim()) parsed.name = userProf.name || 'Profile';
                        // Search by id (string compare) — robust against array replacement
                        const profileId = String(userProf.id || '');
                        let idx = userProfiles.findIndex(p => p && String(p.id) === profileId);
                        if (idx < 0) {
                            // Fallback: search by name
                            idx = userProfiles.findIndex(p => p && String(p.name) === String(userProf.name));
                        }
                        if (idx >= 0) {
                            userProfiles[idx] = parsed;
                        } else {
                            // Profile not found — add it
                            userProfiles.push(parsed);
                        }
                        saveUserProfiles();
                        win.remove();
                        updateProfileList();
                        const ai = document.getElementById('gvf-active-profile-info');
                        if (ai) setActiveProfileInfo(ai, activeUserProfile?.name);
                    } catch(err) { errMsg.textContent = 'Save error: ' + err.message; }
                });
                cancelBtn.addEventListener('click', (ev) => { ev.preventDefault(); ev.stopPropagation(); win.remove(); });
                btnRow.appendChild(applyPreviewBtn); btnRow.appendChild(saveJsonBtn);
                btnRow.appendChild(cancelBtn); btnRow.appendChild(errMsg);
                win.appendChild(btnRow);
                (document.body || document.documentElement).appendChild(win);
            });
            actions.appendChild(editBtn);

            if (userProf.id !== 'default') {
                const deleteBtn = mkActionBtn('✕ Delete','rgba(255,68,68,0.2)','#ff4444','#ff8888');
                deleteBtn.addEventListener('mouseenter', () => { deleteBtn.style.background='rgba(255,68,68,0.4)'; deleteBtn.style.color='#fff'; });
                deleteBtn.addEventListener('mouseleave', () => { deleteBtn.style.background='rgba(255,68,68,0.2)'; deleteBtn.style.color='#ff8888'; });
                deleteBtn.addEventListener('click', (e) => {
                    e.preventDefault(); e.stopPropagation();
                    if (confirm(`Really delete userProf "${userProf.name}"?`)) {
                        deleteUserProfile(userProf.id); updateProfileList();
                        const ai = document.getElementById('gvf-active-profile-info');
                        if (ai) setActiveProfileInfo(ai, activeUserProfile?.name);
                    }
                });
                actions.appendChild(deleteBtn);
            }

            item.appendChild(previewWrap); item.appendChild(info); item.appendChild(actions);
            list.appendChild(item);
        });

        // Render previews sequentially — one per setTimeout tick, no re-render after
        const processQueue = (i) => {
            if (i >= renderQueue.length) return;
            const { settings, bigCanvas, thumbCanvas, onDone } = renderQueue[i];
            setTimeout(() => {
                try {
                    renderProfilePreview(settings, bigCanvas);
                    onDone();
                    const tCtx = thumbCanvas.getContext('2d', { alpha: false });
                    if (tCtx) {
                        tCtx.imageSmoothingEnabled = true;
                        try { tCtx.imageSmoothingQuality = 'high'; } catch(_) {}
                        tCtx.drawImage(bigCanvas, 0, 0, PW, PH);
                    }
                } catch(_) {}
                processQueue(i + 1);
            }, 0);
        };
        processQueue(0);
    }

    function toggleConfigMenu() {
        log('toggleConfigMenu called, currently:', configMenuVisible);

        configMenuVisible = !configMenuVisible;
        const menu = document.getElementById(CONFIG_MENU_ID);

        if (!menu) {
            log('Menu does not exist, creating new one...');
            const newMenu = createConfigMenu();
            if (configMenuVisible) {
                setTimeout(() => {
                    updateProfileList();
                    newMenu.style.display = 'flex';
                }, 10);
            }
            return;
        }

        if (configMenuVisible) {
            log('Showing menu');
            updateProfileList();
            menu.style.display = 'flex';
        } else {
            log('Hiding menu');
            menu.style.display = 'none';
        }
    }

    // LUT Config Menu (LUT Profile Manager)
    // -------------------------
    let lutConfigMenuVisible = false;

    /**
     * Applies a 4x5 feColorMatrix (row-major, 20 floats) to ImageData in-place.
     * Runs on a downscaled buffer for speed; caller handles up/downscaling.
     */
    function applyLut4x5ToImageData(imageData, matrix4x5) {
        if (!imageData || !Array.isArray(matrix4x5) || matrix4x5.length !== 20) return;
        const d = imageData.data;
        const m = matrix4x5.map(Number);
        const m0=m[0],m1=m[1],m2=m[2],m4=m[4]*255;
        const m5=m[5],m6=m[6],m7=m[7],m9=m[9]*255;
        const m10=m[10],m11=m[11],m12=m[12],m14=m[14]*255;
        for (let i = 0; i < d.length; i += 4) {
            const r=d[i], g=d[i+1], b=d[i+2];
            d[i]   = m0*r+m1*g+m2*b+m4   < 0 ? 0 : m0*r+m1*g+m2*b+m4   > 255 ? 255 : m0*r+m1*g+m2*b+m4;
            d[i+1] = m5*r+m6*g+m7*b+m9   < 0 ? 0 : m5*r+m6*g+m7*b+m9   > 255 ? 255 : m5*r+m6*g+m7*b+m9;
            d[i+2] = m10*r+m11*g+m12*b+m14 < 0 ? 0 : m10*r+m11*g+m12*b+m14 > 255 ? 255 : m10*r+m11*g+m12*b+m14;
        }
    }

    /**
     * Captures one shared raw ImageData at 320x180 from the current video frame
     * (or gradient fallback). Used as the base for all LUT previews this session.
     */
    function captureLutPreviewFrame() {
        const W = 1280, H = 720;
        const c = document.createElement('canvas');
        c.width = W; c.height = H;
        const ctx = c.getContext('2d', { alpha: false, willReadFrequently: true });
        if (!ctx) return null;
        let drew = false;
        const video = getHudPrimaryVideo();
        if (video && video.readyState >= 2 && video.videoWidth > 0) {
            try {
                ctx.drawImage(video, 0, 0, W, H);
                ctx.getImageData(0, 0, 1, 1);
                drew = true;
            } catch(_) {}
        }
        if (!drew) {
            const grad = ctx.createLinearGradient(0, 0, W, H);
            grad.addColorStop(0, '#1a3a5c'); grad.addColorStop(0.25, '#c85032');
            grad.addColorStop(0.5, '#f0c040'); grad.addColorStop(0.75, '#3ab56a');
            grad.addColorStop(1, '#8040c0');
            ctx.fillStyle = grad; ctx.fillRect(0, 0, W, H);
        }
        try { return ctx.getImageData(0, 0, W, H); } catch(_) { return null; }
    }

    /**
     * Applies LUT to ImageData in async chunks to avoid blocking the main thread.
     * Calls onDone() when finished.
     */
    function applyLut4x5Async(imageData, matrix4x5, onDone) {
        const d = imageData.data;
        const m = matrix4x5.map(Number);
        const m0=m[0],m1=m[1],m2=m[2],m4=m[4]*255;
        const m5=m[5],m6=m[6],m7=m[7],m9=m[9]*255;
        const m10=m[10],m11=m[11],m12=m[12],m14=m[14]*255;
        const total = d.length;
        const CHUNK = 1280 * 60 * 4; // 60 rows per chunk
        let offset = 0;
        function processChunk() {
            const end = Math.min(offset + CHUNK, total);
            for (let i = offset; i < end; i += 4) {
                const r=d[i], g=d[i+1], b=d[i+2];
                d[i]   = m0*r+m1*g+m2*b+m4   < 0 ? 0 : m0*r+m1*g+m2*b+m4   > 255 ? 255 : m0*r+m1*g+m2*b+m4;
                d[i+1] = m5*r+m6*g+m7*b+m9   < 0 ? 0 : m5*r+m6*g+m7*b+m9   > 255 ? 255 : m5*r+m6*g+m7*b+m9;
                d[i+2] = m10*r+m11*g+m12*b+m14 < 0 ? 0 : m10*r+m11*g+m12*b+m14 > 255 ? 255 : m10*r+m11*g+m12*b+m14;
            }
            offset = end;
            if (offset < total) {
                setTimeout(processChunk, 0);
            } else {
                onDone(imageData);
            }
        }
        setTimeout(processChunk, 0);
    }

    function createLutConfigMenu() {
        let existingMenu = document.getElementById(LUT_CONFIG_MENU_ID);
        if (existingMenu) existingMenu.remove();

        const menu = document.createElement('div');
        menu.id = LUT_CONFIG_MENU_ID;
        menu.style.cssText = `
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            width: 820px;
            max-width: 98vw;
            max-height: 88vh;
            background: rgba(20, 20, 20, 0.98);
            backdrop-filter: blur(10px);
            border: 2px solid #ff8a00;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0, 0, 0, 0.8), 0 0 0 1px rgba(255,255,255,0.1) inset;
            color: #eaeaea;
            font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
            z-index: 2147483647;
            display: none;
            flex-direction: column;
            padding: 20px;
            user-select: none;
            pointer-events: auto;
        `;
        stopEventsOn(menu);

        const header = document.createElement('div');
        header.style.cssText = `
            display:flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 16px;
            padding-bottom: 10px;
            border-bottom: 2px solid #ff8a00;
        `;

        const title = document.createElement('div');
        title.textContent = '🎨 LUT Profile Manager';
        title.style.cssText = `
            font-size: 20px;
            font-weight: 900;
            color: #fff;
            text-shadow: 0 0 10px rgba(255,138,0,0.55);
        `;

        const closeBtn = document.createElement('button');
        closeBtn.type = 'button';
        closeBtn.textContent = '✕';
        closeBtn.style.cssText = `
            background: rgba(255, 255, 255, 0.1);
            border: none;
            color: #fff;
            font-size: 20px;
            cursor: pointer;
            width: 36px;
            height: 36px;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s;
            border: 1px solid rgba(255,255,255,0.2);
        `;
        closeBtn.addEventListener('click', (e) => {
            e.preventDefault(); e.stopPropagation();
            toggleLutConfigMenu();
        });
        stopEventsOn(closeBtn);

        header.appendChild(title);
        header.appendChild(closeBtn);
        menu.appendChild(header);

        makeFloatingManagerDraggable(menu, header, K.LUT_PROFILE_MANAGER_POS);

        const activeInfo = document.createElement('div');
        activeInfo.id = 'gvf-active-lut-profile-info';
        activeInfo.style.cssText = `
            background: rgba(255, 138, 0, 0.18);
            border: 1px solid rgba(255, 138, 0, 0.65);
            border-radius: 8px;
            padding: 10px;
            margin-bottom: 12px;
            font-size: 13px;
            display:flex;
            align-items:center;
            gap:8px;
        `;

        const setActiveLutInfo = () => {
            while (activeInfo.firstChild) activeInfo.removeChild(activeInfo.firstChild);
            activeInfo.append('🟠 Active LUT: ');
            const strong = document.createElement('strong');
            strong.textContent = (activeLutProfileKey && activeLutProfileKey !== 'none') ? lutParseKey(activeLutProfileKey).name : 'None';
            activeInfo.appendChild(strong);
        };
        setActiveLutInfo();
        menu.appendChild(activeInfo);

        // Group controls (for better overview)
        const groupCtlRow = document.createElement('div');
        groupCtlRow.style.cssText = 'display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:12px;';

        const groupLabel = document.createElement('div');
        groupLabel.textContent = 'Group';
        groupLabel.style.cssText = 'font-size:12px;font-weight:900;opacity:0.85;min-width:52px;';

        const groupFilter = document.createElement('select');
        groupFilter.id = 'gvf-lut-group-filter';
        groupFilter.style.cssText = `
            flex: 1;
            min-width: 160px;
            background: rgba(30,30,30,0.9);
            color: #eaeaea;
            border: 1px solid rgba(255,255,255,0.2);
            border-radius: 8px;
            padding: 6px 8px;
            font-size: 12px;
            font-weight: 900;
            cursor: pointer;
        `;
        stopEventsOn(groupFilter);

        const mkSmallBtn = (text, bg, fg) => {
            const b = document.createElement('button');
            b.type = 'button';
            b.textContent = text;
            b.style.cssText = `
                cursor:pointer;
                padding: 8px 10px;border-radius: 10px;
                border: 1px solid rgba(255,255,255,0.14);
                background: ${bg};
                color: ${fg};
                font-weight: 900;
                font-size: 12px;
            `;
            stopEventsOn(b);
            return b;
        };

        const addGroupBtn = mkSmallBtn('Add Group', 'rgba(255, 255, 255, 0.10)', '#ffffff');
        const renameGroupBtn = mkSmallBtn('Rename', 'rgba(255, 255, 255, 0.10)', '#ffffff');
        const deleteGroupBtn = mkSmallBtn('Delete', 'rgba(255, 68, 68, 0.20)', '#ffd0d0');

        const getAllGroupNames = () => {
            const set = new Set();
            // explicit groups (supports empty groups)
            for (const g0 of (Array.isArray(lutGroups) ? lutGroups : [])) {
                const g = String(g0 || '').trim();
                if (g) set.add(g);
            }
            // groups referenced by profiles
            for (const p of (Array.isArray(lutProfiles) ? lutProfiles : [])) {
                const g = (p && p.group) ? String(p.group).trim() : '';
                if (g) set.add(g);
            }
            return Array.from(set).sort((a, b) => a.localeCompare(b));
        };

        const rebuildGroupFilter = () => {
            while (groupFilter.firstChild) groupFilter.removeChild(groupFilter.firstChild);

            const optAll = document.createElement('option');
            optAll.value = '__all__';
            optAll.textContent = 'All groups';
            groupFilter.appendChild(optAll);

            const names = getAllGroupNames();
            for (const g of names) {
                const o = document.createElement('option');
                o.value = g;
                o.textContent = g;
                groupFilter.appendChild(o);
            }

            const optUng = document.createElement('option');
            optUng.value = '__ungrouped__';
            optUng.textContent = 'Ungrouped';
            groupFilter.appendChild(optUng);

            // keep selection if still exists
            const cur = String(groupFilter.dataset.gvfValue || '__all__');
            const canKeep = Array.from(groupFilter.options).some(o => String(o.value) === cur);
            groupFilter.value = canKeep ? cur : '__all__';
            groupFilter.dataset.gvfValue = groupFilter.value;
        };

        // will be extended later (also updates the per-profile group selector)
        let _rebuildLutGroupUis = () => { rebuildGroupFilter(); };

        groupFilter.addEventListener('change', () => {
            groupFilter.dataset.gvfValue = String(groupFilter.value || '__all__');
            updateLutProfileList();
        });

        addGroupBtn.addEventListener('click', () => {
            const n = prompt('New group name:');
            if (!n) return;
            const g = String(n).trim();
            if (!g) return;

            if (!Array.isArray(lutGroups)) lutGroups = [];
            if (!lutGroups.some(x => String(x).trim() === g)) {
                lutGroups.push(g);
                saveLutGroups();
            }

            groupFilter.dataset.gvfValue = g;
            _rebuildLutGroupUis();
            groupFilter.value = g;
            updateLutProfileList();

            log('LUT group created:', g);
        });

        renameGroupBtn.addEventListener('click', () => {
            const cur = String(groupFilter.value || '__all__');
            if (cur === '__all__' || cur === '__ungrouped__') {
                alert('Select a concrete group first.');
                return;
            }
            const n = prompt(`Rename group "${cur}" to:`, cur);
            if (!n) return;
            const next = String(n).trim();
            if (!next || next === cur) return;

            for (const p of (Array.isArray(lutProfiles) ? lutProfiles : [])) {
                if (p && String(p.group || '').trim() === cur) p.group = next;
            }

            // rename in explicit group list
            if (!Array.isArray(lutGroups)) lutGroups = [];
            lutGroups = lutGroups.map(x => (String(x || '').trim() === cur ? next : String(x || '').trim())).filter(Boolean);
            saveLutGroups();

            saveLutProfiles();

            groupFilter.dataset.gvfValue = next;
            _rebuildLutGroupUis();
            groupFilter.value = next;

            try { if (typeof refreshLutDropdownFn === 'function') refreshLutDropdownFn(); } catch (_) { }

            updateLutProfileList();
            log('LUT group renamed:', cur, '->', next);
        });

        deleteGroupBtn.addEventListener('click', () => {
            const cur = String(groupFilter.value || '__all__');
            if (cur === '__all__') { alert('Select a concrete group first.'); return; }
            if (cur === '__ungrouped__') { alert('Ungrouped cannot be deleted.'); return; }

            const ok = confirm(`Delete group "${cur}"? (Profiles will become ungrouped)`);
            if (!ok) return;

            for (const p of (Array.isArray(lutProfiles) ? lutProfiles : [])) {
                if (p && String(p.group || '').trim() === cur) delete p.group;
            }

            // remove from explicit group list
            if (!Array.isArray(lutGroups)) lutGroups = [];
            lutGroups = lutGroups.filter(x => String(x || '').trim() !== cur);
            saveLutGroups();

            saveLutProfiles();

            groupFilter.dataset.gvfValue = '__all__';
            _rebuildLutGroupUis();
            groupFilter.value = '__all__';

            try { if (typeof refreshLutDropdownFn === 'function') refreshLutDropdownFn(); } catch (_) { }

            updateLutProfileList();
            log('LUT group deleted:', cur);
        });

        groupCtlRow.appendChild(groupLabel);
        groupCtlRow.appendChild(groupFilter);
        groupCtlRow.appendChild(addGroupBtn);
        groupCtlRow.appendChild(renameGroupBtn);
        groupCtlRow.appendChild(deleteGroupBtn);

        menu.appendChild(groupCtlRow);
        try { _rebuildLutGroupUis(); } catch (_) { }

        const ctlRow = document.createElement('div');
        ctlRow.style.cssText = `display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px;`;

        const mkCtlBtn = (text) => {
            const b = document.createElement('button');
            b.type = 'button';
            b.textContent = text;
            b.style.cssText = `
                cursor:pointer;
                padding: 8px 10px;border-radius: 10px;
                border: 1px solid rgba(255, 138, 0, 0.55);
                background: rgba(255, 138, 0, 0.18);
                color: #ffd7a6;
                font-weight: 900;
                font-size: 12px;
            `;
            stopEventsOn(b);
            return b;
        };

        const exportBtn = mkCtlBtn('Export ZIP');
        const importBtn = mkCtlBtn('Import ZIP');

        const importInput = document.createElement('input');
        importInput.type = 'file';
        importInput.accept = '.zip,.json,application/zip,application/json';
        importInput.style.display = 'none';

        importBtn.addEventListener('click', () => importInput.click());


exportBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();

            try {
                const zipBlob = exportAllLutProfilesAsZip();
                if (!zipBlob) {
                    logW('No LUT profiles to export.');
                    return;
                }

                const zipName = _zipName('gvf_lut_profiles');
                downloadBlob(zipBlob, zipName);

                log('Exported LUT profiles ZIP:', zipName);
            } catch (err) {
                logW('LUT export failed:', err);
                alert('LUT export failed. Check console for details.');
            }
        });
importInput.addEventListener('change', async () => {
            const file = importInput.files && importInput.files[0] ? importInput.files[0] : null;
            importInput.value = '';
            if (!file) return;

            try {
                const res = await importLutProfilesFromZipOrJsonFile(file);
                if (!res || !res.ok) {
                    logW('LUT import failed:', res && res.msg ? res.msg : 'unknown');
                    alert(res && res.msg ? res.msg : 'LUT import failed. Check console for details.');
                    return;
                }

                log(res.msg || 'LUT import ok.');
            } catch (e) {
                logW('LUT import failed:', e);
                alert('LUT import failed. Check console for details.');
            }
        });

        const loadExamplesBtn = mkCtlBtn('⬇ Load Examples LUT');
        loadExamplesBtn.title = 'Download and import the bundled LUT example profiles';
        loadExamplesBtn.addEventListener('click', async (e) => {
            e.preventDefault();
            e.stopPropagation();
            loadExamplesBtn.disabled = true;
            loadExamplesBtn.textContent = '⏳ Loading…';
            try {
                // githubusercontent CDN URL – try direct first, then proxy fallbacks
                const rawUrl = 'https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/main/LUTsProfiles_v2.0.zip';
                const candidates = [
                    rawUrl,
                    'https://api.allorigins.win/raw?url=' + encodeURIComponent(rawUrl),
                    'https://corsproxy.io/?' + encodeURIComponent(rawUrl),
                    'https://proxy.cors.sh/' + rawUrl,
                ];
                let response = null;
                for (const url of candidates) {
                    try {
                        const r = await fetch(url);
                        if (r.ok) { response = r; break; }
                    } catch (_) { }
                }
                if (!response) throw new Error('All fetch attempts failed (CORS/network)');
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                const blob = await response.blob();
                const file = new File([blob], 'LUTsProfiles_v2.0.zip', { type: 'application/zip' });
                const res = await importLutProfilesFromZipOrJsonFile(file);
                if (!res || !res.ok) {
                    alert(res && res.msg ? res.msg : 'LUT import failed. Check console for details.');
                } else {
                    log(res.msg || 'Example LUTs imported.');
                    try { showValueNotification('LUT Import', res.msg, '#4cff6a'); } catch (_) { }
                }
            } catch (err) {
                logW('Load Examples LUT failed:', err);
                alert('Load Examples LUT failed: ' + (err && err.message ? err.message : err));
            } finally {
                loadExamplesBtn.disabled = false;
                loadExamplesBtn.textContent = '⬇ Load Examples LUT';
            }
        });

        ctlRow.appendChild(exportBtn);
        ctlRow.appendChild(importBtn);
        ctlRow.appendChild(loadExamplesBtn);
        ctlRow.appendChild(importInput);
        menu.appendChild(ctlRow);

        const listContainer = document.createElement('div');
        listContainer.style.cssText = `
            flex: 1;
            overflow-y: auto;
            margin-bottom: 14px;
            max-height: 320px;
            background: rgba(0,0,0,0.3);
            border-radius: 8px;
            padding: 6px;
        `;

        const lutList = document.createElement('div');
        lutList.id = 'gvf-lut-profile-list';
        lutList.style.cssText = `display:flex;flex-direction:column;gap:8px;`;

        listContainer.appendChild(lutList);
        menu.appendChild(listContainer);

        const form = document.createElement('div');
        form.style.cssText = `
            border-top: 1px solid rgba(255, 138, 0, 0.35);
            padding-top: 12px;
            display:flex;
            flex-direction: column;
            gap: 10px;
        `;

        const nameRow = document.createElement('div');
        nameRow.style.cssText = `display:flex;gap:8px;align-items:center;flex-wrap:wrap;`;

        const nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.placeholder = 'Profile name...';
        nameInput.style.cssText = `
            flex: 1;
            min-width: 220px;
            background: rgba(0,0,0,0.5);
            border: 1px solid rgba(255,255,255,0.2);
            border-radius: 8px;
            padding: 10px 12px;
            color: #fff;
            font-size: 14px;
            outline: none;
        `;
        stopEventsOn(nameInput);


        const groupSelect = document.createElement('select');
        groupSelect.id = 'gvf-lut-group-select';
        groupSelect.title = 'Assign group';
        groupSelect.style.cssText = `
            width: 160px;
            background: rgba(30,30,30,0.9);
            color: #eaeaea;
            border: 1px solid rgba(255,255,255,0.2);
            border-radius: 8px;
            padding: 6px 8px;
            font-size: 12px;
            font-weight: 900;
            cursor: pointer;
        `;
        stopEventsOn(groupSelect);

        const rebuildGroupSelect = () => {
            while (groupSelect.firstChild) groupSelect.removeChild(groupSelect.firstChild);

            const optUng = document.createElement('option');
            optUng.value = '';
            optUng.textContent = 'Ungrouped';
            groupSelect.appendChild(optUng);

            const names = getAllGroupNames();
            for (const g of names) {
                const o = document.createElement('option');
                o.value = g;
                o.textContent = g;
                groupSelect.appendChild(o);
            }
        };

        rebuildGroupSelect();

        // extend group UI rebuilder to also refresh the selector
        _rebuildLutGroupUis = () => { rebuildGroupFilter(); rebuildGroupSelect(); };

const fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.accept = 'image/png,.cube,text/plain,application/octet-stream';
        fileInput.style.cssText = `
            flex: 1;
            min-width: 220px;
            color: #eaeaea;
            font-size: 12px;
        `;
        stopEventsOn(fileInput);

        fileInput.addEventListener('change', async () => {
            try {
                const f = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
                if (!f) return;

                const lower = String(f.name || '').toLowerCase();
                if (lower.endsWith('.cube')) {
                    const N = 11;
                    const conv = await cubeFileToMatrix4x5(f, { samplesPerAxis: N, linearizeIn: false, delinearizeOut: false });
                    matrixArea.value = matrixCopyNoBrackets(conv.matrix4x5);
                    if (!nameInput.value.trim()) nameInput.value = f.name.replace(/\.[^.]+$/, '');
                    log('CUBE -> 4x5 matrix generated:', f.name, `size=${conv.size}`);
                } else {
                    // PNG: keep current behavior (fill matrix area for convenience)
                    const conv = await pngFileToMatrix4x5(f, { samplesPerAxis: 11, flipY: false, linearizeIn: false, delinearizeOut: false });
                    matrixArea.value = matrixCopyNoBrackets(conv.matrix4x5);
                    if (!nameInput.value.trim()) nameInput.value = f.name.replace(/\.[^.]+$/, '');
                    log('PNG -> 4x5 matrix generated:', f.name, `(${conv.width}x${conv.height})`, `lutSize=${conv.layout.lutSize}`);
                }
            } catch (e) {
                logW('LUT file convert failed:', e);
            }
        });


        const matrixArea = document.createElement('textarea');
        matrixArea.placeholder = 'Or paste a 4x5 row-major matrix here (20 numbers) or JSON {matrix4x5:[...]}...';
        matrixArea.style.cssText = `
            width: 100%;
            min-height: 110px;
            resize: vertical;
            background: rgba(0,0,0,0.5);
            border: 1px solid rgba(255,255,255,0.2);
            border-radius: 8px;
            padding: 10px 12px;
            color: #fff;
            font-size: 12px;
            outline: none;
            line-height: 1.35;
            font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
        `;
        stopEventsOn(matrixArea);

        const helpRow = document.createElement('div');
        helpRow.style.cssText = 'display:flex;gap:8px;align-items:center;flex-wrap:wrap;';
        const clearBtn = document.createElement('button');
        clearBtn.type = 'button';
        clearBtn.textContent = 'Clear';
        clearBtn.style.cssText = `
            cursor:pointer;
            padding: 6px 10px;border-radius: 10px;
            border: 1px solid rgba(255,255,255,0.14);
            background: rgba(255,255,255,0.10);
            color: #fff;
            font-weight: 900;
            font-size: 12px;
        `;
        stopEventsOn(clearBtn);
        clearBtn.addEventListener('click', () => { matrixArea.value = ''; });

        const loadActiveBtn = document.createElement('button');
        loadActiveBtn.type = 'button';
        loadActiveBtn.textContent = 'Load Active';
        loadActiveBtn.style.cssText = `
            cursor:pointer;
            padding: 6px 10px;border-radius: 10px;
            border: 1px solid rgba(255,255,255,0.14);
            background: rgba(255, 138, 0, 0.18);
            color: #ffd7a6;
            font-weight: 900;
            font-size: 12px;
        `;
        stopEventsOn(loadActiveBtn);
        loadActiveBtn.addEventListener('click', () => {
            const activeName = (activeLutProfileKey && activeLutProfileKey !== 'none') ? lutParseKey(activeLutProfileKey).name : '';
            if (!activeName || activeName === 'none') { alert('No active LUT profile.'); return; }
            const p = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => String(x.name) === activeName);
            if (!p || !Array.isArray(p.matrix4x5) || p.matrix4x5.length !== 20) { alert('Active LUT profile has no valid matrix.'); return; }
            matrixArea.value = p.matrix4x5.join(' ');
            nameInput.value = activeName;
        });


        helpRow.appendChild(loadActiveBtn);
        helpRow.appendChild(clearBtn);


        const saveBtn = document.createElement('button');
        saveBtn.type = 'button';
        saveBtn.textContent = 'Save / Replace';
        saveBtn.style.cssText = `
            background: #ff8a00;
            border: none;
            color: #111;
            padding: 10px 14px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 900;
            cursor: pointer;
        `;
        stopEventsOn(saveBtn);

        const hint = document.createElement('div');
        hint.textContent = 'Upload a PNG LUT (e.g. 512×512). Same names will be overwritten.';
        hint.style.cssText = `font-size: 12px; color: rgba(255,255,255,0.75);`;

        saveBtn.addEventListener('click', async () => {
            const name = String(nameInput.value || '').trim();
            const file = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
            const raw = String(matrixArea.value || '').trim();

            if (!name) { alert('Please enter a profile name.'); return; }

            const parseMatrix4x5 = (input) => {
                const s = String(input || '').trim();
                if (!s) return null;

                // Try JSON first
                if (s[0] === '{' || s[0] === '[') {
                    try {
                        const obj = JSON.parse(s);
                        if (Array.isArray(obj) && obj.length === 20) return obj.map(Number);
                        if (obj && Array.isArray(obj.matrix4x5) && obj.matrix4x5.length === 20) return obj.matrix4x5.map(Number);
                        if (obj && Array.isArray(obj.matrix4x5) && obj.matrix4x5.length === 4 && obj.matrix4x5.every(r => Array.isArray(r) && r.length === 5)) {
                            return obj.matrix4x5.flat().map(Number);
                        }
                    } catch (_) { /* ignore */ }
                }

                // Parse numbers from text (commas/spaces/newlines)
                const nums = s.replace(/\[/g, ' ')
                              .replace(/\]/g, ' ')
                              .replace(/,/g, ' ')
                              .trim()
                              .split(/\s+/)
                              .filter(Boolean)
                              .map((v) => Number(v));

                if (nums.length !== 20 || nums.some((n) => !Number.isFinite(n))) return null;
                return nums;
            };

            try {
                // Priority: manual matrix textarea
                let matrix = parseMatrix4x5(raw);

                if (!matrix) {
                    // Fallback: PNG conversion
                    if (!file) { alert('Please select a PNG LUT file or paste a 4x5 matrix.'); return; }
                    const lower = String(file.name || '').toLowerCase();
                    if (lower.endsWith('.cube')) {
                        const conv = await cubeFileToMatrix4x5(file, { samplesPerAxis: 11, linearizeIn: false, delinearizeOut: false });
                        matrix = conv.matrix4x5;
                        log('CUBE -> 4x5 matrix generated:', name, `size=${conv.size}`);
                    } else {
                        const conv = await pngFileToMatrix4x5(file, { samplesPerAxis: 11, flipY: false, linearizeIn: false, delinearizeOut: false });
                        matrix = conv.matrix4x5;
                        log('PNG -> 4x5 matrix generated:', name, `(${conv.width}x${conv.height})`, `lutSize=${conv.layout.lutSize}`, `tiles=${conv.layout.tilesX}x${conv.layout.tilesY}`);
                    }
                } else {
                    log('Manual 4x5 matrix saved:', name);
                }

                // Allow duplicate names across different groups, but enforce uniqueness within the same group.
                const newGroup = groupSelect.value ? String(groupSelect.value).trim() : '';
                const newKey = lutMakeKey(name, newGroup);

                const oldKey = String(nameInput.dataset.gvfLutEditKey || '').trim();
                if (oldKey) {
                    const old = lutParseKey(oldKey);
                    // If key changed (rename and/or move group), prevent collision in target group.
                    if (old.key !== newKey) {
                        const exists = (Array.isArray(lutProfiles) ? lutProfiles : []).some(p =>
                            lutKeyFromProfile(p) === newKey
                        );
                        if (exists) { alert('A profile with the same name already exists in this group.'); return; }
                        // Remove old entry first to avoid stale duplicates.
                        deleteLutProfile(old.key);
                    }
                }

                // Upsert into the target group
                upsertLutProfile({ name, group: (newGroup ? newGroup : undefined), matrix4x5: matrix });
                setActiveLutProfile(newKey);

                // Clear edit marker
                try { delete nameInput.dataset.gvfLutEditKey; } catch (_) { }

                nameInput.value = '';
                fileInput.value = '';
                matrixArea.value = '';

                try { _rebuildLutGroupUis(); } catch (_) { }
                try { if (typeof refreshLutDropdownFn === 'function') refreshLutDropdownFn(); } catch (_) { }

                updateLutProfileList();
                setActiveLutInfo();

            } catch (e) {
                logW('LUT save failed:', e);
                alert('LUT save failed. Check console for details.');
            }
        });
        nameRow.appendChild(nameInput);
        nameRow.appendChild(groupSelect);
        nameRow.appendChild(fileInput);
        nameRow.appendChild(saveBtn);

        form.appendChild(nameRow);
        form.appendChild(helpRow);
        form.appendChild(matrixArea);
        form.appendChild(hint);
        menu.appendChild(form);

        function updateLutProfileListInner() {
            const container = menu.querySelector('#gvf-lut-profile-list');
            if (!container) return;

            while (container.firstChild) container.removeChild(container.firstChild);

            try { _rebuildLutGroupUis(); } catch (_) { }

            const list = Array.isArray(lutProfiles) ? lutProfiles.slice() : [];
            list.sort((a, b) => String(a.name).localeCompare(String(b.name)));

            // Group filter
            const gf = String((groupFilter && (groupFilter.dataset.gvfValue || groupFilter.value)) || '__all__');
            let filtered = list;
            if (gf === '__ungrouped__') {
                filtered = list.filter(p => !(p && p.group && String(p.group).trim()));
            } else if (gf !== '__all__') {
                filtered = list.filter(p => (p && p.group && String(p.group).trim() === gf));
            }

            if (filtered.length === 0) {
                const empty = document.createElement('div');
                empty.textContent = 'No LUT profiles yet.';
                empty.style.cssText = 'opacity:0.7;font-size:13px;padding:6px;';
                container.appendChild(empty);
                return;
            }

            // Capture ONE shared raw frame (320x180) for all profiles — fast, done once
            const sharedRaw = captureLutPreviewFrame(); // ImageData 320x180 or null

            const mkBtn = (text, bg, fg) => {
                const b = document.createElement('button');
                b.type = 'button';
                b.textContent = text;
                b.style.cssText = `
                    cursor:pointer;
                    padding: 6px 10px;border-radius: 10px;
                    border: 1px solid rgba(255,255,255,0.14);
                    background: ${bg};
                    color: ${fg};
                    font-weight: 900;
                    font-size: 12px;
                `;
                stopEventsOn(b);
                return b;
            };

            const lutSlideshowEntries = []; // for slideshow across all rendered LUT profiles
            for (const p of filtered) {
                const row = document.createElement('div');
                row.style.cssText = `
                    display:flex;align-items:center;gap:12px;
                    padding: 10px 10px;border-radius: 10px;
                    background: rgba(0,0,0,0.35);
                    border: 1px solid rgba(255,255,255,0.12);
                `;

                const left = document.createElement('div');
                left.style.cssText = 'display:flex;flex-direction:column;gap:2px;flex:1;min-width:120px;overflow:hidden;';

                const nm = document.createElement('div');
                nm.textContent = String(p.name);
                nm.style.cssText = `font-weight:900;font-size:14px;color:#fff;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;`;

                const meta = document.createElement('div');
                const isActive = (lutKeyFromProfile(p) === String(activeLutProfileKey));
                meta.textContent = isActive ? 'Active' : '';
                meta.style.cssText = isActive ? 'font-size:12px;color:#ffb35a;font-weight:900;' : 'font-size:12px;opacity:0.7;';

                left.appendChild(nm);
                left.appendChild(meta);

                const grp = (p && p.group && String(p.group).trim()) ? String(p.group).trim() : '';
                if (grp) {
                    const gEl = document.createElement('div');
                    gEl.textContent = 'Group: ' + grp;
                    gEl.style.cssText = 'font-size:12px;opacity:0.75;';
                    left.appendChild(gEl);
                }

                // --- LUT Preview Canvas (thumbnail 160x90, built once in background) ---
                const PW = 160, PH = 90;
                const previewWrap = document.createElement('div');
                previewWrap.style.cssText = `
                    flex-shrink:0;border-radius:8px;overflow:hidden;
                    border:1px solid rgba(255,138,0,0.35);
                    width:${PW}px;height:${PH}px;position:relative;
                    background:#111;cursor:zoom-in;
                `;
                const thumbCanvas = document.createElement('canvas');
                thumbCanvas.width = PW; thumbCanvas.height = PH;
                thumbCanvas.style.cssText = `display:block;width:${PW}px;height:${PH}px;`;
                previewWrap.appendChild(thumbCanvas);

                // Big canvas for lightbox (1280x720) — pre-rendered, never re-rendered
                const LW = 1280, LH = 720;
                const bigCanvas = document.createElement('canvas');
                bigCanvas.width = LW; bigCanvas.height = LH;
                bigCanvas.style.cssText = 'display:block;width:auto;height:auto;max-width:90vw;max-height:65vh;border-radius:10px;';
                let bigReady = false;

                const lutEntry = { bigCanvas, get bigReady() { return bigReady; }, label: '🎨 ' + String(p.name) + (grp ? '  ·  ' + grp : '') };
                lutSlideshowEntries.push(lutEntry);

                // Build both canvases in background — LUT applied async in chunks, no main-thread block
                setTimeout(() => {
                    try {
                    if (!sharedRaw) return; // sharedRaw is already 1280x720

                    const applyAndRender = (id) => {
                        try {
                            // Write LUT-processed frame into bigCanvas (1280x720)
                            const bCtx = bigCanvas.getContext('2d', { alpha: false });
                            if (bCtx) {
                                bCtx.putImageData(id, 0, 0);
                                bigReady = true;
                            }
                            // Scale down to thumbnail 160x90
                            const tCtx = thumbCanvas.getContext('2d', { alpha: false });
                            if (tCtx) {
                                tCtx.imageSmoothingEnabled = true;
                                try { tCtx.imageSmoothingQuality = 'high'; } catch(_) {}
                                tCtx.drawImage(bigCanvas, 0, 0, PW, PH);
                            }
                        } catch(_) {}
                    };

                    // Clone sharedRaw so each profile gets its own copy to mutate
                    const idCopy = new ImageData(
                        new Uint8ClampedArray(sharedRaw.data),
                        sharedRaw.width, sharedRaw.height
                    );

                    if (Array.isArray(p.matrix4x5) && p.matrix4x5.length === 20) {
                        applyLut4x5Async(idCopy, p.matrix4x5, applyAndRender);
                    } else {
                        applyAndRender(idCopy);
                    }

                    } catch(_) {}
                }, 0);

                // Slideshow lightbox
                previewWrap.addEventListener('click', (e) => {
                    e.stopPropagation();
                    openSlideshow(lutSlideshowEntries, lutSlideshowEntries.indexOf(lutEntry), '#ff8a00');
                });
                stopEventsOn(previewWrap);
                // --- end LUT Preview Canvas ---

                const right = document.createElement('div');
                right.style.cssText = 'display:flex;gap:8px;align-items:center;';

                const useBtn = mkBtn('Use', 'rgba(255, 138, 0, 0.18)', '#ffd7a6');
                useBtn.addEventListener('click', () => {
                    setActiveLutProfile(lutKeyFromProfile(p));
                    setActiveLutInfo();
                    updateLutProfileListInner();
                });

                const editBtn = mkBtn('Edit', 'rgba(255, 255, 255, 0.10)', '#ffffff');
                editBtn.addEventListener('click', () => {
                    nameInput.value = String(p.name);
                    try { nameInput.dataset.gvfLutEditKey = lutKeyFromProfile(p); } catch (_) { }
                    fileInput.value = '';
                    try { groupSelect.value = (p && p.group) ? String(p.group).trim() : ''; } catch (_) { }
                    if (Array.isArray(p.matrix4x5) && p.matrix4x5.length === 20) {
                        matrixArea.value = p.matrix4x5.join(' ');
                    } else {
                        matrixArea.value = '';
                    }
                });

                const delBtn = mkBtn('Delete', 'rgba(255, 68, 68, 0.20)', '#ffd0d0');
                delBtn.addEventListener('click', () => {
                    deleteLutProfile(lutKeyFromProfile(p));
                    setActiveLutInfo();
                    updateLutProfileListInner();
                });

                right.appendChild(useBtn);
                right.appendChild(editBtn);
                right.appendChild(delBtn);

                row.appendChild(previewWrap);
                row.appendChild(left);
                row.appendChild(right);
                container.appendChild(row);
            }
        }

        menu._gvfUpdateLutProfileList = updateLutProfileListInner;
        menu._gvfSetActiveLutInfo = setActiveLutInfo;

        try { updateLutProfileListInner(); } catch(_) {}

        const _fsElLut = getFsEl();
        (_fsElLut || document.body || document.documentElement).appendChild(menu);
        applyManagerPosition(menu, K.LUT_PROFILE_MANAGER_POS);
        return menu;
    }

    function updateLutProfileList() {
        const menu = document.getElementById(LUT_CONFIG_MENU_ID);
        if (menu && typeof menu._gvfUpdateLutProfileList === 'function') menu._gvfUpdateLutProfileList();
    }

    function toggleLutConfigMenu() {
        lutConfigMenuVisible = !lutConfigMenuVisible;
        const menu = document.getElementById(LUT_CONFIG_MENU_ID);

        if (!menu) {
            const newMenu = createLutConfigMenu();
            if (lutConfigMenuVisible) {
                setTimeout(() => {
                    updateLutProfileList();
                    if (typeof newMenu._gvfSetActiveLutInfo === 'function') newMenu._gvfSetActiveLutInfo();
                    newMenu.style.display = 'flex';
                }, 10);
            }
            return;
        }

        if (lutConfigMenuVisible) {
            updateLutProfileList();
            if (typeof menu._gvfSetActiveLutInfo === 'function') menu._gvfSetActiveLutInfo();
            menu.style.display = 'flex';
        } else {
            menu.style.display = 'none';
        }
    }

        // -------------------------


    // -------------------------
    // Overlay infrastructure
    // -------------------------
    const overlaysMain = new WeakMap();
    const overlaysGrade = new WeakMap();
    const overlaysIO = new WeakMap();
    const overlaysScopes = new WeakMap();
    let rafScheduled = false;

    function getFsEl() {
        return document.fullscreenElement
            || document.webkitFullscreenElement
            || document.mozFullScreenElement
            || document.msFullscreenElement
            || null;
    }

    function stopEventsOn(el) {
        const stop = (e) => { e.stopPropagation(); };
        [
            'pointerdown', 'pointerup', 'pointermove',
            'mousedown', 'mouseup', 'mousemove',
            'touchstart', 'touchmove', 'touchend',
            'wheel'
        ].forEach(ev => el.addEventListener(ev, stop, { passive: true }));
    }


    function readManagerPosition(key) {
        try {
            const raw = gmGet(key, null);
            if (raw && typeof raw === 'object') {
                const x = Number(raw.x);
                const y = Number(raw.y);
                if (Number.isFinite(x) && Number.isFinite(y)) return { x, y };
            }
        } catch (_) { }
        try {
            const raw = localStorage.getItem(key);
            if (!raw) return null;
            const parsed = JSON.parse(raw);
            const x = Number(parsed && parsed.x);
            const y = Number(parsed && parsed.y);
            if (Number.isFinite(x) && Number.isFinite(y)) return { x, y };
        } catch (_) { }
        return null;
    }

    function writeManagerPosition(key, pos) {
        if (!pos || !Number.isFinite(Number(pos.x)) || !Number.isFinite(Number(pos.y))) return;
        const safe = { x: Math.round(Number(pos.x)), y: Math.round(Number(pos.y)) };
        try { gmSet(key, safe); } catch (_) { }
        try { localStorage.setItem(key, JSON.stringify(safe)); } catch (_) { }
    }

    function clampManagerPosition(menu, x, y) {
        const rect = (menu && typeof menu.getBoundingClientRect === 'function') ? menu.getBoundingClientRect() : null;
        const width = rect && rect.width ? rect.width : (menu ? menu.offsetWidth : 0);
        const height = rect && rect.height ? rect.height : (menu ? menu.offsetHeight : 0);
        const vw = Math.max(document.documentElement ? document.documentElement.clientWidth : 0, window.innerWidth || 0);
        const vh = Math.max(document.documentElement ? document.documentElement.clientHeight : 0, window.innerHeight || 0);
        const margin = 12;
        const maxX = Math.max(margin, vw - width - margin);
        const maxY = Math.max(margin, vh - height - margin);
        return {
            x: clamp(Number(x) || margin, margin, maxX),
            y: clamp(Number(y) || margin, margin, maxY)
        };
    }

    function applyManagerPosition(menu, storageKey) {
        if (!menu) return;
        const stored = readManagerPosition(storageKey);
        const pos = stored
            ? clampManagerPosition(menu, stored.x, stored.y)
            : clampManagerPosition(menu, (window.innerWidth - menu.offsetWidth) / 2, (window.innerHeight - menu.offsetHeight) / 2);
        menu.style.left = pos.x + 'px';
        menu.style.top = pos.y + 'px';
        menu.style.transform = 'none';
    }

    function makeFloatingManagerDraggable(menu, header, storageKey) {
        if (!menu || !header || !storageKey) return;
        header.style.cursor = 'move';
        header.style.touchAction = 'none';

        let dragState = null;

        const finishDrag = () => {
            if (!dragState) return;
            const pos = clampManagerPosition(menu, parseFloat(menu.style.left), parseFloat(menu.style.top));
            menu.style.left = pos.x + 'px';
            menu.style.top = pos.y + 'px';
            menu.style.transform = 'none';
            writeManagerPosition(storageKey, pos);
            dragState = null;
        };

        const onPointerMove = (e) => {
            if (!dragState) return;
            const clientX = Number(e && e.clientX);
            const clientY = Number(e && e.clientY);
            if (!Number.isFinite(clientX) || !Number.isFinite(clientY)) return;
            const pos = clampManagerPosition(menu, clientX - dragState.offsetX, clientY - dragState.offsetY);
            menu.style.left = pos.x + 'px';
            menu.style.top = pos.y + 'px';
            menu.style.transform = 'none';
        };

        const onPointerUp = () => finishDrag();

        header.addEventListener('pointerdown', (e) => {
            if (!e || e.button !== 0) return;
            const target = e.target;
            if (target && typeof target.closest === 'function' && target.closest('button, input, select, textarea, a, label')) return;
            const rect = menu.getBoundingClientRect();
            dragState = {
                offsetX: e.clientX - rect.left,
                offsetY: e.clientY - rect.top
            };
            menu.style.transform = 'none';
            menu.style.left = rect.left + 'px';
            menu.style.top = rect.top + 'px';
            try { header.setPointerCapture(e.pointerId); } catch (_) { }
            e.preventDefault();
            e.stopPropagation();
        });

        header.addEventListener('pointermove', onPointerMove);
        header.addEventListener('pointerup', onPointerUp);
        header.addEventListener('pointercancel', onPointerUp);
        window.addEventListener('resize', () => {
            const pos = clampManagerPosition(menu, parseFloat(menu.style.left), parseFloat(menu.style.top));
            menu.style.left = pos.x + 'px';
            menu.style.top = pos.y + 'px';
            menu.style.transform = 'none';
            writeManagerPosition(storageKey, pos);
        });
    }

    function mkMainOverlay() {
        const overlay = document.createElement('div');
        overlay.className = 'gvf-video-overlay-main';
        overlay.style.cssText = `
      position: fixed;
      display: none;
      flex-direction: column;
      gap: 6px;
      margin-top: 15px;
      z-index: 2147483647;
      pointer-events: auto;
      opacity: 0.92;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
      transform: translateZ(0);
      user-select: none;
    `;

        const top = document.createElement('div');
        top.style.cssText = `display:flex;align-items:center;justify-content: space-between;gap: 8px;`;

        const row = document.createElement('div');
        row.style.cssText = `display:flex; gap:6px; align-items:center;`;

        const profBadge = document.createElement('div');
        profBadge.className = 'gvf-prof-badge';
        profBadge.style.cssText = `
      padding: 4px 8px;border-radius: 10px;font-size: 11px;font-weight: 900;
      background: rgba(0,0,0,0.92);color: #eaeaea;
      box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;white-space: nowrap;
    `;

        const renderBadge = document.createElement('div');
        renderBadge.className = 'gvf-render-badge';
        renderBadge.style.cssText = `
      padding: 2px 6px;border-radius: 8px;font-size: 9px;font-weight: 900;
      background: rgba(0,0,0,0.92);color: #ffaa00;
      box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
      margin-left: 4px;
    `;
        renderBadge.textContent = renderMode === 'gpu' ? 'GPU' : 'SVG';

        const mkBtn = (key, label) => {
            const el = document.createElement('div');
            el.dataset.key = key;
            el.textContent = label;
            el.style.cssText = `
        width: 24px;height: 24px;border-radius: 6px;background: #000;color: #666;
        display:flex;align-items:center;justify-content:center;
        font-size: 11px;font-weight: 800;
        box-shadow: 0 0 0 1px rgba(255,255,255,0.18) inset;
        text-shadow: 0 1px 1px rgba(0,0,0,0.6);
      `;
            return el;
        };

        row.appendChild(mkBtn('base', 'B'));
        row.appendChild(mkBtn('moody', 'D'));
        row.appendChild(mkBtn('teal', 'O'));
        row.appendChild(mkBtn('vib', 'V'));
        row.appendChild(mkBtn('hdr', 'P'));
        row.appendChild(mkBtn('auto', 'A'));
        top.appendChild(row);

        const badgeRow = document.createElement('div');

        badgeRow.style.cssText = `display:flex;align-items:center;gap:4px;`;
        badgeRow.appendChild(profBadge);
        badgeRow.appendChild(renderBadge);
        top.appendChild(badgeRow);

        overlay.appendChild(top);

        const mkSliderRow = (name, labelText, min, max, step, getVal, setVal, gmKey, snapZero, fmt = v => Number(v).toFixed(1)) => {
            const wrap = document.createElement('div');
            wrap.style.cssText = `
        display:flex;align-items:center;gap:8px;padding: 6px 8px;border-radius: 10px;
        background: rgba(0,0,0,0.92);box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
      `;

            const lbl = document.createElement('div');
            lbl.textContent = labelText;
            lbl.style.cssText = `min-width: 36px;text-align:center;font-size: 11px;font-weight: 900;color:#cfcfcf;`;

            const rng = document.createElement('input');
            rng.type = 'range';
            rng.min = String(min);
            rng.max = String(max);
            rng.step = String(step);
            rng.value = String(getVal());
            rng.dataset.gvfRange = name;
            rng.style.cssText = `width: 210px; height: 18px; accent-color: #fff;`;

            const val = document.createElement('div');
            val.dataset.gvfVal = name;
            val.textContent = fmt(getVal());
            val.style.cssText = `width: 52px;text-align:right;font-size: 11px;font-weight: 900;color:#e6e6e6;`;

            stopEventsOn(rng);

            rng.addEventListener('input', () => {
                let v = clamp(parseFloat(rng.value), min, max);
                if (snapZero) v = snap0(v, Math.max(0.005, Number(step) / 2));
                v = roundTo(v, step);

                setVal(v);
                rng.value = String(getVal());
                val.textContent = fmt(getVal());

                gmSet(gmKey, getVal());
                if (gmKey === K.HDR && getVal() !== 0) gmSet(K.HDR_LAST, getVal());

                // Save current settings in active profile
                updateCurrentProfileSettings();

                if (renderMode === 'gpu') {
                    applyGpuFilter();
                } else {
                    regenerateSvgImmediately();
                }
            });

            wrap.appendChild(lbl);
            wrap.appendChild(rng);
            wrap.appendChild(val);
            return wrap;
        };

        overlay.appendChild(mkSliderRow('SL', 'SL', -2, 2, 0.01, () => normSL(), (v) => { sl = v; }, K.SL, true, v => Number(v).toFixed(2)));
        overlay.appendChild(mkSliderRow('SR', 'SR', -2, 2, 0.01, () => normSR(), (v) => { sr = v; }, K.SR, true, v => Number(v).toFixed(2)));
        overlay.appendChild(mkSliderRow('BL', 'BL', -2, 2, 0.01, () => normBL(), (v) => { bl = v; }, K.BL, true, v => Number(v).toFixed(2)));
        overlay.appendChild(mkSliderRow('WL', 'WL', -2, 2, 0.01, () => normWL(), (v) => { wl = v; }, K.WL, true, v => Number(v).toFixed(2)));
        {
            const dnRow = mkSliderRow('DN', 'DN', -1.5, 1.5, 0.01, () => normDN(), (v) => { dn = v; }, K.DN, true, v => Number(v).toFixed(2));
            if (isFirefox()) {
                const rng = dnRow.querySelector('input[type="range"]');
                if (rng) {
                    rng.disabled = true;
                    rng.title = 'DN (Depth/Denoise) is not supported in Firefox.';
                    rng.style.opacity = '0.35';
                    rng.style.cursor = 'not-allowed';
                }
                dnRow.title = 'DN (Depth/Denoise) is not supported in Firefox.';
                dnRow.style.opacity = '0.45';
            }
            overlay.appendChild(dnRow);
        }
        overlay.appendChild(mkSliderRow('HDR', 'HDR', -1.0, 2.0, 0.01, () => normHDR(), (v) => { hdr = v; }, K.HDR, true, v => Number(v).toFixed(2)));

        (document.body || document.documentElement).appendChild(overlay);
        return overlay;
    }

    function mkGradingOverlay() {
        const overlay = document.createElement('div');
        overlay.className = 'gvf-video-overlay-grade';
        overlay.style.cssText = `
      position: fixed;display: none;flex-direction: column;gap: 6px;z-index: 2147483647;
      pointer-events: auto;opacity: 0.92;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
      transform: translateZ(0);user-select: none;
      width: 340px;
      max-height: 90vh;
      overflow-y: auto;
    `;

        const head = document.createElement('div');
        head.style.cssText = `
      display:flex;justify-content: space-between;align-items:center;
      padding: 6px 8px;border-radius: 10px;background: rgba(0,0,0,0.92);
      box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
    `;

        const title = document.createElement('div');
        title.textContent = 'Grading (G) & RGB Gain (0-255)';
        title.style.cssText = `font-size:11px; font-weight:900; color:#eaeaea;`;
        head.appendChild(title);
        overlay.appendChild(head);

        const mkRow = (name, labelText, keyGet, keySet, gmKey) => {
            const wrap = document.createElement('div');
            wrap.style.cssText = `
        display:flex;align-items:center;gap:8px;padding: 6px 8px;border-radius: 10px;
        background: rgba(0,0,0,0.92);box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
      `;

            const lbl = document.createElement('div');
            lbl.textContent = labelText;
            lbl.style.cssText = `
        min-width: 100px;text-align:left;font-size: 11px;font-weight: 900;
        color:#cfcfcf;padding-left: 2px;
      `;

            const rng = document.createElement('input');
            rng.type = 'range';
            rng.min = '-10';
            rng.max = '10';
            rng.step = '0.1';
            rng.value = String(keyGet());
            rng.dataset.gvfRange = name;
            rng.style.cssText = `width: 120px; height: 18px; accent-color: #fff;`;

            const val = document.createElement('div');
            val.dataset.gvfVal = name;
            val.textContent = Number(keyGet()).toFixed(1);
            val.style.cssText = `width: 54px;text-align:right;font-size: 11px;font-weight: 900;color:#e6e6e6;`;

            stopEventsOn(rng);

            rng.addEventListener('input', () => {
                const v = normU(parseFloat(rng.value));
                keySet(v);
                rng.value = String(keyGet());
                val.textContent = Number(keyGet()).toFixed(1);
                gmSet(gmKey, keyGet());

                // Save current settings in active profile
                updateCurrentProfileSettings();

                if (renderMode === 'gpu') {
                    applyGpuFilter();
                } else {
                    regenerateSvgImmediately();
                }
                scheduleOverlayUpdate();
            });

            wrap.appendChild(lbl);
            wrap.appendChild(rng);
            wrap.appendChild(val);
            return wrap;
        };

        const mkRGBRow = (name, labelText, keyGet, keySet, gmKey, color) => {
            const wrap = document.createElement('div');
            wrap.style.cssText = `
        display:flex;align-items:center;gap:8px;padding: 6px 8px;border-radius: 10px;
        background: rgba(0,0,0,0.92);box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
      `;

            const lbl = document.createElement('div');
            lbl.textContent = labelText;
            lbl.style.cssText = `
        min-width: 100px;text-align:left;font-size: 11px;font-weight: 900;
        color:${color};padding-left: 2px;
      `;

            const rng = document.createElement('input');
            rng.type = 'range';
            rng.min = '0';
            rng.max = '255';
            rng.step = '1';
            rng.value = String(keyGet());
            rng.dataset.gvfRange = name;
            rng.style.cssText = `width: 120px; height: 18px; accent-color: ${color};`;

            const val = document.createElement('div');
            val.dataset.gvfVal = name;
            val.textContent = String(Math.round(keyGet()));
            val.style.cssText = `width: 54px;text-align:right;font-size: 11px;font-weight: 900;color:${color};`;

            stopEventsOn(rng);

            rng.addEventListener('input', () => {
                const v = normRGB(parseFloat(rng.value));
                keySet(v);
                rng.value = String(keyGet());
                val.textContent = String(Math.round(keyGet()));
                gmSet(gmKey, keyGet());

                // Save current settings in active profile
                updateCurrentProfileSettings();

                if (renderMode === 'gpu') {
                    applyGpuFilter();
                } else {
                    regenerateSvgImmediately();
                }
                scheduleOverlayUpdate();
            });

            wrap.appendChild(lbl);
            wrap.appendChild(rng);
            wrap.appendChild(val);
            return wrap;
        };

        overlay.appendChild(mkRow('U_CONTRAST', 'Contrast', () => normU(u_contrast), (v) => { u_contrast = v; }, K.U_CONTRAST));
        overlay.appendChild(mkRow('U_BLACK', 'Black Level', () => normU(u_black), (v) => { u_black = v; }, K.U_BLACK));
        overlay.appendChild(mkRow('U_WHITE', 'White Level', () => normU(u_white), (v) => { u_white = v; }, K.U_WHITE));
        overlay.appendChild(mkRow('U_HIGHLIGHTS', 'Highlights', () => normU(u_highlights), (v) => { u_highlights = v; }, K.U_HIGHLIGHTS));
        overlay.appendChild(mkRow('U_SHADOWS', 'Shadows', () => normU(u_shadows), (v) => { u_shadows = v; }, K.U_SHADOWS));
        overlay.appendChild(mkRow('U_SAT', 'Saturation', () => normU(u_sat), (v) => { u_sat = v; }, K.U_SAT));
        overlay.appendChild(mkRow('U_VIB', 'Vibrance', () => normU(u_vib), (v) => { u_vib = v; }, K.U_VIB));
        overlay.appendChild(mkRow('U_SHARP', 'Sharpen', () => normU(u_sharp), (v) => { u_sharp = v; }, K.U_SHARP));
        overlay.appendChild(mkRow('U_GAMMA', 'Gamma', () => normU(u_gamma), (v) => { u_gamma = v; }, K.U_GAMMA));
        overlay.appendChild(mkRow('U_GRAIN', 'Grain (Banding)', () => normU(u_grain), (v) => { u_grain = v; }, K.U_GRAIN));
        overlay.appendChild(mkRow('U_HUE', 'Hue Correction', () => normU(u_hue), (v) => { u_hue = v; }, K.U_HUE));

        const sep = document.createElement('div');
        sep.style.cssText = `height:1px;background:rgba(255,255,255,0.14);margin:8px 0;`;
        overlay.appendChild(sep);

        overlay.appendChild(mkRGBRow('U_R_GAIN', 'R Gain (0-255)', () => normRGB(u_r_gain), (v) => { u_r_gain = v; }, K.U_R_GAIN, '#ff6b6b'));
        overlay.appendChild(mkRGBRow('U_G_GAIN', 'G Gain (0-255)', () => normRGB(u_g_gain), (v) => { u_g_gain = v; }, K.U_G_GAIN, '#6bff6b'));
        overlay.appendChild(mkRGBRow('U_B_GAIN', 'B Gain (0-255)', () => normRGB(u_b_gain), (v) => { u_b_gain = v; }, K.U_B_GAIN, '#6b6bff'));

        // Add color blindness filter dropdown
        const cbSep = document.createElement('div');
        cbSep.style.cssText = `height:1px;background:rgba(255,255,255,0.14);margin:8px 0;`;
        overlay.appendChild(cbSep);

        const cbSection = document.createElement('div');
        cbSection.style.cssText = `
      display:flex;align-items:center;gap:8px;padding: 6px 8px;border-radius: 10px;
      background: rgba(0,0,0,0.92);box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
      margin-top: 4px;
    `;

        const cbLabel = document.createElement('div');
        cbLabel.textContent = 'Color Blind';
        cbLabel.style.cssText = `
      min-width: 100px;text-align:left;font-size: 11px;font-weight: 900;
      color:#cfcfcf;padding-left: 2px;
    `;

        const cbSelect = document.createElement('select');
        cbSelect.dataset.gvfSelect = 'cb_filter';
        cbSelect.style.cssText = `
      width: 120px;background: rgba(30,30,30,0.9);color: #eaeaea;
      border: 1px solid rgba(255,255,255,0.2);border-radius: 6px;
      padding: 4px;font-size: 11px;font-weight: 900;cursor: pointer;
    `;

        const options = [
            { value: 'none', text: 'None' },
            { value: 'protanopia', text: 'Protanopia (Red-Green)' },
            { value: 'deuteranopia', text: 'Deuteranopia (Green-Red)' },
            { value: 'tritanomaly', text: 'Tritanomaly (Blue-Yellow)' }
        ];

        options.forEach(opt => {
            const option = document.createElement('option');
            option.value = opt.value;
            option.textContent = opt.text;
            if (opt.value === cbFilter) {
                option.selected = true;
            }
            cbSelect.appendChild(option);
        });

        stopEventsOn(cbSelect);

        cbSelect.addEventListener('change', () => {
            const newVal = cbSelect.value;
            if (newVal === cbFilter) return;

            cbFilter = newVal;
            gmSet(K.CB_FILTER, cbFilter);
            log('Color blindness filter:', cbFilter);

            // Save current settings in active profile
            updateCurrentProfileSettings();

            if (renderMode === 'gpu') {
                applyGpuFilter();
            } else {
                regenerateSvgImmediately();
            }
            scheduleOverlayUpdate();
        });

        const cbHint = document.createElement('div');

        cbSection.appendChild(cbLabel);
        cbSection.appendChild(cbSelect);
        cbSection.appendChild(cbHint);
        overlay.appendChild(cbSection);

        // Add LUT dropdown + manager button (below Color Blind)
            const lutSection = document.createElement('div');
            lutSection.style.cssText = `
          display:flex;align-items:center;gap:8px;padding: 6px 8px;border-radius: 10px;
          background: rgba(0,0,0,0.92);box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
          margin-top: 6px;
        `;

            const lutLabel = document.createElement('div');
            lutLabel.textContent = 'LUT';
            lutLabel.style.cssText = `
          min-width: 100px;text-align:left;font-size: 11px;font-weight: 900;
          color:#cfcfcf;padding-left: 2px;
        `;

            const lutSelect = document.createElement('select');
            lutSelect.dataset.gvfSelect = 'lut_profile';
            lutSelect.style.cssText = `
          width: 180px;background: rgba(30,30,30,0.9);color: #eaeaea;
          border: 1px solid rgba(255,255,255,0.2);border-radius: 6px;
          padding: 4px;font-size: 11px;font-weight: 900;cursor: pointer;
        `;

            const lutPlus = document.createElement('button');
            lutPlus.type = 'button';
            lutPlus.textContent = '+';
            lutPlus.title = 'Open LUT Profile Manager';
            lutPlus.style.cssText = `
          width: 28px;height: 24px;display:flex;align-items:center;justify-content:center;
          border-radius: 6px;cursor:pointer;
          background: rgba(255, 138, 0, 0.22);
          color: #ffd7a6;
          border: 1px solid rgba(255, 138, 0, 0.55);
          font-weight: 900;
        `;

            stopEventsOn(lutSelect);
            stopEventsOn(lutPlus);


            // Expose for immediate sync/apply
            lutSelectEl = lutSelect;
            const refreshLutDropdown = () => {
                while (lutSelect.firstChild) lutSelect.removeChild(lutSelect.firstChild);

                const optNone = document.createElement('option');
                optNone.value = 'none';
                optNone.textContent = 'None';
                lutSelect.appendChild(optNone);

                const list = Array.isArray(lutProfiles) ? lutProfiles.slice() : [];
                const normGroup = (g) => {
                    const s = String(g || '').trim();
                    return s ? s : '';
                };

                // group -> profiles
                const groups = new Map();
                for (const p of list) {
                    const g = normGroup(p && p.group);
                    if (!groups.has(g)) groups.set(g, []);
                    groups.get(g).push(p);
                }

                // sort groups + members
                const groupNames = Array.from(groups.keys()).sort((a, b) => {
                    if (a === '' && b !== '') return 1; // ungrouped last
                    if (b === '' && a !== '') return -1;
                    return a.localeCompare(b);
                });

                for (const g of groupNames) {
                    const arr = groups.get(g) || [];
                    arr.sort((a, b) => String(a.name).localeCompare(String(b.name)));

                    if (g) {
                        const og = document.createElement('optgroup');
                        og.label = g;
                        for (const p of arr) {
                            const o = document.createElement('option');
                            o.value = lutKeyFromProfile(p);
                            o.textContent = String(p.name);
                            og.appendChild(o);
                        }
                        lutSelect.appendChild(og);
                    } else {
                        // Ungrouped
                        for (const p of arr) {
                            const o = document.createElement('option');
                            o.value = lutKeyFromProfile(p);
                            o.textContent = String(p.name);
                            lutSelect.appendChild(o);
                        }
                    }
                }

                lutSelect.value = String(activeLutProfileKey || 'none');
            };


            refreshLutDropdownFn = refreshLutDropdown;
            refreshLutDropdown();

            lutSelect.addEventListener('change', () => {
                const v = String(lutSelect.value || 'none');
                if (v === String(activeLutProfileKey || 'none')) return;
                setActiveLutProfile(v);
            });

            lutPlus.addEventListener('click', () => {
                toggleLutConfigMenu();
                refreshLutDropdown();
            });

            lutSection.appendChild(lutLabel);
            lutSection.appendChild(lutSelect);
            lutSection.appendChild(lutPlus);
            overlay.appendChild(lutSection);

        // ---- Custom SVG Codes ----
        const svgCodesSep = document.createElement('div');
        svgCodesSep.style.cssText = `height:1px;background:rgba(255,255,255,0.14);margin:8px 0;`;
        overlay.appendChild(svgCodesSep);

        const svgCodesRow = document.createElement('div');
        svgCodesRow.style.cssText = `display:flex;align-items:center;gap:8px;padding:6px 8px;border-radius:10px;background:rgba(0,0,0,0.92);box-shadow:0 0 0 1px rgba(255,255,255,0.14) inset;margin-top:4px;`;

        const svgCodesLabel = document.createElement('div');
        svgCodesLabel.textContent = 'SVG/WebGL Codes';
        svgCodesLabel.style.cssText = `min-width:100px;text-align:left;font-size:11px;font-weight:900;color:#cfcfcf;padding-left:2px;`;

        const svgCodesBtn = document.createElement('button');
        svgCodesBtn.textContent = '⬡ Manage';
        svgCodesBtn.style.cssText = `padding:4px 12px;background:rgba(100,180,255,0.18);color:#a0d4ff;border:1px solid rgba(100,180,255,0.45);border-radius:6px;font-size:11px;font-weight:900;cursor:pointer;transition:background 0.15s;`;
        svgCodesBtn.addEventListener('mouseenter', () => { svgCodesBtn.style.background = 'rgba(100,180,255,0.32)'; });
        svgCodesBtn.addEventListener('mouseleave', () => { svgCodesBtn.style.background = 'rgba(100,180,255,0.18)'; });
        stopEventsOn(svgCodesBtn);
        if (isFirefox()) {
            svgCodesBtn.disabled = true;
            svgCodesBtn.title = 'Custom Filter Codes are not supported in Firefox (WebGL2 limitations).';
            svgCodesBtn.style.opacity = '0.4';
            svgCodesBtn.style.cursor = 'not-allowed';
        } else {
            svgCodesBtn.addEventListener('click', () => openCustomSvgModal());
        }

        const svgCodesCount = document.createElement('div');
        svgCodesCount.id = 'gvf-svg-codes-count';
        svgCodesCount.style.cssText = `font-size:10px;font-weight:900;color:#6ca8ff;opacity:0.85;`;
        const activeCount = customSvgCodes.filter(e => e.enabled).length;
        svgCodesCount.textContent = customSvgCodes.length ? `${activeCount}/${customSvgCodes.length} active` : '';

        svgCodesRow.appendChild(svgCodesLabel);
        svgCodesRow.appendChild(svgCodesBtn);
        svgCodesRow.appendChild(svgCodesCount);
        overlay.appendChild(svgCodesRow);



        (document.body || document.documentElement).appendChild(overlay);
        return overlay;
    }

    function mkIOOverlay() {
        const overlay = document.createElement('div');
        overlay.className = 'gvf-video-overlay-io';
        overlay.style.cssText = `
      position: fixed;display: none;flex-direction: column;gap: 6px;z-index: 2147483647;
      pointer-events: auto;opacity: 0.95;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
      transform: translateZ(0);user-select: none;
      width: 420px;
    `;

        const head = document.createElement('div');
        head.style.cssText = `
      display:flex;justify-content: space-between;align-items:center;
      padding: 6px 8px;border-radius: 10px;background: rgba(0,0,0,0.92);
      box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
    `;

        const title = document.createElement('div');
        title.textContent = 'Settings (I) Export/Import';
        title.style.cssText = `font-size:11px; font-weight:900; color:#eaeaea;`;

        const hint = document.createElement('div');
        hint.textContent = 'JSON';
        hint.style.cssText = `font-size:10px;font-weight:900;color:#cfcfcf;opacity:0.9;`;

        head.appendChild(title);
        head.appendChild(hint);
        overlay.appendChild(head);

        const box = document.createElement('div');
        box.style.cssText = `
      padding: 8px;border-radius: 10px;background: rgba(0,0,0,0.92);
      box-shadow: 0 0 0 1px rgba(255,255,255,0.14) inset;
    `;

        const ta = document.createElement('textarea');
        ta.className = 'gvf-io-text';
        ta.spellcheck = false;
        ta.wrap = 'off';
        ta.style.cssText = `
      width: 100%;height: 220px;resize: vertical;
      background: rgba(10,10,10,0.98);color:#eaeaea;
      border: 1px solid rgba(255,255,255,0.14);
      border-radius: 10px;padding: 8px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 11px; line-height: 1.25;
      outline: none;
    `;
        stopEventsOn(ta);

        const setDirty = (on) => { if (on) ta.dataset.dirty = '1'; else delete ta.dataset.dirty; };
        let ioJsonAutoSaveTimer = null;
        let lastIoJsonApplied = '';

        ta.addEventListener('input', () => {
            setDirty(true);
            status.textContent = 'JSON changed. Waiting for valid JSON...';

            if (ioJsonAutoSaveTimer) {
                clearTimeout(ioJsonAutoSaveTimer);
                ioJsonAutoSaveTimer = null;
            }

            ioJsonAutoSaveTimer = setTimeout(() => {
                const raw = String(ta.value || '').trim();
                if (!raw) return;

                let obj = null;
                try {
                    obj = JSON.parse(raw);
                } catch (_) {
                    status.textContent = 'JSON invalid. Auto-save skipped.';
                    return;
                }

                if (raw === lastIoJsonApplied) return;

                const ok = importSettings(obj);
                if (!ok) {
                    status.textContent = 'Invalid JSON structure.';
                    return;
                }

                const changed = updateCurrentProfileSettings();
                try { updateProfileList(); } catch (_) { }

                lastIoJsonApplied = JSON.stringify(exportSettings(), null, 2);
                ta.value = lastIoJsonApplied;
                setDirty(false);

                status.textContent = changed ? 'Auto-saved + applied to active profile.' : 'No settings change detected.';
                if (changed) {
                    showScreenNotification('', {
                        title: `Profile "${String(activeUserProfile?.name || 'Default')}" auto-saved`,
                        detail: 'IO HUD JSON applied to active profile',
                        detailColor: '#4cff6a'
                    });
                }
            }, 450);
        });

        const row = document.createElement('div');
        row.style.cssText = `display:flex;gap:8px;flex-wrap:wrap;margin-top:8px;`;

        const mkBtn = (text) => {
            const b = document.createElement('button');
            b.type = 'button';
            b.textContent = text;
            b.style.cssText = `
        cursor:pointer;
        padding: 6px 10px;border-radius: 10px;
        border: 1px solid rgba(255,255,255,0.14);
        background: rgba(255,255,255,0.08);
        color:#eaeaea;font-size: 11px;font-weight: 900;
        transition: all 0.2s ease;
      `;

            // hover effect
            b.addEventListener('mouseenter', () => {
                b.style.background = 'rgba(255,255,255,0.15)';
            });
            b.addEventListener('mouseleave', () => {
                b.style.background = 'rgba(255,255,255,0.08)';
            });

            stopEventsOn(b);
            return b;
        };

        const status = document.createElement('div');
        status.className = 'gvf-io-status';
        status.style.cssText = `margin-top:8px;font-size:11px;font-weight:900;color:#cfcfcf;opacity:0.95;`;
        status.textContent = 'Tip: paste JSON here → Save';

        const btnRefresh = mkBtn('Refresh');
        const btnSave = mkBtn('Save');
        const btnSelect = mkBtn('Select All');
        const btnReset = mkBtn('Reset to defaults');
        const btnExportFile = mkBtn('Export .json');
        const btnImportFile = mkBtn('Import .json');
        const btnShot = mkBtn('Screenshot');
        const btnRec = mkBtn('Record');

        if (isFirefox()) {
            const ffMsg = 'Not supported in Firefox.';
            btnShot.disabled = true;
            btnShot.title = ffMsg;
            btnShot.style.opacity = '0.4';
            btnShot.style.cursor = 'not-allowed';
            btnRec.disabled = true;
            btnRec.title = ffMsg;
            btnRec.style.opacity = '0.4';
            btnRec.style.cursor = 'not-allowed';
        }

        // CONFIG BUTTON - Improved version
        const btnConfig = mkBtn('⚙️ Config');
        btnConfig.style.background = 'rgba(42, 111, 219, 0.4)';
        btnConfig.style.border = '2px solid #2a6fdb';
        btnConfig.style.color = '#fff';
        btnConfig.style.fontWeight = 'bold';
        btnConfig.style.padding = '6px 12px';

        // Important: Direct event listener with console.log for testing
        btnConfig.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            log('Config button clicked!');
            toggleConfigMenu();
        });

        // Debug Button
        const btnDebug = mkBtn(debug ? '🐞 Debug: ON' : '🐞 Debug: OFF');
        btnDebug.style.background = debug ? 'rgba(0,255,0,0.2)' : 'rgba(255,0,0,0.2)';
        btnDebug.style.border = debug ? '1px solid #00ff00' : '1px solid #ff0000';
        btnDebug.style.color = debug ? '#00ff00' : '#ff6666';

        btnDebug.addEventListener('click', () => {
            toggleDebug();
            btnDebug.textContent = debug ? '🐞 Debug: ON' : '🐞 Debug: OFF';
            btnDebug.style.background = debug ? 'rgba(0,255,0,0.2)' : 'rgba(255,0,0,0.2)';
            btnDebug.style.border = debug ? '1px solid #00ff00' : '1px solid #ff0000';
            btnDebug.style.color = debug ? '#00ff00' : '#ff6666';
            status.textContent = debug ? 'Debug mode activated' : 'Debug mode deactivated';
        });

        overlay.__btnRec = btnRec;
        overlay.__status = status;

        const fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.accept = 'application/json,.json';
        fileInput.style.display = 'none';
        stopEventsOn(fileInput);

        function downloadJsonToPC(obj) {
            const jsonStr = JSON.stringify(obj, null, 2);
            const blob = new Blob([jsonStr], { type: 'application/json;charset=utf-8' });
            const url = URL.createObjectURL(blob);

            const a = document.createElement('a');
            a.href = url;

            const d = new Date();
            const pad = (n) => String(n).padStart(2, '0');
            const name = `gvf-settings_${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}_${pad(d.getHours())}-${pad(d.getMinutes())}-${pad(d.getSeconds())}.json`;
            a.download = name;

            document.body.appendChild(a);
            a.click();
            a.remove();

            setTimeout(() => URL.revokeObjectURL(url), 1000);
        }

        btnExportFile.addEventListener('click', () => {
            try { downloadJsonToPC(exportSettings()); status.textContent = 'Exported to .json file.'; }
            catch (_) { status.textContent = 'Export failed.'; }
        });

        btnImportFile.addEventListener('click', () => {
            try { fileInput.value = ''; } catch (_) { }
            fileInput.click();
        });

        fileInput.addEventListener('change', () => {
            const f = fileInput.files && fileInput.files[0];
            if (!f) return;

            const reader = new FileReader();
            reader.onload = () => {
                try {
                    const raw = String(reader.result || '').trim();
                    const obj = JSON.parse(raw);
                    const ok = importSettings(obj);
                    if (!ok) { status.textContent = 'Invalid JSON structure.'; return; }

                    setDirty(false);
                    ta.value = JSON.stringify(exportSettings(), null, 2);
                    lastIoJsonApplied = ta.value;
                    status.textContent = 'Imported + applied.';
                } catch (_) {
                    status.textContent = 'Import failed (invalid JSON).';
                } finally {
                    try { fileInput.value = ''; } catch (_) { }
                }
            };
            reader.onerror = () => {
                status.textContent = 'Import failed (read error).';
                try { fileInput.value = ''; } catch (_) { }
            };
            reader.readAsText(f);
        });

        btnRefresh.addEventListener('click', () => {
            setDirty(false);
            ta.value = JSON.stringify(exportSettings(), null, 2);
            lastIoJsonApplied = ta.value;
            status.textContent = 'Exported current settings.';
        });

        btnSelect.addEventListener('click', () => { ta.focus(); ta.select(); status.textContent = 'Selected.'; });

        btnSave.addEventListener('click', () => {
            const raw = String(ta.value || '').trim();
            if (!raw) { status.textContent = 'Empty JSON.'; return; }
            try {
                const obj = JSON.parse(raw);
                const ok = importSettings(obj);
                if (!ok) { status.textContent = 'Invalid JSON structure.'; return; }

                const changed = updateCurrentProfileSettings(true);
                try { updateProfileList(); } catch (_) { }

                setDirty(false);
                ta.value = JSON.stringify(exportSettings(), null, 2);
                lastIoJsonApplied = ta.value;
                status.textContent = changed ? 'Saved + applied.' : 'No settings change detected.';
                showScreenNotification('', {
                    title: `Profile "${String(activeUserProfile?.name || 'Default')}" saved`,
                    detail: 'IO HUD JSON saved + applied',
                    detailColor: '#4cff6a'
                });
            } catch (_) {
                status.textContent = 'JSON parse error.';
            }
        });

        // Reset to defaults
        btnReset.addEventListener('click', () => {

            const firefoxDetected = isFirefox();

            let defaults;

            if (firefoxDetected) {

                defaults = {
                    enabled: true, notify: true, darkMoody: true, tealOrange: false, vibrantSat: false,
                    sl: 1.3, sr: -1.1, bl: 0.3, wl: 0.2, dn: 0.0,
                    edge: 0.0,
                    hdr: 0.0, profile: 'off',
                    renderMode: 'svg',
            lutProfile: 'none',
                    autoOn: true,
                    autoStrength: 0.65,
                    autoLockWB: true,
                    user: {
                        contrast: 0, black: 0, white: 0, highlights: 0, shadows: 0, saturation: 0, vibrance: 0, sharpen: 0, gamma: 0, grain: 0, hue: 0,
                        r_gain: 128, g_gain: 128, b_gain: 128
                    },
                    debug: false,
                    logs: true,
                    cbFilter: 'none'
                };
            } else {

                defaults = {
                    enabled: true, notify: true, darkMoody: true, tealOrange: false, vibrantSat: false,
                    sl: 1.0, sr: 0.5, bl: -1.2, wl: 0.2, dn: 0.0,
                    edge: 0.1,
                    hdr: 0.0, profile: 'user',
                    renderMode: 'svg',
            lutProfile: 'none',
                    autoOn: true,
                    autoStrength: 0.65,
                    autoLockWB: true,
                    user: {
                        contrast: 0, black: 0, white: 0, highlights: 0, shadows: 0, saturation: 0, vibrance: 0, sharpen: 0, gamma: 0, grain: 0, hue: 0,
                        r_gain: 128, g_gain: 128, b_gain: 128
                    },
                    debug: false,
                    logs: true,
                    cbFilter: 'none'
                };
            }

            importSettings(defaults);
            try { updateProfileList(); } catch (_) { }
            setDirty(true);
            ta.value = JSON.stringify(exportSettings(), null, 2);
            lastIoJsonApplied = '';
            status.textContent = 'Reset applied. Waiting for auto-save or Save.';
            showScreenNotification('', {
                title: `Profile "${String(activeUserProfile?.name || 'Default')}" reset`,
                detail: 'Defaults restored',
                detailColor: '#ffcc66'
            });

            // Update Debug button
            btnDebug.textContent = '🐞 Debug: OFF';
            btnDebug.style.background = 'rgba(255,0,0,0.2)';
            btnDebug.style.border = '1px solid #ff0000';
            btnDebug.style.color = '#ff6666';
        });

        btnShot.addEventListener('click', async () => { await takeVideoScreenshot(status); });

        btnRec.addEventListener('click', async () => {
            if (btnRec.disabled) return;
            await toggleVideoRecord(status, btnRec);
        });

        // Load Example button
        const btnLoadExample = mkBtn('⬇ Load Example');
        btnLoadExample.style.background = 'rgba(76, 255, 106, 0.12)';
        btnLoadExample.style.border = '1px solid rgba(76, 255, 106, 0.4)';
        btnLoadExample.style.color = '#fff';
        btnLoadExample.addEventListener('mouseenter', () => {
            btnLoadExample.style.background = 'rgba(76, 255, 106, 0.22)';
            btnLoadExample.style.borderColor = '#4cff6a';
        });
        btnLoadExample.addEventListener('mouseleave', () => {
            btnLoadExample.style.background = 'rgba(76, 255, 106, 0.12)';
            btnLoadExample.style.borderColor = 'rgba(76, 255, 106, 0.4)';
        });
        btnLoadExample.addEventListener('click', async () => {
            btnLoadExample.disabled = true;
            btnLoadExample.textContent = '⏳ Loading…';
            status.textContent = 'Fetching example profile…';
            try {
                const rawUrl = isFirefox()
                    ? 'https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/refs/heads/main/firefox_fix.json'
                    : 'https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/main/My_Profile.json';
                const text = await new Promise((resolve, reject) => {
                    GM_xmlhttpRequest({
                        method: 'GET',
                        url: rawUrl,
                        anonymous: true,
                        redirect: 'follow',
                        onload: (r) => r.status >= 200 && r.status < 300 ? resolve(r.responseText) : reject(new Error('HTTP ' + r.status)),
                        onerror: () => reject(new Error('Network error')),
                        ontimeout: () => reject(new Error('Timeout')),
                        timeout: 15000
                    });
                });
                const obj = JSON.parse(text.trim());
                const ok = importSettings(obj);
                if (!ok) { status.textContent = 'Load Example: invalid JSON structure.'; return; }
                updateCurrentProfileSettings(true);
                try { updateProfileList(); } catch (_) { }
                setDirty(false);
                ta.value = JSON.stringify(exportSettings(), null, 2);
                lastIoJsonApplied = ta.value;
                status.textContent = 'Example profile loaded + applied.';
                try { showValueNotification('Profile Import', 'Example profile loaded.', '#4cff6a'); } catch (_) { }
                log('Example profile loaded from GitHub.');
            } catch (err) {
                logW('Load Example failed:', err);
                status.textContent = 'Load Example failed: ' + (err && err.message ? err.message : err);
            } finally {
                btnLoadExample.disabled = false;
                btnLoadExample.textContent = '⬇ Load Example';
            }
        });

        // Add buttons in the correct order
        row.appendChild(btnRefresh);
        row.appendChild(btnSave);
        row.appendChild(btnSelect);
        row.appendChild(btnExportFile);
        row.appendChild(btnImportFile);
        row.appendChild(btnReset);
        row.appendChild(btnLoadExample);
        row.appendChild(btnShot);
        row.appendChild(btnRec);
        row.appendChild(btnConfig);  // Config Button
        row.appendChild(btnDebug);

        box.appendChild(ta);
        box.appendChild(row);
        box.appendChild(status);
        box.appendChild(fileInput);

        overlay.appendChild(box);

        // Append directly to the body
        if (document.body) {
            document.body.appendChild(overlay);
            log('IO overlay created and added to the body');
        } else {
            logW('Body not yet available');
        }

        return overlay;
    }

    function mkScopesOverlay() {
        const overlay = document.createElement('div');
        overlay.className = 'gvf-video-overlay-scopes';
        overlay.style.cssText = `
      position: fixed;
      display: none;
      flex-direction: column;
      gap: 6px;
      z-index: 2147483647;
      pointer-events: none;
      opacity: 0.95;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
      transform: translateZ(0);
      user-select: none;
      width: 280px;
    `;

        const head = document.createElement('div');
        head.style.cssText = `
      display:flex;justify-content: space-between;align-items:center;
      padding: 4px 8px;border-radius: 8px;background: rgba(0,0,0,0.85);
      box-shadow: 0 0 0 1px rgba(255,255,255,0.2) inset;
      backdrop-filter: blur(2px);
    `;

        const title = document.createElement('div');
        title.textContent = 'Scopes (S)';
        title.style.cssText = `font-size:10px; font-weight:900; color:#eaeaea;`;

        const hint = document.createElement('div');
        hint.textContent = 'live';
        hint.style.cssText = `font-size:9px;font-weight:900;color:#aaa;`;

        head.appendChild(title);
        head.appendChild(hint);
        overlay.appendChild(head);

        const content = document.createElement('div');
        content.style.cssText = `
      padding: 8px;border-radius: 8px;background: rgba(0,0,0,0.85);
      box-shadow: 0 0 0 1px rgba(255,255,255,0.2) inset;
      backdrop-filter: blur(2px);
      display: flex;
      flex-direction: column;
      gap: 10px;
    `;

        const lumaSection = document.createElement('div');
        lumaSection.style.cssText = `display:flex;flex-direction:column;gap:2px;`;

        const lumaTitle = document.createElement('div');
        lumaTitle.style.cssText = `font-size:9px;font-weight:900;color:#cfcfcf;text-transform:uppercase;letter-spacing:0.5px;`;
        lumaTitle.textContent = 'Luma Y';
        lumaSection.appendChild(lumaTitle);

        const lumaBars = document.createElement('div');
        lumaBars.style.cssText = `
      display:flex;align-items:flex-end;height:40px;gap:1px;
      background:rgba(20,20,20,0.6);border-radius:4px;padding:2px;
    `;
        lumaBars.className = 'gvf-scope-luma';
        for (let i = 0; i < 16; i++) {
            const bar = document.createElement('div');
            bar.style.cssText = `
        flex:1;height:2px;background:#4CAF50;border-radius:1px;
        transition:height 0.1s ease;
      `;
            bar.dataset.index = i;
            lumaBars.appendChild(bar);
        }
        lumaSection.appendChild(lumaBars);

        const rgbSection = document.createElement('div');
        rgbSection.style.cssText = `display:flex;flex-direction:column;gap:2px;`;

        const rgbTitle = document.createElement('div');
        rgbTitle.style.cssText = `font-size:9px;font-weight:900;color:#cfcfcf;text-transform:uppercase;letter-spacing:0.5px;`;
        rgbTitle.textContent = 'RGB';
        rgbSection.appendChild(rgbTitle);

        const rgbGrid = document.createElement('div');
        rgbGrid.style.cssText = `
      display:grid;grid-template-columns:1fr 1fr 1fr;gap:2px;
      background:rgba(20,20,20,0.6);border-radius:4px;padding:4px;
    `;

        const redCol = document.createElement('div');
        redCol.style.cssText = `display:flex;flex-direction:column;gap:1px;`;
        const redLabel = document.createElement('div');
        redLabel.style.cssText = `font-size:8px;font-weight:900;color:#ff6b6b;text-align:center;`;
        redLabel.textContent = 'R';
        redCol.appendChild(redLabel);
        const redBars = document.createElement('div');
        redBars.style.cssText = `display:flex;align-items:flex-end;height:32px;gap:1px;`;
        redBars.className = 'gvf-scope-red';
        for (let i = 0; i < 16; i++) {
            const bar = document.createElement('div');
            bar.style.cssText = `flex:1;height:2px;background:#ff5252;border-radius:1px;transition:height 0.1s ease;`;
            bar.dataset.index = i;
            redBars.appendChild(bar);
        }
        redCol.appendChild(redBars);
        rgbGrid.appendChild(redCol);

        const greenCol = document.createElement('div');
        greenCol.style.cssText = `display:flex;flex-direction:column;gap:1px;`;
        const greenLabel = document.createElement('div');
        greenLabel.style.cssText = `font-size:8px;font-weight:900;color:#6bff6b;text-align:center;`;
        greenLabel.textContent = 'G';
        greenCol.appendChild(greenLabel);
        const greenBars = document.createElement('div');
        greenBars.style.cssText = `display:flex;align-items:flex-end;height:32px;gap:1px;`;
        greenBars.className = 'gvf-scope-green';
        for (let i = 0; i < 16; i++) {
            const bar = document.createElement('div');
            bar.style.cssText = `flex:1;height:2px;background:#52ff52;border-radius:1px;transition:height 0.1s ease;`;
            bar.dataset.index = i;
            greenBars.appendChild(bar);
        }
        greenCol.appendChild(greenBars);
        rgbGrid.appendChild(greenCol);

        const blueCol = document.createElement('div');
        blueCol.style.cssText = `display:flex;flex-direction:column;gap:1px;`;
        const blueLabel = document.createElement('div');
        blueLabel.style.cssText = `font-size:8px;font-weight:900;color:#6b6bff;text-align:center;`;
        blueLabel.textContent = 'B';
        blueCol.appendChild(blueLabel);
        const blueBars = document.createElement('div');
        blueBars.style.cssText = `display:flex;align-items:flex-end;height:32px;gap:1px;`;
        blueBars.className = 'gvf-scope-blue';
        for (let i = 0; i < 16; i++) {
            const bar = document.createElement('div');
            bar.style.cssText = `flex:1;height:2px;background:#5252ff;border-radius:1px;transition:height 0.1s ease;`;
            bar.dataset.index = i;
            blueBars.appendChild(bar);
        }
        blueCol.appendChild(blueBars);
        rgbGrid.appendChild(blueCol);

        rgbSection.appendChild(rgbGrid);

        const satSection = document.createElement('div');
        satSection.style.cssText = `display:flex;flex-direction:column;gap:2px;`;

        const satTitle = document.createElement('div');
        satTitle.style.cssText = `font-size:9px;font-weight:900;color:#cfcfcf;text-transform:uppercase;letter-spacing:0.5px;`;
        satTitle.textContent = 'Sat';
        satSection.appendChild(satTitle);

        const satMeter = document.createElement('div');
        satMeter.style.cssText = `
      display:flex;align-items:center;gap:6px;
      background:rgba(20,20,20,0.6);border-radius:4px;padding:4px;
    `;

        const satBarBg = document.createElement('div');
        satBarBg.style.cssText = `flex:1;height:8px;background:#333;border-radius:4px;overflow:hidden;`;

        const satBarFill = document.createElement('div');
        satBarFill.style.cssText = `height:100%;width:0%;background:linear-gradient(90deg,#ffd700,#ff8c00);border-radius:4px;transition:width 0.1s ease;`;
        satBarFill.className = 'gvf-scope-sat-fill';

        const satValue = document.createElement('div');
        satValue.style.cssText = `font-size:9px;font-weight:900;color:#eaeaea;min-width:36px;text-align:right;`;
        satValue.className = 'gvf-scope-sat-value';
        satValue.textContent = '0.00';

        satBarBg.appendChild(satBarFill);
        satMeter.appendChild(satBarBg);
        satMeter.appendChild(satValue);
        satSection.appendChild(satMeter);

        const avgSection = document.createElement('div');
        avgSection.style.cssText = `
      display:grid;grid-template-columns:1fr 1fr 1fr;gap:2px;margin-top:2px;
      font-size:8px;font-weight:900;color:#aaa;
    `;

        const avgY = document.createElement('div');
        avgY.className = 'gvf-scope-avg-y';
        avgY.style.cssText = `text-align:center;background:rgba(30,30,30,0.6);border-radius:4px;padding:2px;`;
        avgY.textContent = 'Y: 0.00';

        const avgRGB = document.createElement('div');
        avgRGB.className = 'gvf-scope-avg-rgb';
        avgRGB.style.cssText = `text-align:center;background:rgba(30,30,30,0.6);border-radius:4px;padding:2px;`;
        avgRGB.textContent = 'RGB: 0.00';

        const avgSat = document.createElement('div');
        avgSat.className = 'gvf-scope-avg-sat';
        avgSat.style.cssText = `text-align:center;background:rgba(30,30,30,0.6);border-radius:4px;padding:2px;`;
        avgSat.textContent = 'Sat: 0.00';

        avgSection.appendChild(avgY);
        avgSection.appendChild(avgRGB);
        avgSection.appendChild(avgSat);

        content.appendChild(lumaSection);
        content.appendChild(rgbSection);
        content.appendChild(satSection);
        content.appendChild(avgSection);

        overlay.appendChild(content);
        (document.body || document.documentElement).appendChild(overlay);
        return overlay;
    }

    const SCOPES = {
        running: false,
        canvas: document.createElement('canvas'),
        ctx: null,
        lastUpdate: 0,
        updateInterval: 100,
        lastVideo: null
    };

    SCOPES.canvas.width = 160;
    SCOPES.canvas.height = 90;
    try {
        SCOPES.ctx = SCOPES.canvas.getContext('2d', { willReadFrequently: true, alpha: false });
    } catch (_) {
        try {
            SCOPES.ctx = SCOPES.canvas.getContext('2d', { alpha: false });
        } catch (__) {
            SCOPES.ctx = SCOPES.canvas.getContext('2d');
        }
    }

    function updateScopesData() {
        if (!scopesHudShown) return;

        const v = choosePrimaryVideo();
        if (!v || !SCOPES.ctx) return;

        if (v.paused || v.seeking || v.ended || v.readyState < 2) {
            return;
        }

        const now = nowMs();
        if (now - SCOPES.lastUpdate < SCOPES.updateInterval) return;

        try {
            const w = Math.max(2, v.videoWidth || 0);
            const h = Math.max(2, v.videoHeight || 0);
            if (!w || !h) return;

            const cssFilter = getAppliedCssFilterString(v);

            SCOPES.ctx.save();
            if (cssFilter) {
                SCOPES.ctx.filter = cssFilter;
            }
            SCOPES.ctx.drawImage(v, 0, 0, 160, 90);
            SCOPES.ctx.restore();

            let imgData;
            try {
                imgData = SCOPES.ctx.getImageData(0, 0, 160, 90);
            } catch (e) {
                SCOPES.lastUpdate = now;
                return;
            }

            const d = imgData.data;

            const lumaHist = new Array(16).fill(0);
            const redHist = new Array(16).fill(0);
            const greenHist = new Array(16).fill(0);
            const blueHist = new Array(16).fill(0);

            let sumR = 0, sumG = 0, sumB = 0, sumSat = 0;
            let count = 0;

            for (let y = 0; y < 90; y += 2) {
                for (let x = 0; x < 160; x += 2) {
                    const i = (y * 160 + x) * 4;
                    const r = d[i];
                    const g = d[i + 1];
                    const b = d[i + 2];

                    const yVal = Math.round(0.299 * r + 0.587 * g + 0.114 * b);
                    const lumaBucket = Math.floor(yVal / 16);
                    if (lumaBucket >= 0 && lumaBucket < 16) lumaHist[lumaBucket]++;

                    const rBucket = Math.floor(r / 16);
                    const gBucket = Math.floor(g / 16);
                    const bBucket = Math.floor(b / 16);
                    if (rBucket >= 0 && rBucket < 16) redHist[rBucket]++;
                    if (gBucket >= 0 && gBucket < 16) greenHist[gBucket]++;
                    if (bBucket >= 0 && bBucket < 16) blueHist[bBucket]++;

                    const max = Math.max(r, g, b);
                    const min = Math.min(r, g, b);
                    const sat = max - min;

                    sumR += r;
                    sumG += g;
                    sumB += b;
                    sumSat += sat;
                    count++;
                }
            }

            if (count === 0) return;

            const maxLuma = Math.max(...lumaHist, 1);
            const maxRed = Math.max(...redHist, 1);
            const maxGreen = Math.max(...greenHist, 1);
            const maxBlue = Math.max(...blueHist, 1);

            document.querySelectorAll('.gvf-scope-luma [data-index]').forEach(bar => {
                const idx = parseInt(bar.dataset.index);
                const val = lumaHist[idx] || 0;
                const pct = (val / maxLuma) * 36;
                bar.style.height = Math.max(2, pct) + 'px';
            });

            document.querySelectorAll('.gvf-scope-red [data-index]').forEach(bar => {
                const idx = parseInt(bar.dataset.index);
                const val = redHist[idx] || 0;
                const pct = (val / maxRed) * 28;
                bar.style.height = Math.max(2, pct) + 'px';
            });

            document.querySelectorAll('.gvf-scope-green [data-index]').forEach(bar => {
                const idx = parseInt(bar.dataset.index);
                const val = greenHist[idx] || 0;
                const pct = (val / maxGreen) * 28;
                bar.style.height = Math.max(2, pct) + 'px';
            });

            document.querySelectorAll('.gvf-scope-blue [data-index]').forEach(bar => {
                const idx = parseInt(bar.dataset.index);
                const val = blueHist[idx] || 0;
                const pct = (val / maxBlue) * 28;
                bar.style.height = Math.max(2, pct) + 'px';
            });

            const avgSat = sumSat / count / 255;
            const satPct = Math.min(100, avgSat * 200);
            const satFill = document.querySelector('.gvf-scope-sat-fill');
            const satValue = document.querySelector('.gvf-scope-sat-value');
            if (satFill) satFill.style.width = satPct + '%';
            if (satValue) satValue.textContent = avgSat.toFixed(2);

            const avgY = (0.299 * (sumR / count) + 0.587 * (sumG / count) + 0.114 * (sumB / count)) / 255;
            const avgR = (sumR / count) / 255;
            const avgG = (sumG / count) / 255;
            const avgB = (sumB / count) / 255;
            const avgRGB = (avgR + avgG + avgB) / 3;

            const avgYEl = document.querySelector('.gvf-scope-avg-y');
            const avgRGBEl = document.querySelector('.gvf-scope-avg-rgb');
            const avgSatEl = document.querySelector('.gvf-scope-avg-sat');

            if (avgYEl) avgYEl.textContent = `Y: ${avgY.toFixed(2)}`;
            if (avgRGBEl) avgRGBEl.textContent = `RGB: ${avgRGB.toFixed(2)}`;
            if (avgSatEl) avgSatEl.textContent = `Sat: ${avgSat.toFixed(2)}`;

            SCOPES.lastUpdate = now;

        } catch (e) {
            if (debug) console.log('[GVF] Scopes update failed:', e);
            SCOPES.lastUpdate = now;
        }
    }

    function startScopesLoop() {
        if (SCOPES.running) return;
        SCOPES.running = true;

        const loop = () => {
            if (!SCOPES.running) return;

            if (scopesHudShown) {
                try {
                    updateScopesData();
                } catch (e) {
                    if (debug) console.log('[GVF] Scopes loop error:', e);
                }
            }

            setTimeout(loop, SCOPES.updateInterval);
        };

        setTimeout(loop, 100);
    }

    function exportSettings() {
        return {
            schema: 'gvf-settings',
            ver: '1.10',
            enabled: !!enabled,
            notify: !!notify,
            darkMoody: !!darkMoody,
            tealOrange: !!tealOrange,
            vibrantSat: !!vibrantSat,

            sl: nFix(normSL(), 1),
            sr: nFix(normSR(), 1),
            bl: nFix(normBL(), 1),
            wl: nFix(normWL(), 1),
            ...(isFirefox() ? {} : { dn: nFix(normDN(), 1) }),
            ...(isFirefox() ? {} : { edge: nFix(normEDGE(), 2) }),

            hdr: nFix(normHDR(), 2),
            profile: String(profile),
            lutProfile: String((typeof activeLutProfileKey==='string' && activeLutProfileKey.trim()) ? activeLutProfileKey.trim() : 'none'),
            renderMode: String(renderMode),

            autoOn: !!autoOn,
            autoStrength: nFix(autoStrength, 2),
            autoLockWB: !!autoLockWB,

            adaptiveFps: {
                min: ADAPTIVE_FPS.MIN,
                max: ADAPTIVE_FPS.MAX,
                current: ADAPTIVE_FPS.current
            },

            user: {
                contrast: nFix(normU(u_contrast), 1),
                black: nFix(normU(u_black), 1),
                white: nFix(normU(u_white), 1),
                highlights: nFix(normU(u_highlights), 1),
                shadows: nFix(normU(u_shadows), 1),
                saturation: nFix(normU(u_sat), 1),
                vibrance: nFix(normU(u_vib), 1),
                sharpen: nFix(normU(u_sharp), 1),
                gamma: nFix(normU(u_gamma), 1),
                grain: nFix(normU(u_grain), 1),
                hue: nFix(normU(u_hue), 1),

                r_gain: Math.round(normRGB(u_r_gain)),
                g_gain: Math.round(normRGB(u_g_gain)),
                b_gain: Math.round(normRGB(u_b_gain))
            },
            debug: !!debug,
            logs: !!logs,
            cbFilter: String(cbFilter)
        };
    }

    function importSettings(obj) {
        if (!obj || typeof obj !== 'object') return false;

        _suspendSync = true;
        _inSync = true;

        try {
            const u = (obj.user && typeof obj.user === 'object') ? obj.user : {};

            if ('enabled' in obj) enabled = !!obj.enabled;
            if ('notify' in obj) {
                notify = !!obj.notify;
                gmSet(K.NOTIFY, notify);
            }
            if ('darkMoody' in obj) darkMoody = !!obj.darkMoody;
            if ('tealOrange' in obj) tealOrange = !!obj.tealOrange;
            if ('vibrantSat' in obj) vibrantSat = !!obj.vibrantSat;

            if ('sl' in obj) sl = clamp(Number(obj.sl), -2, 2);
            if ('sr' in obj) sr = clamp(Number(obj.sr), -2, 2);
            if ('bl' in obj) bl = clamp(Number(obj.bl), -2, 2);
            if ('wl' in obj) wl = clamp(Number(obj.wl), -2, 2);
            if ('dn' in obj) dn = clamp(Number(obj.dn), -1.5, 1.5);
            if ('edge' in obj) edge = clamp(Number(obj.edge), 0, 1);

            if ('hdr' in obj) hdr = clamp(Number(obj.hdr), -1, 2);

            if ('profile' in obj) {
                const p = String(obj.profile).toLowerCase();
                profile = (['off', 'film', 'anime', 'gaming', 'eyecare', 'user'].includes(p) ? p : 'off');
            }

            if ('renderMode' in obj) {
                const r = String(obj.renderMode).toLowerCase();
                renderMode = (r === 'gpu' ? 'gpu' : 'svg');
            }

            if ('autoOn' in obj) autoOn = !!obj.autoOn;
            if ('autoStrength' in obj) autoStrength = clamp(Number(obj.autoStrength), 0, 1);
            if ('autoLockWB' in obj) autoLockWB = !!obj.autoLockWB;

            if ('debug' in obj) {
                debug = !!obj.debug;
                gmSet(K.DEBUG, debug);
            }
            if ('logs' in obj) {
                logs = !!obj.logs;
                gmSet(K.LOGS, logs);
                LOG.on = logs;
            }

            if ('cbFilter' in obj) {
                const cb = String(obj.cbFilter).toLowerCase();
                cbFilter = (['none', 'protanopia', 'deuteranopia', 'tritanomaly'].includes(cb) ? cb : 'none');
                gmSet(K.CB_FILTER, cbFilter);
            }


// LUT profile selection (persist/restore via IO HUD config)
if ('lutProfile' in obj) {
    const raw = String(obj.lutProfile || 'none').trim() || 'none';

    // Accept either a key ("group||name") or legacy name-only value.
    const want = lutParseKey(raw);
    let key = (raw.includes('||') || raw === 'none') ? raw : want.key;

    // If legacy name-only was stored, pick the first matching profile (any group).
    if (!raw.includes('||') && raw !== 'none') {
        const p0 = (Array.isArray(lutProfiles) ? lutProfiles : []).find(x => _lutNormName(x && x.name) === want.name) || null;
        if (p0) key = lutKeyFromProfile(p0);
    }

    setActiveLutProfile(key);

    try {
        if (lutSelectEl) lutSelectEl.value = String(activeLutProfileKey || 'none');
        if (typeof refreshLutDropdownFn === 'function') refreshLutDropdownFn();
    } catch (_) { }

    log('Imported LUT profile selection:', activeLutProfileKey);
}            if ('contrast' in u) u_contrast = normU(u.contrast);
            if ('black' in u) u_black = normU(u.black);
            if ('white' in u) u_white = normU(u.white);
            if ('highlights' in u) u_highlights = normU(u.highlights);
            if ('shadows' in u) u_shadows = normU(u.shadows);
            if ('saturation' in u) u_sat = normU(u.saturation);
            if ('vibrance' in u) u_vib = normU(u.vibrance);
            if ('sharpen' in u) u_sharp = normU(u.sharpen);
            if ('gamma' in u) u_gamma = normU(u.gamma);
            if ('grain' in u) u_grain = normU(u.grain);
            if ('hue' in u) u_hue = normU(u.hue);

            if ('r_gain' in u) u_r_gain = normRGB(u.r_gain);
            if ('g_gain' in u) u_g_gain = normRGB(u.g_gain);
            if ('b_gain' in u) u_b_gain = normRGB(u.b_gain);

            enabled = !!enabled; darkMoody = !!darkMoody; tealOrange = !!tealOrange; vibrantSat = !!vibrantSat; iconsShown = !!iconsShown;

            sl = normSL(); sr = normSR(); bl = normBL(); wl = normWL(); dn = normDN(); hdr = normHDR();

            u_contrast = normU(u_contrast);
            u_black = normU(u_black);
            u_white = normU(u_white);
            u_highlights = normU(u_highlights);
            u_shadows = normU(u_shadows);
            u_sat = normU(u_sat);
            u_vib = normU(u_vib);
            u_sharp = normU(u_sharp);
            u_gamma = normU(u_gamma);
            u_grain = normU(u_grain);
            u_hue = normU(u_hue);

            u_r_gain = normRGB(u_r_gain);
            u_g_gain = normRGB(u_g_gain);
            u_b_gain = normRGB(u_b_gain);

            gmSet(K.enabled, enabled);
            gmSet(K.moody, darkMoody);
            gmSet(K.teal, tealOrange);
            gmSet(K.vib, vibrantSat);
            gmSet(K.icons, iconsShown);

            gmSet(K.SL, sl);
            gmSet(K.SR, sr);
            gmSet(K.BL, bl);
            gmSet(K.WL, wl);
            gmSet(K.DN, dn);
            gmSet(K.EDGE, edge);

            gmSet(K.HDR, hdr);
            if (hdr !== 0) gmSet(K.HDR_LAST, hdr);

            gmSet(K.PROF, profile);
            gmSet(K.RENDER_MODE, renderMode);
            gmSet(K.NOTIFY, notify);
            gmSet(K.G_HUD, gradingHudShown);
            gmSet(K.I_HUD, ioHudShown);
            gmSet(K.S_HUD, scopesHudShown);

            gmSet(K.U_CONTRAST, u_contrast);
            gmSet(K.U_BLACK, u_black);
            gmSet(K.U_WHITE, u_white);
            gmSet(K.U_HIGHLIGHTS, u_highlights);
            gmSet(K.U_SHADOWS, u_shadows);
            gmSet(K.U_SAT, u_sat);
            gmSet(K.U_VIB, u_vib);
            gmSet(K.U_SHARP, u_sharp);
            gmSet(K.U_GAMMA, u_gamma);
            gmSet(K.U_GRAIN, u_grain);
            gmSet(K.U_HUE, u_hue);

            gmSet(K.U_R_GAIN, u_r_gain);
            gmSet(K.U_G_GAIN, u_g_gain);
            gmSet(K.U_B_GAIN, u_b_gain);

            gmSet(K.AUTO_ON, autoOn);
            gmSet(K.AUTO_STRENGTH, autoStrength);
            gmSet(K.AUTO_LOCK_WB, autoLockWB);

            setAutoOn(autoOn, { silent: true });

            if (renderMode === 'gpu') {
                applyGpuFilter();
            } else {
                regenerateSvgImmediately();
            }
            scheduleOverlayUpdate();

            return true;
        } catch (_) {
            return false;
        } finally {
            _inSync = false;
            _suspendSync = false;
        }
    }

    function toggleRenderMode() {
        renderMode = renderMode === 'svg' ? 'gpu' : 'svg';
        gmSet(K.RENDER_MODE, renderMode);
        logToggle('Render Mode (Ctrl+Alt+X)', renderMode === 'gpu', `Mode: ${renderMode === 'gpu' ? 'WebGL2 Canvas Pipeline' : 'SVG'}`);

        // Save current settings in active profile
        updateCurrentProfileSettings();

        if (renderMode === 'gpu') {
            deactivateSVGMode();
            activateWebGLMode();
            applyGpuFilter();
        } else {
            deactivateWebGLMode();
            regenerateSvgImmediately();
        }

        scheduleOverlayUpdate();
    }

    function deactivateSVGMode() {
        const style = document.getElementById(STYLE_ID);
        if (style) style.remove();
        const svg = document.getElementById(SVG_ID);
        if (svg) svg.remove();
    }

    function ensureGpuSvgHost() {
        let svg = document.getElementById(GPU_SVG_ID);
        if (svg) return svg;

        svg = document.createElementNS(svgNS, 'svg');
        svg.id = GPU_SVG_ID;
        svg.setAttribute('width', '0');
        svg.setAttribute('height', '0');
        svg.style.position = 'absolute';
        svg.style.left = '-9999px';
        svg.style.top = '-9999px';

        const defs = document.createElementNS(svgNS, 'defs');
        svg.appendChild(defs);

        (document.body || document.documentElement).appendChild(svg);
        return svg;
    }

    function upsertGpuGainFilter() {
        const svg = ensureGpuSvgHost();
        if (!svg) return;

        const defs = svg.querySelector('defs') || svg;

        let f = defs.querySelector(`#${GPU_GAIN_FILTER_ID}`);
        if (!f) {
            f = document.createElementNS(svgNS, 'filter');
            f.setAttribute('id', GPU_GAIN_FILTER_ID);
            defs.appendChild(f);
        } else {
            while (f.firstChild) f.removeChild(f.firstChild);
        }

        const r = rgbGainToFactor(u_r_gain);
        const g = rgbGainToFactor(u_g_gain);
        const b = rgbGainToFactor(u_b_gain);

        const fe = document.createElementNS(svgNS, 'feColorMatrix');
        fe.setAttribute('type', 'matrix');
        fe.setAttribute('values', [
            r, 0, 0, 0, 0,
            0, g, 0, 0, 0,
            0, 0, b, 0, 0,
            0, 0, 0, 1, 0
        ].join(' '));
        f.appendChild(fe);
    }

    function gpuProfileMatrixActive() {
        return (profile === 'film' || profile === 'anime' || profile === 'gaming' || profile === 'eyecare');
    }

    function upsertGpuProfileFilter() {
        const svg = ensureGpuSvgHost();
        if (!svg) return;

        const defs = svg.querySelector('defs') || svg;

        let f = defs.querySelector(`#${GPU_PROFILE_FILTER_ID}`);
        if (!f) {
            f = document.createElementNS(svgNS, 'filter');
            f.setAttribute('id', GPU_PROFILE_FILTER_ID);
            f.setAttribute('color-interpolation-filters', 'sRGB');
            defs.appendChild(f);
        } else {
            const lastP = f.getAttribute('data-prof');
            if (lastP === profile) return;
            while (f.firstChild) f.removeChild(f.firstChild);
        }

        f.setAttribute('data-prof', profile);

        const profMat = mkProfileMatrixCT(profile);
        if (profMat) f.appendChild(profMat);

        if (profile === 'eyecare') {
            const sat = document.createElementNS(svgNS, 'feColorMatrix');
            sat.setAttribute('type', 'saturate');
            sat.setAttribute('values', '0.82');
            f.appendChild(sat);

            const sepia = document.createElementNS(svgNS, 'feColorMatrix');
            sepia.setAttribute('type', 'matrix');
            sepia.setAttribute('values', [
                0.85, 0.15, 0.00, 0, 0,
                0.10, 0.80, 0.10, 0, 0,
                0.05, 0.05, 0.70, 0, 0,
                0, 0, 0, 1, 0
            ].join(' '));
            f.appendChild(sepia);

            const hue = document.createElementNS(svgNS, 'feColorMatrix');
            hue.setAttribute('type', 'hueRotate');
            hue.setAttribute('values', '-22');
            f.appendChild(hue);
        }

        if (profile === 'anime') {

            const blur = document.createElementNS(svgNS, 'feGaussianBlur');
            blur.setAttribute('stdDeviation', '0.8');
            blur.setAttribute('in', 'SourceGraphic');
            blur.setAttribute('result', 'denoised');
            f.appendChild(blur);


            const sobel = document.createElementNS(svgNS, 'feConvolveMatrix');
            sobel.setAttribute('order', '3');
            sobel.setAttribute('kernelMatrix',
                '-1 -2 -1 ' +
                ' 0  0  0 ' +
                ' 1  2  1'
            );
            sobel.setAttribute('divisor', '1');
            sobel.setAttribute('in', 'denoised');
            sobel.setAttribute('result', 'edges');
            f.appendChild(sobel);

            const componentTransfer = document.createElementNS(svgNS, 'feComponentTransfer');
            componentTransfer.setAttribute('in', 'edges');
            componentTransfer.setAttribute('result', 'darkEdges');

            const funcR = document.createElementNS(svgNS, 'feFuncR');
            funcR.setAttribute('type', 'linear');
            funcR.setAttribute('slope', '2.2');
            funcR.setAttribute('intercept', '-0.3');
            componentTransfer.appendChild(funcR);

            const funcG = funcR.cloneNode();
            const funcB = funcR.cloneNode();
            componentTransfer.appendChild(funcG);
            componentTransfer.appendChild(funcB);
            f.appendChild(componentTransfer);

            const threshold = document.createElementNS(svgNS, 'feComponentTransfer');
            threshold.setAttribute('in', 'darkEdges');
            threshold.setAttribute('result', 'thresholdEdges');

            const tFuncR = document.createElementNS(svgNS, 'feFuncR');
            tFuncR.setAttribute('type', 'linear');
            tFuncR.setAttribute('slope', '3');
            tFuncR.setAttribute('intercept', '-0.4');
            threshold.appendChild(tFuncR);

            const tFuncG = tFuncR.cloneNode();
            const tFuncB = tFuncR.cloneNode();
            threshold.appendChild(tFuncG);
            threshold.appendChild(tFuncB);
            f.appendChild(threshold);

            const blend = document.createElementNS(svgNS, 'feComposite');
            blend.setAttribute('operator', 'arithmetic');
            blend.setAttribute('k1', '0');
            blend.setAttribute('k2', '1');
            blend.setAttribute('k3', '0.3');
            blend.setAttribute('k4', '0');
            blend.setAttribute('in', 'SourceGraphic');
            blend.setAttribute('in2', 'thresholdEdges');
            blend.setAttribute('result', 'final');
            f.appendChild(blend);
        }
    }

    function removeGpuProfileFilter() {
        const svg = document.getElementById(GPU_SVG_ID);
        if (!svg) return;
        const f = svg.querySelector(`#${GPU_PROFILE_FILTER_ID}`);
        if (f && f.parentNode) f.parentNode.removeChild(f);
    }

    function removeGpuGainFilter() {
        const svg = document.getElementById(GPU_SVG_ID);
        if (!svg) return;
        const f = svg.querySelector(`#${GPU_GAIN_FILTER_ID}`);
        if (f && f.parentNode) f.parentNode.removeChild(f);
    }

    function gpuGainActive() {
        if (profile !== 'user') return false;
        return (u_r_gain !== 128) || (u_g_gain !== 128) || (u_b_gain !== 128);
    }

    function applyGpuFilter() {
        if (renderMode === 'gpu' && webglPipeline && webglPipeline.active) {
            let style = document.getElementById(STYLE_ID);
            if (style) style.remove();
            scheduleOverlayUpdate();
            return;
        }

        let style = document.getElementById(STYLE_ID);

        const nothingOn =
            !enabled && !darkMoody && !tealOrange && !vibrantSat && normEDGE() === 0 && normHDR() === 0 && (profile === 'off') && !autoOn && cbFilter === 'none'
            && (!activeLutMatrix4x5 || String(activeLutProfileKey || 'none') === 'none');

        if (nothingOn) {
            if (style) style.remove();
            removeGpuGainFilter();
            scheduleOverlayUpdate();
            return;
        }

        if (!style) {
            style = document.createElement('style');
            style.id = STYLE_ID;
            document.head.appendChild(style);
        }

        let gpuFilterString = getGpuFilterString();

        if (gpuProfileMatrixActive()) {
            upsertGpuProfileFilter();
            const urlP = `url(#${GPU_PROFILE_FILTER_ID})`;
            gpuFilterString = gpuFilterString ? (gpuFilterString + ' ' + urlP) : urlP;
        } else {
            removeGpuProfileFilter();
        }

        if (gpuGainActive()) {
            upsertGpuGainFilter();
            const url = `url(#${GPU_GAIN_FILTER_ID})`;
            gpuFilterString = gpuFilterString ? (gpuFilterString + ' ' + url) : url;
        } else {
            removeGpuGainFilter();
        }

        const outlineCss = (PROFILE_VIDEO_OUTLINE && profile !== 'off')
            ? `outline: 2px solid ${(PROF[profile] || PROF.off).color} !important; outline-offset: -2px;`
            : `outline: none !important;`;

        const finalFilter = (gpuFilterString && String(gpuFilterString).trim()) ? String(gpuFilterString).trim() : 'none';

        style.textContent = `
      video {
        will-change: filter;
        transform: translateZ(0);
        filter: ${finalFilter} !important;
        ${outlineCss}
      }
    `;

        scheduleOverlayUpdate();
    }

    function updateMainOverlayState(overlay) {
        if (!iconsShown) { overlay.style.display = 'none'; return; }
        overlay.style.display = 'flex';

        const state = {
            base: enabled,
            moody: darkMoody,
            teal: tealOrange,
            vib: vibrantSat,
            hdr: (normHDR() !== 0),
            auto: autoOn,
            mode: true
        };

        overlay.querySelectorAll('[data-key]').forEach(el => {
            const key = el.dataset.key;
            let on = !!state[key];

            if (key === 'mode') {
                el.textContent = renderMode === 'gpu' ? 'C' : 'S';
                on = true;
                el.style.color = renderMode === 'gpu' ? '#ffaa00' : '#88ccff';
                el.style.background = 'rgba(255,255,255,0.15)';
            } else {
                el.style.color = on ? '#fff' : '#666';
                el.style.background = on ? 'rgba(255,255,255,0.22)' : '#000';
            }
            el.style.boxShadow = '0 0 0 1px rgba(255,255,255,0.18) inset';
        });

        const badge = overlay.querySelector('.gvf-prof-badge');
        if (badge) {
            const p = PROF[profile] || PROF.off;
            const c = p.color;
            badge.textContent = `${p.name} (C)`;

            if (c && c !== 'transparent') {
                badge.style.background = 'rgba(0,0,0,0.92)';
                badge.style.border = `1px solid ${c}`;
                badge.style.boxShadow = `0 0 0 1px rgba(255,255,255,0.14) inset, 0 0 0 2px ${c}, 0 0 18px ${c}55`;
            } else {
                badge.style.background = 'rgba(0,0,0,0.92)';
                badge.style.border = '1px solid rgba(255,255,255,0.10)';
                badge.style.boxShadow = '0 0 0 1px rgba(255,255,255,0.14) inset';
            }
        }

        const renderBadge = overlay.querySelector('.gvf-render-badge');
        if (renderBadge) {
            renderBadge.textContent = renderMode === 'gpu' ? 'GPU' : 'SVG';
            renderBadge.style.color = renderMode === 'gpu' ? '#ffaa00' : '#88ccff';
        }

        const setPair = (name, v) => {
            const r = overlay.querySelector(`[data-gvf-range="${cssEscape(name)}"]`);
            const t = overlay.querySelector(`[data-gvf-val="${cssEscape(name)}"]`);
            if (r) r.value = String(v);
            if (t) t.textContent = Number(v).toFixed(2);
        };

        setPair('SL', normSL());
        setPair('SR', normSR());
        setPair('BL', normBL());
        setPair('WL', normWL());
        setPair('DN', normDN());
        setPair('HDR', normHDR());
    }

    function updateGradingOverlayState(overlay) {
        if (!gradingHudShown) { overlay.style.display = 'none'; return; }
        overlay.style.display = 'flex';

        const setPair = (name, v) => {
            const r = overlay.querySelector(`[data-gvf-range="${cssEscape(name)}"]`);
            const t = overlay.querySelector(`[data-gvf-val="${cssEscape(name)}"]`);
            if (r) r.value = String(v);
            if (t) t.textContent = Number(v).toFixed(1);
        };

        setPair('U_CONTRAST', normU(u_contrast));
        setPair('U_BLACK', normU(u_black));
        setPair('U_WHITE', normU(u_white));
        setPair('U_HIGHLIGHTS', normU(u_highlights));
        setPair('U_SHADOWS', normU(u_shadows));
        setPair('U_SAT', normU(u_sat));
        setPair('U_VIB', normU(u_vib));
        setPair('U_SHARP', normU(u_sharp));
        setPair('U_GAMMA', normU(u_gamma));
        setPair('U_GRAIN', normU(u_grain));
        setPair('U_HUE', normU(u_hue));

        const setRGBPair = (name, v) => {
            const r = overlay.querySelector(`[data-gvf-range="${cssEscape(name)}"]`);
            const t = overlay.querySelector(`[data-gvf-val="${cssEscape(name)}"]`);
            if (r) r.value = String(v);
            if (t) t.textContent = String(Math.round(v));
        };

        setRGBPair('U_R_GAIN', normRGB(u_r_gain));
        setRGBPair('U_G_GAIN', normRGB(u_g_gain));
        setRGBPair('U_B_GAIN', normRGB(u_b_gain));

        // Update color blindness dropdown
        const cbSelect = overlay.querySelector('[data-gvf-select="cb_filter"]');
        if (cbSelect) {
            cbSelect.value = cbFilter;
        }
    }

    function updateIOOverlayState(overlay) {
        if (!ioHudShown) { overlay.style.display = 'none'; return; }
        overlay.style.display = 'flex';

        try {
            const btnRec = overlay.__btnRec;
            const status = overlay.__status;
            if (btnRec && !REC.active) {
                const v = getActiveVideoForCapture();
                if (!v) {
                    btnRec.disabled = true;
                    btnRec.textContent = 'No video';
                    btnRec.style.opacity = '0.55';
                    btnRec.style.cursor = 'not-allowed';
                } else {
                    const chk = canBakeToCanvas(v);
                    if (!chk.ok) {
                        btnRec.disabled = true;
                        btnRec.textContent = 'DRM blocked';
                        btnRec.style.opacity = '0.55';
                        btnRec.style.cursor = 'not-allowed';
                        if (status && status.textContent === 'Tip: paste JSON here → Save') {
                            status.textContent = `Recording disabled: ${chk.reason}`;
                        }
                    } else {
                        if (isFirefox()) {
                            btnRec.disabled = true;
                            btnRec.textContent = 'Record';
                            btnRec.title = 'Not supported in Firefox.';
                            btnRec.style.opacity = '0.4';
                            btnRec.style.cursor = 'not-allowed';
                        } else {
                            btnRec.disabled = false;
                            btnRec.textContent = 'Record';
                            btnRec.style.opacity = '1';
                            btnRec.style.cursor = 'pointer';
                        }

                        if (isFirefox()) {
                            const tap = ensureAudioTap(v);
                            if (tap && tap.tracks && tap.tracks.length && status && !status.textContent.startsWith('Recording disabled')) {
                                if (status.textContent === 'Tip: paste JSON here → Save') {
                                    status.textContent = 'Firefox: recording uses WebAudio tap (should keep audio + no auto-mute).';
                                }
                            }
                        }
                    }
                }
            }

            // Update Debug button
            const btnDebug = Array.from(overlay.querySelectorAll('button')).find(b => b.textContent.startsWith('🐞') || b.textContent.startsWith('Debug'));
            if (btnDebug) {
                btnDebug.textContent = debug ? '🐞 Debug: ON' : '🐞 Debug: OFF';
                btnDebug.style.background = debug ? 'rgba(0,255,0,0.2)' : 'rgba(255,0,0,0.2)';
                btnDebug.style.border = debug ? '1px solid #00ff00' : '1px solid #ff0000';
                btnDebug.style.color = debug ? '#00ff00' : '#ff6666';
            }

            // Config Button Status
            const btnConfig = Array.from(overlay.querySelectorAll('button')).find(b => b.textContent.includes('Config'));
            if (btnConfig) {
                if (configMenuVisible) {
                    btnConfig.style.background = 'rgba(42, 111, 219, 0.6)';
                } else {
                    btnConfig.style.background = 'rgba(42, 111, 219, 0.4)';
                }
            }
        } catch (_) { }

        const ta = overlay.querySelector('.gvf-io-text');
        if (!ta) return;
        if (ta.dataset.dirty) return;

        ta.value = JSON.stringify(exportSettings(), null, 2);
    }

    function updateScopesOverlayState(overlay) {
        if (!scopesHudShown) { overlay.style.display = 'none'; return; }
        overlay.style.display = 'flex';
    }

    const fsWraps2 = new WeakMap();

    function ensureFsWrapper(video) {
        if (fsWraps2.has(video)) return fsWraps2.get(video);
        if (!video || !video.parentNode) return null;

        const parent = video.parentNode;

        const wrap = document.createElement('div');
        wrap.className = 'gvf-fs-wrap';
        wrap.style.cssText = `
      position: relative;display: inline-block;width: 100%;height: 100%;
      max-width: 100%;background: black;
    `;

        const ph = document.createComment('gvf-video-placeholder');
        parent.insertBefore(ph, video);
        parent.insertBefore(wrap, video);
        wrap.appendChild(video);

        wrap.__gvfPlaceholder = ph;
        fsWraps2.set(video, wrap);
        return wrap;
    }

    function restoreFromFsWrapper(video) {
        const wrap = fsWraps2.get(video);
        if (!wrap) return;
        const ph = wrap.__gvfPlaceholder;
        if (ph && ph.parentNode) {
            ph.parentNode.insertBefore(video, ph);
            ph.parentNode.removeChild(ph);
        }
        if (wrap.parentNode) wrap.parentNode.removeChild(wrap);
        fsWraps2.delete(video);
    }

    function patchFullscreenRequest(video) {
        if (!video || video.__gvfFsPatched) return;
        video.__gvfFsPatched = true;

        if (typeof video.webkitEnterFullscreen === 'function') return;

        const origReq = video.requestFullscreen || video.webkitRequestFullscreen || video.msRequestFullscreen;
        if (!origReq) return;

        const callWrapFs = async () => {
            const wrap = ensureFsWrapper(video);
            if (!wrap) return origReq.call(video);
            const req = wrap.requestFullscreen || wrap.webkitRequestFullscreen || wrap.msRequestFullscreen;
            if (req) return req.call(wrap);
            return origReq.call(video);
        };

        if (video.requestFullscreen) {
            const _orig = video.requestFullscreen.bind(video);
            video.requestFullscreen = function () { return callWrapFs() || _orig(); };
        }
        if (video.webkitRequestFullscreen) {
            const _orig = video.webkitRequestFullscreen.bind(video);
            video.webkitRequestFullscreen = function () { return callWrapFs() || _orig(); };
        }
        if (video.msRequestFullscreen) {
            const _orig = video.msRequestFullscreen.bind(video);
            video.msRequestFullscreen = function () { return callWrapFs() || _orig(); };
        }
    }

    function getOverlayContainer(video) {
        const fsEl = getFsEl();
        const wrap = fsWraps2.get(video);

        if (fsEl && wrap && fsEl === wrap) return wrap;

        if (fsEl && (fsEl === video || (fsEl.contains && fsEl.contains(video)))) {
            if (fsEl.tagName && fsEl.tagName.toLowerCase() === 'video') return document.body || document.documentElement;
            return fsEl;
        }
        return document.body || document.documentElement;
    }

    function positionOverlayAt(video, overlay, dx, dy) {
        const fsEl = getFsEl();
        const container = getOverlayContainer(video);

        if (overlay.parentNode !== container) container.appendChild(overlay);

        const isWrapFs = fsEl && container === fsEl && fsEl.classList && fsEl.classList.contains('gvf-fs-wrap');
        overlay.style.position = isWrapFs ? 'absolute' : 'fixed';

        const r = video.getBoundingClientRect();
        if (!r || r.width < 40 || r.height < 40) { overlay.style.display = 'none'; return; }

        if (!fsEl) {
            if (r.bottom < 0 || r.right < 0 || r.top > (window.innerHeight || 0) || r.left > (window.innerWidth || 0)) {
                overlay.style.display = 'none';
                return;
            }
        }

        if (overlay.classList.contains('gvf-video-overlay-scopes')) {
            if (isWrapFs) {
                const cr = container.getBoundingClientRect();
                overlay.style.top = `${Math.round((r.top - cr.top) + dy)}px`;
                overlay.style.left = `${Math.round((r.left - cr.left) + dx)}px`;
                overlay.style.transform = 'none';
            } else {
                overlay.style.top = `${Math.round(r.top + dy)}px`;
                overlay.style.left = `${Math.round(r.left + dx)}px`;
                overlay.style.transform = 'none';
            }
        } else {
            if (isWrapFs) {
                const cr = container.getBoundingClientRect();
                overlay.style.top = `${Math.round((r.top - cr.top) + dy)}px`;
                overlay.style.left = `${Math.round((r.left - cr.left) + r.width - dx)}px`;
                overlay.style.transform = 'translateX(-100%) translateZ(0)';
            } else {
                overlay.style.top = `${Math.round(r.top + dy)}px`;
                overlay.style.left = `${Math.round(r.left + r.width - dx)}px`;
                overlay.style.transform = 'translateX(-100%) translateZ(0)';
            }
        }
    }

    function ensureOverlays() {
        document.querySelectorAll('video').forEach(v => {
            patchFullscreenRequest(v);

            if (!overlaysMain.has(v)) overlaysMain.set(v, mkMainOverlay());
            if (!overlaysGrade.has(v)) overlaysGrade.set(v, mkGradingOverlay());
            if (!overlaysIO.has(v)) overlaysIO.set(v, mkIOOverlay());
            if (!overlaysScopes.has(v)) overlaysScopes.set(v, mkScopesOverlay());
            if (debug && !overlaysAutoDot.has(v)) overlaysAutoDot.set(v, mkAutoDotOverlay());
        });
    }

    function updateAllOverlays() {
        ensureOverlays();
        updateCustomWebglOverlays();
        updateCustomCanvas2DOverlays();

        const primary = choosePrimaryVideo();
        const hudPrimary = getHudPrimaryVideo();

        document.querySelectorAll('video').forEach(v => {
            const oMain = overlaysMain.get(v);
            const oGr = overlaysGrade.get(v);
            const oIO = overlaysIO.get(v);
            const oScopes = overlaysScopes.get(v);
            const oDot = overlaysAutoDot.get(v);
            const visible = isVideoRenderable(v);
            const hudVisible = isHudVideoVisible(v);

            if (oMain) {
                updateMainOverlayState(oMain);
                if (iconsShown && hudVisible && hudPrimary === v) positionOverlayAt(v, oMain, 10, 10);
                else oMain.style.display = 'none';
            }
            if (oGr) {
                if (gradingHudShown && hudVisible && hudPrimary === v) {
                    updateGradingOverlayState(oGr);
                    positionOverlayAt(v, oGr, 10, 10 + 280);
                } else {
                    oGr.style.display = 'none';
                }
            }
            if (oIO) {
                if (ioHudShown && hudVisible && hudPrimary === v) {
                    updateIOOverlayState(oIO);
                    positionOverlayAt(v, oIO, 10, 10 + 560);
                } else {
                    oIO.style.display = 'none';
                }
            }
            if (oScopes) {
                if (scopesHudShown && hudVisible && hudPrimary === v) {
                    updateScopesOverlayState(oScopes);
                    positionOverlayAt(v, oScopes, 10, 10);
                } else {
                    oScopes.style.display = 'none';
                }
            }

            if (oDot) {
                applyAutoDotStyle(oDot);

                if (!debug || !autoOn || !primary || v !== primary || !visible) {
                    oDot.style.display = 'none';
                } else {
                    positionOverlayAt(v, oDot, 10, 10);
                    oDot.style.display = 'block';
                }
            }
        });
    }

    function scheduleOverlayUpdate() {
        if (rafScheduled) return;
        rafScheduled = true;
        requestAnimationFrame(() => {
            rafScheduled = false;
            updateAllOverlays();
        });
    }

    // Moves GVF modals/panels into the fullscreen element so they remain visible.
    // Called on fullscreenchange; also called when a panel is opened while in FS.
    function reparentGvfModals() {
        const fsEl = getFsEl();
        const target = fsEl || document.body || document.documentElement;
        const IDS = [CONFIG_MENU_ID, LUT_CONFIG_MENU_ID, 'gvf-custom-svg-modal'];
        IDS.forEach(id => {
            const el = document.getElementById(id);
            if (el && el.parentNode !== target) {
                target.appendChild(el);
            }
        });
    }

    function onFsChange() {
        const fsEl = getFsEl();
        if (!fsEl) {
            document.querySelectorAll('video').forEach(v => {
                if (fsWraps2.has(v)) restoreFromFsWrapper(v);
            });
        }
        CustomWebglOverlayManager.reparentAll();
        CustomCanvas2DOverlayManager.reparentAll();
        reparentGvfModals();
        scheduleOverlayUpdate();
    }

    document.addEventListener('visibilitychange', scheduleOverlayUpdate, { passive: true });
    window.addEventListener('focus', scheduleOverlayUpdate, { passive: true });
    window.addEventListener('blur', () => setTimeout(scheduleOverlayUpdate, 100), { passive: true });

    function mkGamma(ch, amp, exp, off) {
        const f = document.createElementNS(svgNS, ch);
        f.setAttribute('type', 'gamma');
        f.setAttribute('amplitude', String(amp));
        f.setAttribute('exponent', String(exp));
        f.setAttribute('offset', String(off));
        return f;
    }

    function mkOffsetCT(inId, outId, offset) {
        const ct = document.createElementNS(svgNS, 'feComponentTransfer');
        ct.setAttribute('in', inId);
        ct.setAttribute('result', outId);
        ct.appendChild(mkGamma('feFuncR', 1.0, 1.0, offset));
        ct.appendChild(mkGamma('feFuncG', 1.0, 1.0, offset));
        ct.appendChild(mkGamma('feFuncB', 1.0, 1.0, offset));
        return ct;
    }

    function mkHighlightsTableCT(inId, outId, hiAdj) {
        const knee = 0.78;
        const steps = 17;
        const vals = [];
        for (let i = 0; i < steps; i++) {
            const x = i / (steps - 1);
            let y = x;
            if (x > knee) {
                const t = (x - knee) / (1 - knee);
                y = x + hiAdj * t;
            }
            y = clamp(y, 0, 1);
            vals.push(y.toFixed(4));
        }

        const ct = document.createElementNS(svgNS, 'feComponentTransfer');
        ct.setAttribute('in', inId);
        ct.setAttribute('result', outId);

        const mkTable = (tag) => {
            const f = document.createElementNS(svgNS, tag);
            f.setAttribute('type', 'table');
            f.setAttribute('tableValues', vals.join(' '));
            return f;
        };

        ct.appendChild(mkTable('feFuncR'));
        ct.appendChild(mkTable('feFuncG'));
        ct.appendChild(mkTable('feFuncB'));
        return ct;
    }

    function mkDenoiseBlend(inId, outId, sigma, mix) {
        const blur = document.createElementNS(svgNS, 'feGaussianBlur');
        blur.setAttribute('in', inId);
        blur.setAttribute('stdDeviation', String(sigma));
        blur.setAttribute('result', outId + '_b');

        const comp = document.createElementNS(svgNS, 'feComposite');
        comp.setAttribute('in', inId);
        comp.setAttribute('in2', outId + '_b');
        comp.setAttribute('operator', 'arithmetic');
        comp.setAttribute('k1', '0');
        comp.setAttribute('k2', String(1 - mix));
        comp.setAttribute('k3', String(mix));
        comp.setAttribute('k4', '0');
        comp.setAttribute('result', outId);

        return [blur, comp];
    }

    function mkGrain(inId, outId, alpha) {
        const turb = document.createElementNS(svgNS, 'feTurbulence');
        turb.setAttribute('type', 'fractalNoise');
        turb.setAttribute('baseFrequency', '0.9');
        turb.setAttribute('numOctaves', '2');
        turb.setAttribute('seed', '2');
        turb.setAttribute('result', outId + '_n');

        const noiseCM = document.createElementNS(svgNS, 'feColorMatrix');
        noiseCM.setAttribute('in', outId + '_n');
        noiseCM.setAttribute('type', 'matrix');
        noiseCM.setAttribute('values',
            '0.33 0.33 0.33 0 0 ' +
            '0.33 0.33 0.33 0 0 ' +
            '0.33 0.33 0.33 0 0 ' +
            '0    0    0    1 0'
        );
        noiseCM.setAttribute('result', outId + '_nm');

        const comp = document.createElementNS(svgNS, 'feComposite');
        comp.setAttribute('in', inId);
        comp.setAttribute('in2', outId + '_nm');
        comp.setAttribute('operator', 'arithmetic');
        comp.setAttribute('k1', '0');
        comp.setAttribute('k2', '1');
        comp.setAttribute('k3', String(alpha));
        comp.setAttribute('k4', '0');
        comp.setAttribute('result', outId);

        return [turb, noiseCM, comp];
    }

    function mkClarityHighpass(inId, outId, sigma, amount) {
        const blur = document.createElementNS(svgNS, 'feGaussianBlur');
        blur.setAttribute('in', inId);
        blur.setAttribute('stdDeviation', String(sigma));
        blur.setAttribute('result', outId + '_b');

        const comp = document.createElementNS(svgNS, 'feComposite');
        comp.setAttribute('in', inId);
        comp.setAttribute('in2', outId + '_b');
        comp.setAttribute('operator', 'arithmetic');
        comp.setAttribute('k1', '0');
        comp.setAttribute('k2', String(1 + amount));
        comp.setAttribute('k3', String(-amount));
        comp.setAttribute('k4', '0');
        comp.setAttribute('result', outId);

        return [blur, comp];
    }

    function mkBlend(inA, inB, outId, mixB) {
        const comp = document.createElementNS(svgNS, 'feComposite');
        comp.setAttribute('in', inA);
        comp.setAttribute('in2', inB);
        comp.setAttribute('operator', 'arithmetic');
        comp.setAttribute('k1', '0');
        comp.setAttribute('k2', String(1 - mixB));
        comp.setAttribute('k3', String(mixB));
        comp.setAttribute('k4', '0');
        comp.setAttribute('result', outId);
        return comp;
    }

    function mkLinearCT(inId, outId, slope, intercept) {
        const ct = document.createElementNS(svgNS, 'feComponentTransfer');
        ct.setAttribute('in', inId);
        ct.setAttribute('result', outId);

        const mkLin = (tag) => {
            const f = document.createElementNS(svgNS, tag);
            f.setAttribute('type', 'linear');
            f.setAttribute('slope', String(slope));
            f.setAttribute('intercept', String(intercept));
            return f;
        };

        ct.appendChild(mkLin('feFuncR'));
        ct.appendChild(mkLin('feFuncG'));
        ct.appendChild(mkLin('feFuncB'));
        return ct;
    }

    function mkSCurveTableCT(inId, outId, strength) {
        const s = clamp(strength, 0, 2);

        const steps = 33;
        const vals = [];
        const toe = 0.20 + s * 0.06;
        const shoulder = 0.78 - s * 0.05;
        const shoulderGain = 0.16 + s * 0.10;

        for (let i = 0; i < steps; i++) {
            const x = i / (steps - 1);
            let y = x;

            if (x < toe) {
                const t = x / toe;
                const ss = t * t * (3 - 2 * t);
                y = x + (toe - x) * (0.10 + s * 0.10) * (1 - ss);
            }

            if (x > shoulder) {
                const t = (x - shoulder) / (1 - shoulder);
                const ss = t * t * (3 - 2 * t);
                y = x - shoulderGain * ss * t;
            }

            y = clamp(y, 0, 1);
            vals.push(y.toFixed(4));
        }

        const ct = document.createElementNS(svgNS, 'feComponentTransfer');
        ct.setAttribute('in', inId);
        ct.setAttribute('result', outId);

        const mkTable = (tag) => {
            const f = document.createElementNS(svgNS, tag);
            f.setAttribute('type', 'table');
            f.setAttribute('tableValues', vals.join(' '));
            return f;
        };

        ct.appendChild(mkTable('feFuncR'));
        ct.appendChild(mkTable('feFuncG'));
        ct.appendChild(mkTable('feFuncB'));
        return ct;
    }

    function mkProfileMatrixCT(prof) {
        const cm = document.createElementNS(svgNS, 'feColorMatrix');
        cm.setAttribute('type', 'matrix');

        let values = null;

        if (prof === 'film') {
            values =
                '1.06 0.02 0.00 0 -0.03 ' +
                '0.01 1.03 0.01 0 -0.02 ' +
                '0.00 0.03 1.05 0 -0.03 ' +
                '0    0    0    1  0';
        } else if (prof === 'anime') {
            values =
                '1.06 0.01 0.00 0 -0.012 ' +
                '0.00 1.07 0.01 0 -0.012 ' +
                '0.01 0.03 1.10 0 -0.016 ' +
                '0    0    0    1  0';
        } else if (prof === 'gaming') {
            values =
                '1.04 0.00 0.00 0 -0.010 ' +
                '0.00 1.04 0.00 0 -0.010 ' +
                '0.00 0.00 1.04 0 -0.010 ' +
                '0    0    0    1  0';
        } else if (prof === 'eyecare') {
            values =
                '1.08 0.00 0.00 0 0.00 ' +
                '0.15 1.05 0.00 0 0.00 ' +
                '0.25 0.00 0.50 0 0.00 ' +
                '0    0    0    1  0';
        } else {
            return null;
        }

        cm.setAttribute('values', values);
        return cm;
    }

    function userToneCss() {
        if (profile !== 'user') return '';

        const c = clamp(1.0 + (uDelta(u_contrast) * 0.04), 0.60, 1.60);
        const sat = clamp(1.0 + (uDelta(u_sat) * 0.05), 0.40, 1.80);
        const vib = clamp(1.0 + (uDelta(u_vib) * 0.02), 0.70, 1.35);
        const hue = clamp(uDelta(u_hue) * 3.0, -30, 30);

        const blk = clamp(uDelta(u_black) * 0.012, -0.12, 0.12);
        const wht = clamp(uDelta(u_white) * 0.012, -0.12, 0.12);
        const sh = clamp(uDelta(u_shadows) * 0.010, -0.10, 0.10);
        const hi = clamp(uDelta(u_highlights) * 0.010, -0.10, 0.10);

        const br = clamp(1.0 + (-blk + wht + sh + hi) * 0.6, 0.70, 1.35);

        const g = clamp(1.0 + (uDelta(u_gamma) * 0.025), 0.60, 1.60);
        const gBr = clamp(1.0 + (1.0 - g) * 0.18, 0.85, 1.20);
        const gCt = clamp(1.0 + (g - 1.0) * 0.10, 0.90, 1.15);

        const s = uDelta(u_sharp);
        const cssSharp = s > 0 ? ` drop-shadow(0 0 ${Math.max(0.001, (s / 10) * 0.35).toFixed(3)}px rgba(0,0,0,0.0))` : '';

        return ` brightness(${(br * gBr).toFixed(3)}) contrast(${(c * gCt).toFixed(3)}) saturate(${(sat * vib).toFixed(3)}) hue-rotate(${hue.toFixed(1)}deg)${cssSharp}`;
    }

    function buildFilter(svg, id, opts, radius, sharpenA, blurSigma, blackOffset, whiteAdj, dnVal, edgeVal, hdrVal, prof) {
        const { moody, teal, vib } = opts;

        const filter = document.createElementNS(svgNS, 'filter');
        filter.setAttribute('id', id);
        filter.setAttribute('color-interpolation-filters', 'sRGB');

        let last = 'SourceGraphic';

        if (blurSigma > 0) {
            const b = document.createElementNS(svgNS, 'feGaussianBlur');
            b.setAttribute('in', last);
            b.setAttribute('stdDeviation', String(radius));
            b.setAttribute('result', 'r_blur');
            filter.appendChild(b);
            last = 'r_blur';
        } else {
            const blur = document.createElementNS(svgNS, 'feGaussianBlur');
            blur.setAttribute('in', 'SourceGraphic');
            blur.setAttribute('stdDeviation', String(radius));
            blur.setAttribute('result', 'blur');
            filter.appendChild(blur);

            const comp = document.createElementNS(svgNS, 'feComposite');
            comp.setAttribute('in', 'SourceGraphic');
            comp.setAttribute('in2', 'blur');
            comp.setAttribute('operator', 'arithmetic');
            comp.setAttribute('k1', '0');
            comp.setAttribute('k2', String(1 + sharpenA));
            comp.setAttribute('k3', String(-sharpenA));
            comp.setAttribute('k4', '0');
            comp.setAttribute('result', 'r0');
            filter.appendChild(comp);

            last = 'r0';
        }

        if (blackOffset !== 0) {
            filter.appendChild(mkOffsetCT(last, 'r_bl', blackOffset));
            last = 'r_bl';
        }

        if (whiteAdj !== 0) {
            filter.appendChild(mkHighlightsTableCT(last, 'r_wl', whiteAdj));
            last = 'r_wl';
        }

        if (dnVal > 0) {
            const mix = dnToDenoiseMix(dnVal);
            const sig = dnToDenoiseSigma(dnVal);
            const [b, c] = mkDenoiseBlend(last, 'r_dn', sig, mix);
            filter.appendChild(b);
            filter.appendChild(c);
            last = 'r_dn';
        } else if (dnVal < 0) {
            const alpha = dnToGrainAlpha(dnVal);
            const parts = mkGrain(last, 'r_gr', alpha);
            parts.forEach(p => filter.appendChild(p));
            last = 'r_gr';
        }

        if (hdrVal !== 0) {
            if (hdrVal > 0) {
                const s = clamp(hdrVal, 0, 2);

                const clarityAmt = 0.55 + s * 0.55;
                const claritySigma = clamp(1.3 + radius * 0.75, 1.3, 3.6);
                const [b, c] = mkClarityHighpass(last, 'r_hdr_cl', claritySigma, clarityAmt);
                filter.appendChild(b);
                filter.appendChild(c);

                filter.appendChild(mkBlend(last, 'r_hdr_cl', 'r_hdr_clb', clamp(0.65 + s * 0.12, 0.65, 0.89)));
                last = 'r_hdr_clb';

                filter.appendChild(mkSCurveTableCT(last, 'r_hdr_tm', s));
                last = 'r_hdr_tm';

                const slope = 1.10 + s * 0.18;
                const intercept = -0.015 + s * 0.006;
                filter.appendChild(mkLinearCT(last, 'r_hdr_lin', slope, intercept));
                last = 'r_hdr_lin';

                const sat = document.createElementNS(svgNS, 'feColorMatrix');
                sat.setAttribute('type', 'saturate');
                sat.setAttribute('values', String(1.10 + s * 0.30));
                sat.setAttribute('in', last);
                sat.setAttribute('result', 'r_hdr_sat');
                filter.appendChild(sat);
                last = 'r_hdr_sat';
            } else {
                const s = clamp(-hdrVal, 0, 1);

                const mix = clamp(s * 0.55, 0, 0.55);
                const sig = clamp(0.9 + s * 1.8, 0.9, 2.7);
                const [b, c] = mkDenoiseBlend(last, 'r_hdr_soft', sig, mix);
                filter.appendChild(b);
                filter.appendChild(c);
                last = 'r_hdr_soft';

                const sat = document.createElementNS(svgNS, 'feColorMatrix');
                sat.setAttribute('type', 'saturate');
                sat.setAttribute('values', String(1.0 - s * 0.18));
                sat.setAttribute('in', last);
                sat.setAttribute('result', 'r_hdr_soft2');
                filter.appendChild(sat);
                last = 'r_hdr_soft2';
            }
        }

        // Apply color blindness filter if enabled
        if (cbFilter !== 'none') {
            const cbMatrix = getColorBlindnessMatrix(cbFilter);
            const cbCM = document.createElementNS(svgNS, 'feColorMatrix');
            cbCM.setAttribute('type', 'matrix');
            cbCM.setAttribute('in', last);
            cbCM.setAttribute('result', 'r_cb');
            cbCM.setAttribute('values', matToSvgValues(cbMatrix));
            filter.appendChild(cbCM);
            last = 'r_cb';
        }

        // Apply LUT matrix if a LUT profile is active (approximation)
        if (activeLutMatrix4x5 && Array.isArray(activeLutMatrix4x5) && activeLutMatrix4x5.length === 20 && activeLutProfileKey !== 'none') {
            const lutCM = document.createElementNS(svgNS, 'feColorMatrix');
            lutCM.setAttribute('type', 'matrix');
            lutCM.setAttribute('in', last);
            lutCM.setAttribute('result', 'r_lut');
            lutCM.setAttribute('values', matToSvgValues(activeLutMatrix4x5));
            filter.appendChild(lutCM);
            last = 'r_lut';
        }


        if (profile === 'user') {
            const rGain = rgbGainToFactor(u_r_gain);
            const gGain = rgbGainToFactor(u_g_gain);
            const bGain = rgbGainToFactor(u_b_gain);

            if (Math.abs(rGain - 1.0) > 0.01 || Math.abs(gGain - 1.0) > 0.01 || Math.abs(bGain - 1.0) > 0.01) {

                const rgbMatrix = matRGBGain(rGain, gGain, bGain);
                const rgbCM = document.createElementNS(svgNS, 'feColorMatrix');
                rgbCM.setAttribute('type', 'matrix');
                rgbCM.setAttribute('in', last);
                rgbCM.setAttribute('result', 'r_rgb');
                rgbCM.setAttribute('values', matToSvgValues(rgbMatrix));
                filter.appendChild(rgbCM);
                last = 'r_rgb';
            }
        }

        if (moody) {
            const ct = document.createElementNS(svgNS, 'feComponentTransfer');
            ct.setAttribute('in', last);
            ct.setAttribute('result', 'r1');
            ct.appendChild(mkGamma('feFuncR', 0.96, 1.14, -0.015));
            ct.appendChild(mkGamma('feFuncG', 0.96, 1.13, -0.015));
            ct.appendChild(mkGamma('feFuncB', 0.97, 1.11, -0.015));
            filter.appendChild(ct);

            const sat = document.createElementNS(svgNS, 'feColorMatrix');
            sat.setAttribute('type', 'saturate');
            sat.setAttribute('values', '0.90');
            sat.setAttribute('in', 'r1');
            sat.setAttribute('result', 'r2');
            filter.appendChild(sat);

            last = 'r2';
        }

        if (teal) {
            const cool = document.createElementNS(svgNS, 'feColorMatrix');
            cool.setAttribute('type', 'matrix');
            cool.setAttribute('values',
                '0.96 0.02 0.00 0 0 ' +
                '0.02 1.02 0.02 0 0 ' +
                '0.00 0.04 1.06 0 0 ' +
                '0    0    0    1 0'
            );
            cool.setAttribute('in', last);
            cool.setAttribute('result', 'r3');
            filter.appendChild(cool);

            const warm = document.createElementNS(svgNS, 'feColorMatrix');
            warm.setAttribute('type', 'matrix');
            warm.setAttribute('values',
                '1.10 0.02 0.00 0 0 ' +
                '0.02 1.00 0.00 0 0 ' +
                '0.00 0.00 0.90 0 0 ' +
                '0    0    0    1 0'
            );
            warm.setAttribute('in', 'r3');
            warm.setAttribute('result', 'r4');
            filter.appendChild(warm);

            const pop = document.createElementNS(svgNS, 'feColorMatrix');
            pop.setAttribute('type', 'saturate');
            pop.setAttribute('values', '1.08');
            pop.setAttribute('in', 'r4');
            pop.setAttribute('result', 'r4b');
            filter.appendChild(pop);

            last = 'r4b';
        }

        if (vib) {
            const vSat = document.createElementNS(svgNS, 'feColorMatrix');
            vSat.setAttribute('type', 'saturate');
            vSat.setAttribute('values', '1.35');
            vSat.setAttribute('in', last);
            vSat.setAttribute('result', 'r5');
            filter.appendChild(vSat);
            last = 'r5';
        }

        if (prof && (prof === 'film' || prof === 'anime' || prof === 'gaming' || prof === 'eyecare')) {
            const pm = mkProfileMatrixCT(prof);
            if (pm) {
                pm.setAttribute('in', last);
                pm.setAttribute('result', 'r_prof');
                filter.appendChild(pm);
                last = 'r_prof';

                const sat = document.createElementNS(svgNS, 'feColorMatrix');
                sat.setAttribute('type', 'saturate');
                sat.setAttribute('in', last);
                sat.setAttribute('result', 'r_prof_sat');
                if (prof === 'film') sat.setAttribute('values', '1.08');
                if (prof === 'anime') sat.setAttribute('values', '1.18');
                if (prof === 'gaming') sat.setAttribute('values', '1.06');
                if (prof === 'eyecare') sat.setAttribute('values', '0.90');
                filter.appendChild(sat);
                last = 'r_prof_sat';
            }
        }

        if (prof === 'anime') {

            const blur = document.createElementNS(svgNS, 'feGaussianBlur');
            blur.setAttribute('stdDeviation', '0.8');
            blur.setAttribute('in', last);
            blur.setAttribute('result', 'anime_denoised');
            filter.appendChild(blur);

            const sobel = document.createElementNS(svgNS, 'feConvolveMatrix');
            sobel.setAttribute('order', '3');
            sobel.setAttribute('kernelMatrix',
                '-1 -2 -1 ' +
                ' 0  0  0 ' +
                ' 1  2  1'
            );
            sobel.setAttribute('divisor', '1');
            sobel.setAttribute('in', 'anime_denoised');
            sobel.setAttribute('result', 'anime_edges');
            filter.appendChild(sobel);

            const componentTransfer = document.createElementNS(svgNS, 'feComponentTransfer');
            componentTransfer.setAttribute('in', 'anime_edges');
            componentTransfer.setAttribute('result', 'anime_darkEdges');

            const funcR = document.createElementNS(svgNS, 'feFuncR');
            funcR.setAttribute('type', 'linear');
            funcR.setAttribute('slope', '2.2');
            funcR.setAttribute('intercept', '-0.3');
            componentTransfer.appendChild(funcR);

            const funcG = funcR.cloneNode();
            const funcB = funcR.cloneNode();
            componentTransfer.appendChild(funcG);
            componentTransfer.appendChild(funcB);
            filter.appendChild(componentTransfer);

            const threshold = document.createElementNS(svgNS, 'feComponentTransfer');
            threshold.setAttribute('in', 'anime_darkEdges');
            threshold.setAttribute('result', 'anime_threshold');

            const tFuncR = document.createElementNS(svgNS, 'feFuncR');
            tFuncR.setAttribute('type', 'linear');
            tFuncR.setAttribute('slope', '3');
            tFuncR.setAttribute('intercept', '-0.4');
            threshold.appendChild(tFuncR);

            const tFuncG = tFuncR.cloneNode();
            const tFuncB = tFuncR.cloneNode();
            threshold.appendChild(tFuncG);
            threshold.appendChild(tFuncB);
            filter.appendChild(threshold);

            const blend = document.createElementNS(svgNS, 'feComposite');
            blend.setAttribute('operator', 'arithmetic');
            blend.setAttribute('k1', '0');
            blend.setAttribute('k2', '1');
            blend.setAttribute('k3', '0.3');
            blend.setAttribute('k4', '0');
            blend.setAttribute('in', last);
            blend.setAttribute('in2', 'anime_threshold');
            blend.setAttribute('result', 'r_anime_lines');
            filter.appendChild(blend);

            last = 'r_anime_lines';
        }


        if (edgeVal > 0.0001) {
            const edgeStrength = Math.pow(clamp(edgeVal, 0.0, 1.0), 2.2);
            const edgeSigma = 0.30 + edgeStrength * 0.70;
            const preBlur = document.createElementNS(svgNS, 'feGaussianBlur');
            preBlur.setAttribute('stdDeviation', String(edgeSigma));
            preBlur.setAttribute('in', last);
            preBlur.setAttribute('result', 'r_edge_pre');
            filter.appendChild(preBlur);

            const sobelX = document.createElementNS(svgNS, 'feConvolveMatrix');
            sobelX.setAttribute('order', '3');
            sobelX.setAttribute('kernelMatrix', '-1 0 1 -2 0 2 -1 0 1');
            sobelX.setAttribute('divisor', '1');
            sobelX.setAttribute('bias', '0');
            sobelX.setAttribute('preserveAlpha', 'true');
            sobelX.setAttribute('in', 'r_edge_pre');
            sobelX.setAttribute('result', 'r_edge_sx');
            filter.appendChild(sobelX);

            const sobelY = document.createElementNS(svgNS, 'feConvolveMatrix');
            sobelY.setAttribute('order', '3');
            sobelY.setAttribute('kernelMatrix', '-1 -2 -1 0 0 0 1 2 1');
            sobelY.setAttribute('divisor', '1');
            sobelY.setAttribute('bias', '0');
            sobelY.setAttribute('preserveAlpha', 'true');
            sobelY.setAttribute('in', 'r_edge_pre');
            sobelY.setAttribute('result', 'r_edge_sy');
            filter.appendChild(sobelY);

            const edgeMix = document.createElementNS(svgNS, 'feBlend');
            edgeMix.setAttribute('mode', 'lighten');
            edgeMix.setAttribute('in', 'r_edge_sx');
            edgeMix.setAttribute('in2', 'r_edge_sy');
            edgeMix.setAttribute('result', 'r_edge_mix');
            filter.appendChild(edgeMix);

            const edgeMask = document.createElementNS(svgNS, 'feComponentTransfer');
            edgeMask.setAttribute('in', 'r_edge_mix');
            edgeMask.setAttribute('result', 'r_edge_mask');

            const edgeSlope = -(0.35 + edgeStrength * 3.65);
            const edgeIntercept = 1.0;
            const feR = document.createElementNS(svgNS, 'feFuncR');
            feR.setAttribute('type', 'linear');
            feR.setAttribute('slope', String(edgeSlope));
            feR.setAttribute('intercept', String(edgeIntercept));
            edgeMask.appendChild(feR);

            const feG = document.createElementNS(svgNS, 'feFuncG');
            feG.setAttribute('type', 'linear');
            feG.setAttribute('slope', String(edgeSlope));
            feG.setAttribute('intercept', String(edgeIntercept));
            edgeMask.appendChild(feG);

            const feB = document.createElementNS(svgNS, 'feFuncB');
            feB.setAttribute('type', 'linear');
            feB.setAttribute('slope', String(edgeSlope));
            feB.setAttribute('intercept', String(edgeIntercept));
            edgeMask.appendChild(feB);

            const feA = document.createElementNS(svgNS, 'feFuncA');
            feA.setAttribute('type', 'identity');
            edgeMask.appendChild(feA);
            filter.appendChild(edgeMask);

            const edgeComposite = document.createElementNS(svgNS, 'feBlend');
            edgeComposite.setAttribute('mode', 'multiply');
            edgeComposite.setAttribute('in', last);
            edgeComposite.setAttribute('in2', 'r_edge_mask');
            edgeComposite.setAttribute('result', 'r_edge_final');
            filter.appendChild(edgeComposite);
            last = 'r_edge_final';
        }

        const autoCM = document.createElementNS(svgNS, 'feColorMatrix');
        autoCM.setAttribute('type', 'matrix');
        autoCM.setAttribute('in', last);
        autoCM.setAttribute('result', 'r_auto');
        autoCM.setAttribute('data-gvf-auto', '1');
        autoCM.setAttribute('values', autoMatrixStr || matToSvgValues(matIdentity4x5()));
        filter.appendChild(autoCM);
        last = 'r_auto';

        // Inject enabled SVG-type custom filter codes into filter pipeline (WebGL entries handled separately)
        if (Array.isArray(customSvgCodes)) {
            customSvgCodes.filter(e => e && e.enabled && e.type !== 'webgl').forEach((entry, idx) => {
                const nodes = parseCustomSvgCode(entry.code);
                if (!nodes || !nodes.length) return;
                nodes.forEach((node, ni) => {
                    const imported = document.importNode(node, true);
                    const resultId = 'r_cust_' + idx + '_' + ni;
                    if (!imported.getAttribute('in')) imported.setAttribute('in', last);
                    imported.setAttribute('result', resultId);
                    filter.appendChild(imported);
                    last = resultId;
                });
            });
        }

        const merge = document.createElementNS(svgNS, 'feMerge');
        const n1 = document.createElementNS(svgNS, 'feMergeNode');
        n1.setAttribute('in', last);
        merge.appendChild(n1);
        filter.appendChild(merge);

        svg.appendChild(filter);
    }

    function ensureSvgFilter(force = false) {
        const SL = Number(normSL().toFixed(1));
        const SR = Number(normSR().toFixed(1));
        const R = Number(getRadius().toFixed(1));
        const A = Number(getSharpenA().toFixed(3));
        const BS = Number(getBlurSigma().toFixed(3));
        const BL = Number(normBL().toFixed(1));
        const WL = Number(normWL().toFixed(1));
        const DN = Number(normDN().toFixed(1));
        const HDR = Number(normHDR().toFixed(2));
        const EDGE = Number(normEDGE().toFixed(2));
        const P = (profile || 'off');
        const CB = cbFilter;

        const LUTN = String(activeLutProfileKey || 'none');
        const uSig = [
            normU(u_contrast), normU(u_black), normU(u_white), normU(u_highlights), normU(u_shadows),
            normU(u_sat), normU(u_vib), normU(u_sharp), normU(u_gamma), normU(u_grain), normU(u_hue),
            normRGB(u_r_gain), normRGB(u_g_gain), normRGB(u_b_gain)
        ].map(x => Number(x).toFixed(1)).join(',');

        const customSig = customSvgCodes.filter(e => e && e.enabled && e.type !== 'webgl').map(e => e.id + ':' + e.code).join('||');
        const want = `${SL}|${SR}|${R}|${A}|${BS}|${BL}|${WL}|${DN}|${EDGE}|${HDR}|${P}|U:${uSig}|CB:${CB}|LUT:${LUTN}|CSVG:${customSig}`;

        const existing = document.getElementById(SVG_ID);
        if (existing) {
            const has = existing.getAttribute('data-params') || '';
            if (has === want && !force) {
                updateAutoMatrixInSvg(autoMatrixStr);
                return;
            }
            if (has === want && force) {
                existing.remove();
            }

            if (!force) {
                updateAutoMatrixInSvg(autoMatrixStr);
                return;
            }

            existing.remove();
        }

        const svg = document.createElementNS(svgNS, 'svg');
        svg.id = SVG_ID;
        svg.setAttribute('data-params', want);
        svg.setAttribute('width', '0');
        svg.setAttribute('height', '0');
        svg.style.position = 'absolute';
        svg.style.left = '-9999px';
        svg.style.top = '-9999px';

        const blackOffset = blackToOffset(BL);
        const whiteAdj = whiteToHiAdj(WL);

        buildFilter(svg, 'gvf_s', { moody: false, teal: false, vib: false }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_sm', { moody: true, teal: false, vib: false }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_st', { moody: false, teal: true, vib: false }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_sv', { moody: false, teal: false, vib: true }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_smt', { moody: true, teal: true, vib: false }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_smv', { moody: true, teal: false, vib: true }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_stv', { moody: false, teal: true, vib: true }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);
        buildFilter(svg, 'gvf_smtv', { moody: true, teal: true, vib: true }, R, A, BS, blackOffset, whiteAdj, DN, EDGE, HDR, P);

        (document.body || document.documentElement).appendChild(svg);

        updateAutoMatrixInSvg(autoMatrixStr);
    }

    function pickComboId() {
        const m = !!darkMoody;
        const t = !!tealOrange;
        const v = !!vibrantSat;

        if (m && t && v) return 'gvf_smtv';
        if (m && t && !v) return 'gvf_smt';
        if (m && !t && v) return 'gvf_smv';
        if (!m && t && v) return 'gvf_stv';
        if (m && !t && !v) return 'gvf_sm';
        if (!m && t && !v) return 'gvf_st';
        if (!m && !t && v) return 'gvf_sv';
        return 'gvf_s';
    }

    function profileToneCss() {
        if (profile === 'film') return ' brightness(1.01) contrast(1.08) saturate(1.08)';
        if (profile === 'anime') return ' brightness(1.03) contrast(1.10) saturate(1.16)';
        if (profile === 'gaming') return ' brightness(1.01) contrast(1.12) saturate(1.06)';
        if (profile === 'eyecare') return ' brightness(1.05) contrast(0.96) saturate(0.88) hue-rotate(-12deg)';
        return '';
    }

    function applyFilter(opts = {}) {
        if (renderMode === 'gpu') {
            applyGpuFilter();
            return;
        }

        let style = document.getElementById(STYLE_ID);

        const nothingOn =
            !enabled && !darkMoody && !tealOrange && !vibrantSat && normEDGE() === 0 && normHDR() === 0 && (profile === 'off') && !autoOn && cbFilter === 'none';

        if (nothingOn) {
            if (style) style.remove();
            scheduleOverlayUpdate();
            return;
        }

        const skipSvgIfPossible = !!opts.skipSvgIfPossible;
        const svgExists = !!document.getElementById(SVG_ID);

        if (!skipSvgIfPossible || !svgExists) {
            ensureSvgFilter(true);
        }

        if (!style) {
            style = document.createElement('style');
            style.id = STYLE_ID;
            document.head.appendChild(style);
        }

        const baseTone = enabled ? ' brightness(1.02) contrast(1.05) saturate(1.21)' : '';
        const profTone = profileToneCss();
        const userTone = userToneCss();

        const outlineCss = (PROFILE_VIDEO_OUTLINE && profile !== 'off')
            ? `outline: 2px solid ${(PROF[profile] || PROF.off).color} !important; outline-offset: -2px;`
            : `outline: none !important;`;

        style.textContent = `
      video {
        will-change: filter;
        transform: translateZ(0);
        filter: url("#${pickComboId()}")${baseTone}${profTone}${userTone} !important;
        ${outlineCss}
      }
    `;

        scheduleOverlayUpdate();
    }

    function getSelfCode() {
        try {
            if (document.currentScript && document.currentScript.textContent) {
                const t = document.currentScript.textContent.trim();
                if (t.length > 200) return t;
            }
        } catch (_) { }
        try {
            if (typeof GM_info !== 'undefined' && GM_info.script && GM_info.script.source) {
                return String(GM_info.script.source || '');
            }
        } catch (_) { }
        return null;
    }

    function injectIntoIframe(iframe, code) {
        try {
            const doc = iframe.contentDocument;
            const win = iframe.contentWindow;
            if (!doc || !win) return;
            if (win.__GLOBAL_VIDEO_FILTER__) return;
            if (!code) return;

            const s = doc.createElement('script');
            s.type = 'text/javascript';
            s.textContent = code;
            (doc.head || doc.documentElement).appendChild(s);
            s.remove();
        } catch (_) { }
    }

    function watchIframes() {
        const code = getSelfCode();
        if (!code) return;

        const scan = () => document.querySelectorAll('iframe').forEach(ifr => injectIntoIframe(ifr, code));
        scan();

        document.addEventListener('load', (e) => {
            const t = e.target;
            if (t && t.tagName && t.tagName.toLowerCase() === 'iframe') injectIntoIframe(t, code);
        }, true);

        new MutationObserver(scan).observe(document.documentElement, { childList: true, subtree: true });
    }

    let _globalSyncApplyTimer = null;
    function scheduleGlobalSyncApply(delay = 50) {
        if (_globalSyncApplyTimer) clearTimeout(_globalSyncApplyTimer);
        _globalSyncApplyTimer = setTimeout(() => {
            _globalSyncApplyTimer = null;
            try {
                setAutoOn(autoOn);
                if (renderMode === 'gpu') {
                    applyGpuFilter();
                } else {
                    regenerateSvgImmediately();
                }
                scheduleOverlayUpdate();
            } catch (e) {
                logW('Global sync apply failed:', e);
            }
        }, Math.max(0, Number(delay) || 0));
    }

    function listenGlobalSync() {
        const profileAutoSaveKeys = new Set([
            K.enabled,
            K.moody,
            K.teal,
            K.vib,
            K.SL,
            K.SR,
            K.BL,
            K.WL,
            K.DN,
            K.HDR,
            K.PROF,
            K.RENDER_MODE,
            K.AUTO_ON,
            K.AUTO_STRENGTH,
            K.AUTO_LOCK_WB,
            K.U_CONTRAST,
            K.U_BLACK,
            K.U_WHITE,
            K.U_HIGHLIGHTS,
            K.U_SHADOWS,
            K.U_SAT,
            K.U_VIB,
            K.U_SHARP,
            K.U_GAMMA,
            K.U_GRAIN,
            K.U_HUE,
            K.U_R_GAIN,
            K.U_G_GAIN,
            K.U_B_GAIN,
            K.CB_FILTER,
            K.LUT_ACTIVE_PROFILE
        ]);

        const sync = (changedKey) => {
            if (_isSwitchingUserProfile) return;
            if (isValueSyncSuppressed()) return;
            if (!_applyingRemoteProfileSync && (changedKey === K.USER_PROFILES || changedKey === K.USER_PROFILES_REV || changedKey === K.ACTIVE_USER_PROFILE)) {
                // Manual save only: ignore automatic user-profile sync events to prevent profile reverts.
                return;
            }
            if (_suspendSync) return;

            _inSync = true;
            try {
                enabled = !!gmGet(K.enabled, enabled);
                darkMoody = !!gmGet(K.moody, darkMoody);
                tealOrange = !!gmGet(K.teal, tealOrange);
                vibrantSat = !!gmGet(K.vib, vibrantSat);
                iconsShown = !!gmGet(K.icons, iconsShown);

                sl = Number(gmGet(K.SL, sl));
                sr = Number(gmGet(K.SR, sr));
                bl = Number(gmGet(K.BL, bl));
                wl = Number(gmGet(K.WL, wl));
                dn = Number(gmGet(K.DN, dn));
                hdr = Number(gmGet(K.HDR, hdr));

                profile = String(gmGet(K.PROF, profile)).toLowerCase();
                if (!['off', 'film', 'anime', 'gaming', 'eyecare', 'user'].includes(profile)) profile = 'off';

                renderMode = String(gmGet(K.RENDER_MODE, renderMode)).toLowerCase();
                if (!['svg', 'gpu'].includes(renderMode)) renderMode = 'svg';

                gradingHudShown = !!gmGet(K.G_HUD, gradingHudShown);
                ioHudShown = !!gmGet(K.I_HUD, ioHudShown);
                scopesHudShown = !!gmGet(K.S_HUD, scopesHudShown);

                u_contrast = Number(gmGet(K.U_CONTRAST, u_contrast));
                u_black = Number(gmGet(K.U_BLACK, u_black));
                u_white = Number(gmGet(K.U_WHITE, u_white));
                u_highlights = Number(gmGet(K.U_HIGHLIGHTS, u_highlights));
                u_shadows = Number(gmGet(K.U_SHADOWS, u_shadows));
                u_sat = Number(gmGet(K.U_SAT, u_sat));
                u_vib = Number(gmGet(K.U_VIB, u_vib));
                u_sharp = Number(gmGet(K.U_SHARP, u_sharp));
                u_gamma = Number(gmGet(K.U_GAMMA, u_gamma));
                u_grain = Number(gmGet(K.U_GRAIN, u_grain));
                u_hue = Number(gmGet(K.U_HUE, u_hue));

                u_r_gain = Number(gmGet(K.U_R_GAIN, u_r_gain));
                u_g_gain = Number(gmGet(K.U_G_GAIN, u_g_gain));
                u_b_gain = Number(gmGet(K.U_B_GAIN, u_b_gain));

                autoOn = !!gmGet(K.AUTO_ON, autoOn);
                notify = !!gmGet(K.NOTIFY, notify);
                autoStrength = clamp(Number(gmGet(K.AUTO_STRENGTH, autoStrength)), 0, 1);
                autoLockWB = !!gmGet(K.AUTO_LOCK_WB, autoLockWB);

                cbFilter = String(gmGet(K.CB_FILTER, cbFilter)).toLowerCase();
                if (!['none', 'protanopia', 'deuteranopia', 'tritanomaly'].includes(cbFilter)) cbFilter = 'none';

                // Reload custom SVG codes and refresh modal if open
                if (changedKey === K.CUSTOM_SVG_CODES) {
                    loadCustomSvgCodes();
                    regenerateSvgImmediately();
                    updateCustomWebglOverlays();
                    updateCustomCanvas2DOverlays();
                    const modal = document.getElementById('gvf-custom-svg-modal');
                    if (modal && modal._gvfRenderList) modal._gvfRenderList();
                    const badge = document.getElementById('gvf-svg-codes-count');
                    if (badge) {
                        const ac = customSvgCodes.filter(e => e.enabled).length;
                        badge.textContent = customSvgCodes.length ? `${ac}/${customSvgCodes.length} active` : '';
                    }
                    _inSync = false;
                    return;
                }

                // Debug/Load settings from storage
                logs = !!gmGet(K.LOGS, logs);
                debug = !!gmGet(K.DEBUG, debug);
                LOG.on = logs;

                scheduleGlobalSyncApply(profileAutoSaveKeys.has(changedKey) ? 70 : 40);
            } finally {
                _inSync = false;
            }
        };

        Object.values(K).forEach(key => {
            try {
                GM_addValueChangeListener(key, function() {
                    sync(key);
                });
            } catch (_) { }
        });

        try {
            window.addEventListener('storage', function(ev) {
                const key = ev && ev.key ? String(ev.key) : '';
                if (!key) return;
                // User profile storage sync is intentionally disabled here.
                // Manual save only: do not auto-switch or auto-merge profiles across tabs.
            }, false);
        } catch (_) { }
    }

    function cycleProfile() {
        const order = ['off', 'film', 'anime', 'gaming', 'eyecare', 'user'];
        const cur = order.indexOf(profile);
        profile = order[(cur < 0 ? 0 : (cur + 1)) % order.length];
        gmSet(K.PROF, profile);
        log('Profile cycled:', profile);
        showProfileCycleNotification(profile);

        // Save current settings in active profile
        updateCurrentProfileSettings();

        if (renderMode === 'gpu') {
            applyGpuFilter();
        } else {
            regenerateSvgImmediately();
        }
        scheduleOverlayUpdate();
    }

    function toggleGradingHud() {
        gradingHudShown = !gradingHudShown;
        gmSet(K.G_HUD, gradingHudShown);
        logToggle('Grading HUD (Ctrl+Alt+G)', gradingHudShown);
        scheduleOverlayUpdate();
    }

    function toggleIOHud() {
        ioHudShown = !ioHudShown;
        gmSet(K.I_HUD, ioHudShown);
        logToggle('IO HUD (Ctrl+Alt+I)', ioHudShown);
        scheduleOverlayUpdate();
    }

    function toggleScopesHud() {
        scopesHudShown = !scopesHudShown;
        gmSet(K.S_HUD, scopesHudShown);
        logToggle('Scopes HUD (Ctrl+Alt+S)', scopesHudShown);
        scheduleOverlayUpdate();

        if (scopesHudShown) {
            startScopesLoop();
        } else {
            document.querySelectorAll('.gvf-scope-luma [data-index]').forEach(bar => {
                bar.style.height = '2px';
            });
            document.querySelectorAll('.gvf-scope-red [data-index]').forEach(bar => {
                bar.style.height = '2px';
            });
            document.querySelectorAll('.gvf-scope-green [data-index]').forEach(bar => {
                bar.style.height = '2px';
            });
            document.querySelectorAll('.gvf-scope-blue [data-index]').forEach(bar => {
                bar.style.height = '2px';
            });
            const satFill = document.querySelector('.gvf-scope-sat-fill');
            if (satFill) satFill.style.width = '0%';
            const satValue = document.querySelector('.gvf-scope-sat-value');
            if (satValue) satValue.textContent = '0.00';

            const avgYEl = document.querySelector('.gvf-scope-avg-y');
            if (avgYEl) avgYEl.textContent = 'Y: 0.00';
            const avgRGBEl = document.querySelector('.gvf-scope-avg-rgb');
            if (avgRGBEl) avgRGBEl.textContent = 'RGB: 0.00';
            const avgSatEl = document.querySelector('.gvf-scope-avg-sat');
            if (avgSatEl) avgSatEl.textContent = 'Sat: 0.00';
        }
    }

    // -------------------------
    // Auto-Import LUT Profiles from URL (runs once when no LUT profiles are stored)
    // -------------------------
    async function autoImportLutProfilesFromUrl(url) {
        try {
            if (Array.isArray(lutProfiles) && lutProfiles.length > 0) {
                log('autoImportLutProfilesFromUrl: LUT profiles already present, skipping auto-import.');
                return;
            }
            log('autoImportLutProfilesFromUrl: No LUT profiles found – fetching from', url);
            const rawUrl = 'https://raw.githubusercontent.com/nextscript/Ultimate-Video-Enhancer/main/LUTsProfiles_v2.0.zip';
            const candidates = [
                rawUrl,
                'https://api.allorigins.win/raw?url=' + encodeURIComponent(rawUrl),
                'https://corsproxy.io/?' + encodeURIComponent(rawUrl),
                'https://proxy.cors.sh/' + rawUrl,
            ];
            let response = null;
            for (const c of candidates) {
                try { const r = await fetch(c); if (r.ok) { response = r; break; } } catch (_) { }
            }
            if (!response) { logW('autoImportLutProfilesFromUrl: All fetch attempts failed.'); return; }
            if (!response.ok) {
                logW('autoImportLutProfilesFromUrl: Fetch failed:', response.status, response.statusText);
                return;
            }
            const blob = await response.blob();
            const fileName = url.split('/').pop() || 'LUTsProfiles.zip';
            const file = new File([blob], fileName, { type: 'application/zip' });
            const result = await importLutProfilesFromZipOrJsonFile(file);
            if (result && result.ok) {
                log('autoImportLutProfilesFromUrl:', result.msg);
                try { showValueNotification('LUT Import', result.msg, '#4cff6a'); } catch (_) { }
            } else {
                logW('autoImportLutProfilesFromUrl: Import failed –', result && result.msg);
            }
        } catch (e) {
            logW('autoImportLutProfilesFromUrl error:', e);
        }
    }

    function init() {
        const isFirefoxBrowser = isFirefox();
        if (activeUserProfile && activeUserProfile.settings && typeof activeUserProfile.settings === 'object') {
            try {
                applyUserProfileSettings(activeUserProfile.settings);
            } catch (e) {
                logW('Failed to restore active user profile on init:', e);
            }
        }

        sl = normSL(); gmSet(K.SL, sl);
        sr = normSR(); gmSet(K.SR, sr);
        bl = normBL(); gmSet(K.BL, bl);
        wl = normWL(); gmSet(K.WL, wl);
        dn = normDN(); gmSet(K.DN, dn);
        hdr = normHDR(); gmSet(K.HDR, hdr);
        if (hdr !== 0) gmSet(K.HDR_LAST, hdr);

        u_contrast = normU(u_contrast); gmSet(K.U_CONTRAST, u_contrast);
        u_black = normU(u_black); gmSet(K.U_BLACK, u_black);
        u_white = normU(u_white); gmSet(K.U_WHITE, u_white);
        u_highlights = normU(u_highlights); gmSet(K.U_HIGHLIGHTS, u_highlights);
        u_shadows = normU(u_shadows); gmSet(K.U_SHADOWS, u_shadows);
        u_sat = normU(u_sat); gmSet(K.U_SAT, u_sat);
        u_vib = normU(u_vib); gmSet(K.U_VIB, u_vib);
        u_sharp = normU(u_sharp); gmSet(K.U_SHARP, u_sharp);
        u_gamma = normU(u_gamma); gmSet(K.U_GAMMA, u_gamma);
        u_grain = normU(u_grain); gmSet(K.U_GRAIN, u_grain);
        u_hue = normU(u_hue); gmSet(K.U_HUE, u_hue);

        u_r_gain = normRGB(u_r_gain); gmSet(K.U_R_GAIN, u_r_gain);
        u_g_gain = normRGB(u_g_gain); gmSet(K.U_G_GAIN, u_g_gain);
        u_b_gain = normRGB(u_b_gain); gmSet(K.U_B_GAIN, u_b_gain);

        gmSet(K.G_HUD, gradingHudShown);
        gmSet(K.I_HUD, ioHudShown);
        gmSet(K.S_HUD, scopesHudShown);

        if (!['off', 'film', 'anime', 'gaming', 'eyecare', 'user'].includes(profile)) profile = 'off';
        gmSet(K.PROF, profile);

        if (!['svg', 'gpu'].includes(renderMode)) renderMode = 'svg';
        gmSet(K.RENDER_MODE, renderMode);

        gmSet(K.AUTO_ON, autoOn);
        gmSet(K.AUTO_STRENGTH, autoStrength);
        gmSet(K.AUTO_LOCK_WB, autoLockWB);

        gmSet(K.CB_FILTER, cbFilter);

        gmSet(K.LOGS, logs);
        gmSet(K.DEBUG, debug);

        // Manual save only: do not auto-save user profiles during init.

        setAutoDotState(autoOn ? (debug ? 'idle' : 'off') : 'off');

        autoMatrixStr = matToSvgValues(autoOn ? buildAutoMatrixValues() : matIdentity4x5());
        _autoLastMatrixStr = autoMatrixStr;
        AUTO.lastGoodMatrixStr = autoMatrixStr;
        AUTO.lastAppliedMs = 0;

        loadCustomSvgCodes();

        if (renderMode === 'gpu') {
            applyGpuFilter();
        } else {
            regenerateSvgImmediately();
        }

        listenGlobalSync();
        watchIframes();
        primeAutoOnVideoActivity();

        // Ensure GLSL overlays are created as soon as any video becomes ready
        ['canplay', 'loadedmetadata', 'play', 'playing'].forEach(evt => {
            document.addEventListener(evt, scheduleOverlayUpdate, { passive: true, capture: true });
        });

        ensureAutoLoop();
        setAutoOn(autoOn);

        if (scopesHudShown) startScopesLoop();

        // Initialize config menu (but do not display)
        createConfigMenu();

        log('Init complete with WebGL2 Canvas Pipeline! RGB Gain now works correctly!', {
            enabled, darkMoody, tealOrange, vibrantSat, iconsShown,
            hdr: normHDR(), profile, renderMode,
            autoOn, autoStrength: Number(autoStrength.toFixed(2)), autoLockWB,
            scopesHudShown,
            rgb: { r_gain: u_r_gain, g_gain: u_g_gain, b_gain: u_b_gain },
            adaptiveFps: { min: ADAPTIVE_FPS.MIN, max: ADAPTIVE_FPS.MAX, current: ADAPTIVE_FPS.current },
            motionThresh: AUTO.motionThresh,
            motionMinFrames: AUTO.motionMinFrames,
            statsAlpha: AUTO.statsAlpha,
            gpuPipeline: renderMode === 'gpu',
            branchlessShader: true,
            debug: debug,
            logs: logs,
            colorBlindnessFilter: cbFilter,
            isFirefox: isFirefoxBrowser,
            bugfixes: 'REC.stopRequested evaluated, AUTO.blink reset, null check in updateAutoMatrixInSvg',
            userProfiles: userProfiles.length,
            activeProfile: activeUserProfile?.name,
            newFeatures: 'Shift+Q profile cycling, 3-second on-screen notification, real EDG slider with dark-edge Sobel detection'
        });

        document.addEventListener('keydown', (e) => {
            const tag = (e.target && e.target.tagName || '').toLowerCase();
            if (tag === 'input' || tag === 'textarea' || e.isComposing) return;

            const k = (e.key || '').toLowerCase();

            // Custom filter hotkeys — single key, no modifier required
            if (!e.ctrlKey && !e.altKey && !e.shiftKey && !e.metaKey) {
                const hkEntry = customSvgCodes.find(e2 => e2.hotkey && e2.hotkey.toLowerCase() === k);
                if (hkEntry) {
                    e.preventDefault();
                    hkEntry.enabled = !hkEntry.enabled;
                    saveCustomSvgCodes();
                    regenerateSvgImmediately();
                    updateCustomWebglOverlays();
                    updateCustomCanvas2DOverlays();
                    showToggleNotification(hkEntry.label || 'Custom Filter', hkEntry.enabled);
                    return;
                }
            }

            // NEW: Shift+Q for profile cycling
            if (e.shiftKey && !e.ctrlKey && !e.altKey && k === PROFILE_CYCLE_KEY) {
                e.preventDefault();
                log('Shift+Q pressed - cycling to next profile');
                cycleToNextProfile();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === SCOPES_KEY) {
                e.preventDefault();
                toggleScopesHud();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === IO_HUD_KEY) {
                e.preventDefault();
                toggleIOHud();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === GRADE_HUD_KEY) {
                e.preventDefault();
                toggleGradingHud();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === GPU_MODE_KEY) {
                e.preventDefault();
                toggleRenderMode();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === PROF_TOGGLE_KEY) {
                e.preventDefault();
                cycleProfile();
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === HDR_TOGGLE_KEY) {
                e.preventDefault();
                const cur = normHDR();
                if (cur === 0) {
                    const last = Number(gmGet(K.HDR_LAST, 0.3));
                    hdr = clamp(last || 1.2, -1.0, 2.0);
                    logToggle('HDR (Ctrl+Alt+P)', true, `value=${normHDR().toFixed(2)}`);
                    showValueNotification('HDR', `Enabled (${normHDR().toFixed(2)})`, '#4cff6a');
                } else {
                    gmSet(K.HDR_LAST, cur);
                    hdr = 0;
                    logToggle('HDR (Ctrl+Alt+P)', false);
                    showToggleNotification('HDR', false);
                }
                gmSet(K.HDR, normHDR());

                // Save current settings in active profile
                updateCurrentProfileSettings();

                if (renderMode === 'gpu') {
                    applyGpuFilter();
                } else {
                    regenerateSvgImmediately();
                }
                return;
            }

            if (e.ctrlKey && e.altKey && !e.shiftKey && k === AUTO_KEY) {
                e.preventDefault();
                setAutoOn(!autoOn);
                return;
            }

            if (!(e.ctrlKey && e.altKey) || e.shiftKey) return;

            if (k === HK.base) {
                enabled = !enabled; gmSet(K.enabled, enabled); e.preventDefault(); logToggle('Base (Ctrl+Alt+B)', enabled); showToggleNotification('Base Tone Chain', enabled);
                updateCurrentProfileSettings();
                if (renderMode === 'gpu') applyGpuFilter(); else regenerateSvgImmediately(); return;
            }
            if (k === HK.moody) {
                darkMoody = !darkMoody; gmSet(K.moody, darkMoody); e.preventDefault(); logToggle('Dark&Moody (Ctrl+Alt+D)', darkMoody); showToggleNotification('Dark & Moody', darkMoody);
                updateCurrentProfileSettings();
                if (renderMode === 'gpu') applyGpuFilter(); else regenerateSvgImmediately(); return;
            }
            if (k === HK.teal) {
                tealOrange = !tealOrange; gmSet(K.teal, tealOrange); e.preventDefault(); logToggle('Teal&Orange (Ctrl+Alt+O)', tealOrange); showToggleNotification('Teal & Orange', tealOrange);
                updateCurrentProfileSettings();
                if (renderMode === 'gpu') applyGpuFilter(); else regenerateSvgImmediately(); return;
            }
            if (k === HK.vib) {
                vibrantSat = !vibrantSat; gmSet(K.vib, vibrantSat); e.preventDefault(); logToggle('Vibrant (Ctrl+Alt+V)', vibrantSat); showToggleNotification('Vibrant & Saturated', vibrantSat);
                updateCurrentProfileSettings();
                if (renderMode === 'gpu') applyGpuFilter(); else regenerateSvgImmediately(); return;
            }
            if (k === HK.icons) {
                iconsShown = !iconsShown; gmSet(K.icons, iconsShown); e.preventDefault(); logToggle('Overlay Icons (Ctrl+Alt+H)', iconsShown); scheduleOverlayUpdate(); return;
            }
        });

        window.addEventListener('scroll', scheduleOverlayUpdate, { passive: true });
        window.addEventListener('resize', scheduleOverlayUpdate, { passive: true });

        document.addEventListener('fullscreenchange', onFsChange);
        document.addEventListener('webkitfullscreenchange', onFsChange);

        new MutationObserver(() => {
            if (!document.getElementById(SVG_ID) && renderMode === 'svg') {
                regenerateSvgImmediately();
            }
            scheduleOverlayUpdate();
        }).observe(document.documentElement, { childList: true, subtree: true });

        scheduleOverlayUpdate();
    }

    document.readyState === 'loading'
        ? document.addEventListener('DOMContentLoaded', init, { once: true })
        : init();


})();
