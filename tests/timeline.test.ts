import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { AudioStreamInfo, EffectState, MediaInfo, Timeline, TimelineClip, UveProjectV1, VideoStreamInfo } from '../shared/types';
import { DEFAULT_EXPORT, UVE_SOURCE } from '../shared/types';
import { DEFAULT_SETTINGS } from '../shared/uve/settings';
import { expertDefaults } from '../shared/uve/expert';
import {
    DEFAULT_FRAME_RATE, addMarker, addSource, addTrack, audioWindowWeight, buildAudioGraph, buildPlan, channelFold,
    clearCurve, clipById, clipEnd, clipsOn, dbToLin, defaultTimeline, deleteClip, duplicateClip, emptyMediaInfo,
    emptyTimeline, frameRateFromMedia, fpsArgument, handleNeed, handleSides, insertClip, isAligned,
    isTransitionKind, makeClip, mediaHeadroom, migrateProjectV1toV2, moveClip, moveTrack, panLaw, patchClip,
    patchTrack, removeCurvePoint, removeMarker, removeSource, removeTrack, renameMarker, rippleTrimEnd, sampleCurve,
    sanitizeTimeline, secOf, setCurvePoint, setSourcePath, setSpeed, setTransition, snap, snapPoints, snapTime,
    splitClip, srcOut, srcSpan, srcTimeAt, streamStartFrameOf, timelineDuration, trackAudible, trackVisible,
    timelineDuration as tlDur, transitionFrames, trimClip, validateTimeline
} from '../shared/timeline';

const F = 25;
const near = (a: number, b: number, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const codes = (tl: Timeline) => validateTimeline(tl).map(i => i.code);
const fx = (): EffectState => ({ settings: DEFAULT_SETTINGS, expert: expertDefaults(), customFilters: [], lut: null });

const vstream = (fpsNum = 25, fpsDen = 1): VideoStreamInfo => ({
    index: 0, codec: 'h264', codecLong: 'H.264 / AVC', profile: 'High', width: 1920, height: 1080,
    displayWidth: 1920, displayHeight: 1080, sar: '1:1', fps: fpsNum / fpsDen, fpsNum, fpsDen,
    avgFps: fpsNum / fpsDen, vfr: false, pixFmt: 'yuv420p', bitDepth: 8, colorSpace: 'bt709',
    colorPrimaries: 'bt709', colorTransfer: 'bt709', colorRange: 'tv', rotation: 0, hdr: false, alpha: false,
    frames: 0, bitRate: 0, title: ''
});
const astream = (index = 1): AudioStreamInfo => ({
    index, codec: 'aac', codecLong: 'AAC (Advanced Audio Coding)', channels: 2, layout: 'stereo',
    sampleRate: 48000, bitRate: 0, language: 'eng', title: ''
});
const media = (path: string, duration = 10, o: { fpsNum?: number; fpsDen?: number; audio?: AudioStreamInfo[] } = {}): MediaInfo => {
    const video = vstream(o.fpsNum ?? 25, o.fpsDen ?? 1);
    return {
        path, name: path.replace(/^.*[\\/]/, ''), size: 1024, mtimeMs: 0, container: 'mp4', containerLong: 'MP4/',
        duration, startTime: 0, bitRate: 0, video, videoStreams: [video], audio: o.audio ?? [astream()],
        subtitles: [], encrypted: false, warnings: []
    };
};

/** V1 (top video) + A1 (audio) with one 10 s source 'S1'. */
function base(): Timeline {
    const tl = defaultTimeline(F);
    tl.sources = [{ id: 'S1', path: 'M:/media/a.mp4', media: media('M:/media/a.mp4', 10) }];
    return tl;
}
function put(tl: Timeline, spec: { start: number; dur: number; srcIn?: number; speed?: number; track?: number; id?: string }): TimelineClip {
    const c = makeClip({
        trackId: tl.tracks[spec.track ?? 0].id, sourceId: 'S1',
        start: spec.start, dur: spec.dur, srcIn: spec.srcIn ?? 0
    });
    if (spec.speed !== undefined) c.speed = spec.speed;
    if (spec.id) c.id = spec.id;
    tl.clips.push(c);
    return c;
}
const V1 = (tl: Timeline) => tl.tracks[0].id;
const A1 = (tl: Timeline) => tl.tracks[1].id;
/** Index (for `put`) of the i-th track of a kind. */
const nth = (tl: Timeline, kind: 'video' | 'audio', i = 0) => tl.tracks.indexOf(tl.tracks.filter(t => t.kind === kind)[i]);
const audioOnly = (path: string, duration = 10): MediaInfo => ({ ...media(path, duration), video: null, videoStreams: [] });
const videoOnly = (path: string, duration = 10): MediaInfo => media(path, duration, { audio: [] });
/** base() with its single source 'S1' replaced by another media snapshot. */
function baseWith(m: MediaInfo): Timeline {
    const tl = base();
    tl.sources = [{ id: 'S1', path: m.path, media: m }];
    return tl;
}

/* ------------------------------------------------------------------ time grid */

test('positions are frame-grid seconds, not frames', () => {
    near(snap(1.234, F), 1.24);
    near(snap(1.2, F), 1.2);
    assert.ok(isAligned(1.24, F));
    assert.ok(!isAligned(1.235, F));
    assert.equal(DEFAULT_FRAME_RATE, 25);
    const c = makeClip({ trackId: 'T', sourceId: 'S', start: 1, dur: 2, srcIn: 0.5 });
    c.speed = 2.5;
    near(srcSpan(c), 5); near(srcOut(c), 5.5); near(clipEnd(c), 3);
});

test('a transition window straddles the cut with hL frames left, hR right', () => {
    assert.deepEqual(handleSides(1, F), { hL: 12, hR: 13 });
    assert.deepEqual(handleSides(0.4, F), { hL: 5, hR: 5 });
    assert.deepEqual(handleSides(0, F), { hL: 0, hR: 1 });
    assert.equal(transitionFrames(0.96, F), 24);
    near(secOf(handleSides(1, F).hL + handleSides(1, F).hR, F), 1);
    const c = makeClip({ trackId: 'T', sourceId: 'S', start: 0, dur: 2, srcIn: 0 });
    c.speed = 2;
    near(handleNeed(c, 13, F), 1.04);
});

/* ------------------------------------------------------------------ validation */

test('validateTimeline reports geometry, media and reference problems', () => {
    const tl = base();
    assert.deepEqual(codes(tl), []);

    put(tl, { start: 0, dur: 1.234 });
    assert.ok(codes(tl).includes('unaligned'));

    const tl2 = base();
    put(tl2, { start: 0, dur: 2, id: 'A' });
    put(tl2, { start: 1, dur: 2, id: 'B' });
    const overlap = validateTimeline(tl2).find(i => i.code === 'overlap');
    assert.equal(overlap?.clipId, 'B');
    assert.equal(overlap?.trackId, V1(tl2));

    const tl3 = base();
    put(tl3, { start: 0, dur: 4, srcIn: 8, id: 'A' });
    assert.ok(codes(tl3).includes('mediaShort'));

    const tl4 = base();
    put(tl4, { start: 0, dur: 0, id: 'A' });
    put(tl4, { start: 1, dur: 1, srcIn: -1, id: 'B' });
    const orphan = makeClip({ trackId: 'nope', sourceId: 'S1', start: 5, dur: 1, srcIn: 0 });
    orphan.id = 'C';
    tl4.clips.push(orphan);
    const got = codes(tl4);
    assert.ok(got.includes('zeroDur'));
    assert.ok(got.includes('negIn'));
    assert.ok(got.includes('missingTrack'));

    const tl5 = base();
    put(tl5, { start: 0, dur: 1 });
    tl5.clips[0].sourceId = 'gone';
    assert.ok(codes(tl5).includes('missingSource'));
});

test('transition validation: no left clip, gap, too long, missing handles', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    tl.clips[0].transition = { kind: 'dissolve', dur: 1 };
    assert.ok(codes(tl).includes('trNoLeft'));

    const tl2 = base();
    put(tl2, { start: 0, dur: 2, id: 'A' });
    put(tl2, { start: 3, dur: 2, srcIn: 3, id: 'B' });
    clipById(tl2, 'B')!.transition = { kind: 'wipeLeft', dur: 1 };
    assert.ok(codes(tl2).includes('trGap'));

    // B is 12 frames long but the window needs hR = 13 frames of it
    const tl3 = base();
    put(tl3, { start: 0, dur: 2, id: 'A' });
    put(tl3, { start: 2, dur: 0.48, srcIn: 2, id: 'B' });
    clipById(tl3, 'B')!.transition = { kind: 'dissolve', dur: 1 };
    const tooLong = validateTimeline(tl3).find(i => i.code === 'trTooLong');
    assert.ok(tooLong, 'expected trTooLong');
    assert.equal(tooLong?.clipId, 'B');

    // two transitions whose windows overlap inside the middle clip (hR 13 + hL 12 > 24 frames)
    const tl4 = base();
    put(tl4, { start: 0, dur: 2, id: 'A' });
    put(tl4, { start: 2, dur: 0.96, srcIn: 3, id: 'B' });
    put(tl4, { start: 2.96, dur: 2, srcIn: 4, id: 'C' });
    clipById(tl4, 'B')!.transition = { kind: 'dissolve', dur: 1 };   // 25 frames = 12+13
    clipById(tl4, 'C')!.transition = { kind: 'dissolve', dur: 1 };   // 25 frames = 12+13
    const overlapped = validateTimeline(tl4).find(i => i.code === 'trOverlap');
    assert.ok(overlapped, 'two 1 s transitions inside a 0.96 s clip must conflict');
    assert.equal(overlapped?.clipId, 'B');

    // B's in point is 0.20 s from the cut and A's media ends at its out point
    const tl5 = base();
    put(tl5, { start: 0, dur: 1, srcIn: 9, id: 'A' });
    put(tl5, { start: 1, dur: 2, srcIn: 0.2, id: 'B' });
    clipById(tl5, 'B')!.transition = { kind: 'circleOpen', dur: 1 };
    const handles = validateTimeline(tl5).find(i => i.code === 'trHandles');
    assert.ok(handles, 'expected trHandles');
    assert.match(handles!.message, /0\.480 s before the in point of B \(only 0\.200 s available\)/);
    assert.match(handles!.message, /0\.520 s after the out point of A \(only 0\.000 s available\)/);

    // same geometry with enough media is clean
    const tl6 = base();
    put(tl6, { start: 0, dur: 2, id: 'A' });
    put(tl6, { start: 2, dur: 2, srcIn: 4, id: 'B' });
    clipById(tl6, 'B')!.transition = { kind: 'circleOpen', dur: 1 };
    assert.deepEqual(codes(tl6), []);
});

test('an audio-only source is a noVideo issue on a video track and clean on an audio track', () => {
    const onVideo = baseWith(audioOnly('M:/media/voice.wav'));
    put(onVideo, { start: 0, dur: 2, id: 'A', track: nth(onVideo, 'video') });
    const issue = validateTimeline(onVideo).find(i => i.code === 'noVideo');
    assert.equal(issue?.clipId, 'A');
    assert.equal(issue?.trackId, onVideo.tracks.filter(t => t.kind === 'video')[0].id);
    assert.ok(!codes(onVideo).includes('noAudio'));

    const onAudio = baseWith(audioOnly('M:/media/voice.wav'));
    put(onAudio, { start: 0, dur: 2, id: 'A', track: nth(onAudio, 'audio') });
    assert.ok(!codes(onAudio).includes('noVideo'));
    assert.deepEqual(codes(onAudio), []);
});

test('a source without audio streams is a noAudio issue on an audio track only', () => {
    const onAudio = baseWith(videoOnly('M:/media/mute.mp4'));
    put(onAudio, { start: 0, dur: 2, id: 'A', track: nth(onAudio, 'audio') });
    const issue = validateTimeline(onAudio).find(i => i.code === 'noAudio');
    assert.equal(issue?.clipId, 'A');
    assert.equal(issue?.trackId, onAudio.tracks.filter(t => t.kind === 'audio')[0].id);
    assert.ok(!codes(onAudio).includes('noVideo'));

    const onVideo = baseWith(videoOnly('M:/media/mute.mp4'));
    put(onVideo, { start: 0, dur: 2, id: 'A', track: nth(onVideo, 'video') });
    assert.ok(!codes(onVideo).includes('noAudio'));
    assert.ok(!codes(onVideo).includes('noVideo'));
    assert.deepEqual(codes(onVideo), []);
});

test('mediaHeadroom swaps sides for reversed clips', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 2, id: 'A' });
    near(mediaHeadroom(tl, clipById(tl, 'A')!, 'left'), 2);
    near(mediaHeadroom(tl, clipById(tl, 'A')!, 'right'), 4);
    clipById(tl, 'A')!.reverse = true;                      // shows media 6 -> 2
    near(mediaHeadroom(tl, clipById(tl, 'A')!, 'left'), 4);
    near(mediaHeadroom(tl, clipById(tl, 'A')!, 'right'), 2);
});

/* ------------------------------------------------------------------ sources and tracks */

test('sources: add re-probes by path, remove refuses while clips reference it', () => {
    let tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    const added = addSource(tl, 'M:/media/a.mp4', media('M:/media/a.mp4', 20));
    assert.ok(added.ok);
    assert.equal(added.ok && added.tl.sources.length, 1);
    assert.equal(added.ok && added.tl.sources[0].media.duration, 20);

    const other = addSource(tl, 'M:/media/b.mp4', media('M:/media/b.mp4', 6));
    assert.ok(other.ok && other.tl.sources.length === 2);
    const removed = removeSource(tl, 'S1');
    assert.ok(!removed.ok && /used by 1 clip/.test(removed.ok ? '' : removed.error));
    assert.ok(removeSource(tl, 'S2').ok);

    const relinked = setSourcePath(tl, 'S1', 'M:/media/relink.mp4', media('M:/media/relink.mp4', 30));
    assert.ok(relinked.ok);
    assert.equal(relinked.ok && relinked.tl.sources[0].path, 'M:/media/relink.mp4');
    assert.equal(relinked.ok && relinked.tl.sources[0].media.duration, 30);
    assert.ok(!setSourcePath(tl, 'missing', 'x').ok);
});

test('tracks: new video tracks stack below the existing ones, audio tracks append', () => {
    let tl = base();
    const res = addTrack(tl, 'video');
    assert.ok(res.ok);
    tl = res.ok ? res.tl : tl;
    assert.deepEqual(tl.tracks.map(t => t.name), ['V1', 'V2', 'A1']);
    assert.equal(tl.tracks[1].kind, 'video');

    const audio = addTrack(tl, 'audio');
    assert.ok(audio.ok);
    const names = audio.ok ? audio.tl.tracks.map(t => t.name) : [];
    assert.deepEqual(names, ['V1', 'V2', 'A1', 'A2']);

    const moved = moveTrack(audio.ok ? audio.tl : tl, tl.tracks[1].id, 0);
    assert.ok(moved.ok);
    assert.equal(moved.ok ? moved.tl.tracks[0].name : '', 'V2');
    assert.ok(!moveTrack(tl, 'nope', 0).ok);

    const patched = patchTrack(tl, A1(tl), { gainDb: -6, pan: 0.5, muted: true, name: 'dialog' });
    assert.ok(patched.ok);
    const t = patched.ok ? patched.tl.tracks.find(x => x.id === A1(tl)) : undefined;
    assert.equal(t?.id, A1(tl));
    assert.equal(t?.gainDb, -6);
    assert.equal(t?.pan, 0.5);
    assert.equal(t?.muted, true);
    assert.equal(t?.name, 'dialog');

    const clipRes = insertClip(tl, { trackId: A1(tl), sourceId: 'S1', start: 0, dur: 2, srcIn: 0 });
    assert.ok(clipRes.ok);
    const busy = removeTrack(clipRes.ok ? clipRes.tl : tl, A1(tl));
    assert.ok(!busy.ok && /contains 1 clip/.test(busy.ok ? '' : busy.error));
    assert.ok(removeTrack(tl, A1(tl)).ok);
});

/* ------------------------------------------------------------------ insert / move / split */

test('insertClip ripples everything that starts at the drop point', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    const res = insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: 3, dur: 1, srcIn: 5 });
    assert.ok(res.ok);
    if (res.ok) {
        const newId = res.tl.clips.find(c => c.id !== 'A' && c.id !== 'B')!.id;
        assert.deepEqual(clipsOn(res.tl, V1(tl)).map(c => [c.id, c.start, clipEnd(c)]),
            [['A', 0, 2], [newId, 3, 4], ['B', 4, 6]]);
    }
});

test('insertClip replace mode drops only the overlapped clips', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    const res = insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: 1, dur: 2, srcIn: 6, mode: 'replace' });
    assert.ok(res.ok);
    const list = res.ok ? clipsOn(res.tl, V1(tl)) : [];
    assert.equal(list.length, 2);
    assert.ok(list.some(c => c.id === 'B'));
    assert.ok(!list.some(c => c.id === 'A'));
    assert.deepEqual(codes(res.ok ? res.tl : tl), []);
});

test('insertClip refuses nonsense geometry and missing media', () => {
    const tl = base();
    assert.ok(!insertClip(tl, { trackId: 'nope', sourceId: 'S1', start: 0, dur: 1, srcIn: 0 }).ok);
    assert.ok(!insertClip(tl, { trackId: V1(tl), sourceId: 'gone', start: 0, dur: 1, srcIn: 0 }).ok);
    assert.ok(!insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: -1, dur: 1, srcIn: 0 }).ok);
    assert.ok(!insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: 0, dur: 0, srcIn: 0 }).ok);
    assert.ok(!insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: 0, dur: 11, srcIn: 0 }).ok);
    // overlap that ripple cannot resolve
    put(tl, { start: 0, dur: 4, id: 'A' });
    const bad = insertClip(tl, { trackId: V1(tl), sourceId: 'S1', start: 2, dur: 1, srcIn: 5 });
    assert.ok(!bad.ok);
});

test('moveClip rejects overlaps and locked tracks but allows cross-track moves', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    const blocked = moveClip(tl, 'B', { start: 1 });
    assert.ok(!blocked.ok);
    assert.match(blocked.ok ? '' : blocked.error, /Position blocked/);

    const moved = moveClip(tl, 'B', { trackId: A1(tl), start: 6 });
    assert.ok(moved.ok);
    assert.equal(moved.ok ? clipById(moved.tl, 'B')?.trackId : '', A1(tl));
    assert.equal(moved.ok ? clipById(moved.tl, 'B')?.start : -1, 6);

    const lockedRes = patchTrack(tl, A1(tl), { locked: true });
    const locked = lockedRes.ok ? moveClip(lockedRes.tl, 'B', { trackId: A1(tl), start: 6 }) : lockedRes;
    assert.ok(!locked.ok);
    assert.equal(locked.ok ? '' : locked.error, 'Track is locked.');
    assert.ok(!moveClip(tl, 'nope', { start: 0 }).ok);
    assert.ok(!moveClip(tl, 'B', { start: -0.04 }).ok);
});

test('splitClip shifts the in point by speed and re-bases automation', () => {
    const tl = base();
    const a = put(tl, { start: 0, dur: 4, srcIn: 1, id: 'A' });
    a.speed = 2;
    a.fadeIn = 0.5; a.fadeOut = 1; a.audioFadeOut = 0.5;
    a.opacityCurve = [{ t: 1, v: 0.2 }, { t: 3, v: 0.8 }];
    a.effect = fx();

    const res = splitClip(tl, 'A', 2);
    assert.ok(res.ok);
    const tl2 = res.ok ? res.tl : base();
    const left = clipById(tl2, res.info.leftId)!;
    const right = clipById(tl2, res.info.rightId)!;
    near(left.dur, 2); near(left.srcIn, 1); near(srcOut(left), 5);
    near(right.start, 2); near(right.dur, 2); near(right.srcIn, 5); near(srcOut(right), 9);
    assert.equal(right.speed, 2);
    assert.equal(left.fadeIn, 0.5);
    assert.equal(left.fadeOut, 0);
    assert.equal(left.audioFadeOut, 0);
    assert.equal(right.fadeIn, 0);          // the fade stays on the original left edge
    assert.equal(right.fadeOut, 1);         // the original right edge travels with the right half
    assert.equal(right.audioFadeOut, 0.5);
    assert.deepEqual(left.opacityCurve, [{ t: 1, v: 0.2 }]);
    assert.deepEqual(right.opacityCurve, [{ t: 0, v: 0.5 }, { t: 1, v: 0.8 }]);
    assert.ok(right.effect);
    assert.deepEqual(codes(tl2), []);

    for (const at of [0, 4, -1, 5]) {
        const bad = splitClip(tl, 'A', at);
        assert.ok(!bad.ok);
        assert.equal(bad.ok ? '' : bad.error, 'Split point is outside the clip.');
    }
    assert.ok(!splitClip(tl, 'nope', 1).ok);
});

/* ------------------------------------------------------------------ automation curves */

test('sampleCurve holds the ends and interpolates in between', () => {
    assert.equal(sampleCurve([], 1), null);
    assert.equal(sampleCurve([{ t: 2, v: 0.4 }], 0), 0.4);
    assert.equal(sampleCurve([{ t: 2, v: 0.4 }], 9), 0.4);
    const pts = [{ t: 3, v: 1 }, { t: 1, v: 0 }];
    assert.equal(sampleCurve(pts, 0), 0);
    assert.equal(sampleCurve(pts, 4), 1);
    assert.equal(sampleCurve(pts, 2), 0.5);
    assert.equal(sampleCurve(pts, 1.25), 0.125);
});

test('curve points snap to the grid, clamp to the clip and overwrite at the same time', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 0, id: 'A' });
    let res = setCurvePoint(tl, 'A', 'gain', 1.234, 3);
    assert.ok(res.ok);
    assert.deepEqual(res.ok ? clipById(res.tl, 'A')!.gainCurve : [], [{ t: 1.24, v: 3 }]);
    res = res.ok ? setCurvePoint(res.tl, 'A', 'gain', 1.24, -6) : res;
    assert.deepEqual(res.ok ? clipById(res.tl, 'A')!.gainCurve : [], [{ t: 1.24, v: -6 }]);
    res = res.ok ? setCurvePoint(res.tl, 'A', 'gain', 99, 0) : res;      // clamped to dur
    const after = res.ok ? clipById(res.tl, 'A')!.gainCurve : [];
    assert.deepEqual(after.map(p => p.t), [1.24, 4]);
    assert.ok(res.ok && !setCurvePoint(res.tl, 'A', 'gain', 2, Number.NaN).ok);

    const removed = res.ok ? removeCurvePoint(res.tl, 'A', 'gain', 1.24) : res;
    assert.ok(removed.ok);
    assert.deepEqual(removed.ok ? clipById(removed.tl, 'A')!.gainCurve : [], [{ t: 4, v: 0 }]);
    assert.ok(removed.ok && !removeCurvePoint(removed.tl, 'A', 'gain', 9).ok);
    const cleared = removed.ok ? clearCurve(removed.tl, 'A', 'gain') : removed;
    assert.deepEqual(cleared.ok ? clipById(cleared.tl, 'A')!.gainCurve : [], []);
});

test('patchClip edits in place and never moves the clip to another track', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, srcIn: 0, id: 'A' });
    const res = patchClip(tl, 'A', { opacity: 0.5, level: 1.2, gainDb: -3, pan: -1, trackId: A1(tl), start: 100 });
    assert.ok(res.ok);
    const c = res.ok ? clipById(res.tl, 'A')! : undefined;
    assert.equal(c?.trackId, V1(tl));
    assert.equal(c?.opacity, 0.5);
    assert.equal(c?.level, 1.2);
    assert.equal(c?.gainDb, -3);
    assert.equal(c?.pan, -1);
    assert.equal(c?.start, 100);
    assert.ok(!patchClip(tl, 'A', { srcIn: 9, dur: 4 }).ok);   // runs past the media
});

/* ------------------------------------------------------------------ trim / ripple / speed */

test('trim keeps the media frame under the edge that did not move', () => {
    const tl = base();
    put(tl, { start: 4, dur: 6, srcIn: 2, id: 'A' });   // media 2 .. 8
    const both = trimClip(tl, 'A', { start: 5, dur: 5 });
    assert.ok(both.ok);
    if (both.ok) {
        const a = clipById(both.tl, 'A')!;
        near(a.srcIn, 3); near(srcOut(a), 8);
    }

    const onlyEnd = trimClip(tl, 'A', { dur: 7 });
    assert.ok(onlyEnd.ok);
    if (onlyEnd.ok) {
        const a = clipById(onlyEnd.tl, 'A')!;
        near(a.srcIn, 2); near(srcOut(a), 9);
    }

    // a plain trim never ripples; it is rejected when it runs into the next clip
    const tl2 = base();
    put(tl2, { start: 0, dur: 2, id: 'A' });
    put(tl2, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    const snug = trimClip(tl2, 'A', { dur: 3 });
    assert.ok(snug.ok);
    assert.equal(snug.ok ? clipById(snug.tl, 'B')?.start : -1, 3);
    const collide = trimClip(tl2, 'A', { dur: 3.04 });
    assert.ok(!collide.ok);
    assert.match(collide.ok ? '' : collide.error, /Overlap on V1/);
});

test('trim out of the available media is refused with the missing handle amount', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 1, id: 'A' });   // media 1 .. 5, 5 s left after it
    const over = trimClip(tl, 'A', { dur: 10 });
    assert.ok(!over.ok);
    assert.equal(over.ok ? '' : over.error, 'Not enough media after the out point (5.000 s available).');
    const early = trimClip(tl, 'A', { start: -0.04, dur: 4.04 });
    assert.ok(!early.ok);
    assert.equal(early.ok ? '' : early.error, 'Clip starts before 0.');
    const tl2 = base();
    put(tl2, { start: 1, dur: 4, srcIn: 0.52, id: 'A' });   // only 0.52 s of media before the in point
    const left = trimClip(tl2, 'A', { start: 0, dur: 5 });
    assert.ok(!left.ok);
    assert.equal(left.ok ? '' : left.error, 'Not enough media before the in point (0.520 s available).');
    assert.ok(trimClip(tl, 'A', { start: 1, dur: 4 }).ok);
    assert.ok(!trimClip(tl, 'A', { dur: 0 }).ok);
    assert.ok(!trimClip(tl, 'nope', { dur: 1 }).ok);
});

test('reversed clips trim their in point from the timeline-right edge', () => {
    const tl = base();
    const c = put(tl, { start: 4, dur: 6, srcIn: 2, id: 'A' });   // plays media 8 -> 2
    c.reverse = true;
    const left = trimClip(tl, 'A', { start: 5, dur: 5 });
    assert.ok(left.ok);
    const l = left.ok ? clipById(left.tl, 'A')! : c;
    near(l.srcIn, 2); near(srcOut(l), 7);      // first shown frame moved 8 -> 7

    const right = trimClip(tl, 'A', { dur: 7 });
    assert.ok(right.ok);
    const r = right.ok ? clipById(right.tl, 'A')! : c;
    near(r.srcIn, 1); near(srcOut(r), 8);      // last shown frame moved 2 -> 1

    const exact = trimClip(tl, 'A', { start: 2, dur: 9.04 });   // takes the media exactly to its ends
    assert.ok(exact.ok);
    const e = exact.ok ? clipById(exact.tl, 'A')! : c;
    near(e.srcIn, 0.96); near(srcOut(e), 10);

    const overEnd = trimClip(tl, 'A', { start: 1.96, dur: 6 });  // 2.04 s needed past srcOut
    assert.ok(!overEnd.ok);
    assert.equal(overEnd.ok ? '' : overEnd.error, 'Not enough media after the out point (2.000 s available).');

    const overIn = trimClip(tl, 'A', { start: 4, dur: 9 });
    assert.ok(!overIn.ok);
    assert.equal(overIn.ok ? '' : overIn.error, 'Not enough media before the in point (2.000 s available).');
});

test('rippleTrimEnd closes the gap it makes', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    put(tl, { start: 6, dur: 1, srcIn: 4, id: 'C' });
    const res = rippleTrimEnd(tl, 'A', 3);
    assert.ok(res.ok);
    const list = res.ok ? clipsOn(res.tl, V1(tl)).map(c => [c.id, c.start, clipEnd(c)]) : [];
    assert.deepEqual(list, [['A', 0, 3], ['B', 4, 6], ['C', 7, 8]]);
    assert.ok(!rippleTrimEnd(tl, 'A', 9).ok);
});

test('setSpeed keeps the consumed media span and ripples the tail', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 0, id: 'A' });
    put(tl, { start: 5, dur: 2, srcIn: 4, id: 'B' });
    const fast = setSpeed(tl, 'A', 2);
    assert.ok(fast.ok);
    const a = fast.ok ? clipById(fast.tl, 'A')! : undefined;
    near(a?.dur ?? -1, 2); near(a?.srcIn ?? -1, 0); near(srcOut(a!), 4);
    assert.equal(fast.ok ? clipById(fast.tl, 'B')?.start : -1, 3);

    const slow = setSpeed(tl, 'A', 0.5);
    assert.ok(slow.ok);
    near(slow.ok ? clipById(slow.tl, 'A')!.dur : -1, 8);
    near(srcOut(slow.ok ? clipById(slow.tl, 'A')! : a!), 4);   // same media, twice the timeline length
    assert.equal(slow.ok ? clipById(slow.tl, 'B')?.start : -1, 9);

    // a snug tail is pushed by the delta instead of colliding (insert ripple)
    const tl2 = base();
    put(tl2, { start: 0, dur: 4, srcIn: 0, id: 'A' });
    put(tl2, { start: 4, dur: 2, srcIn: 4, id: 'B' });
    const snug = setSpeed(tl2, 'A', 0.5);
    assert.ok(snug.ok);
    near(snug.ok ? clipById(snug.tl, 'A')!.dur : -1, 8);
    assert.equal(snug.ok ? clipById(snug.tl, 'B')?.start : -1, 8);

    assert.ok(!setSpeed(tl, 'A', 0).ok);
    assert.ok(!setSpeed(tl, 'A', -1).ok);
    assert.ok(!setSpeed(tl, 'A', 101).ok);
});

test('deleteClip ripple closes the gap, lift (extract) leaves it', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 2, dur: 3, srcIn: 2, id: 'B' });
    put(tl, { start: 5, dur: 1, srcIn: 5, id: 'C' });
    const ripple = deleteClip(tl, 'B', 'ripple');
    assert.ok(ripple.ok);
    assert.deepEqual(ripple.ok ? clipsOn(ripple.tl, V1(tl)).map(c => [c.id, c.start]) : [], [['A', 0], ['C', 2]]);
    const lift = deleteClip(tl, 'B', 'lift');
    assert.ok(lift.ok);
    assert.deepEqual(lift.ok ? clipsOn(lift.tl, V1(tl)).map(c => [c.id, c.start]) : [], [['A', 0], ['C', 5]]);
    assert.ok(!deleteClip(tl, 'nope', 'lift').ok);
});

test('ripple delete keeps a transition whose new neighbours still fit, drops one that does not', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 2, dur: 3, srcIn: 2, id: 'B' });
    put(tl, { start: 5, dur: 2, srcIn: 5, id: 'C' });
    clipById(tl, 'C')!.transition = { kind: 'dissolve', dur: 1 };
    assert.deepEqual(codes(tl), []);

    const res = deleteClip(tl, 'B', 'ripple');       // C lands on A's out point: media is still there
    assert.ok(res.ok);
    const c = res.ok ? clipById(res.tl, 'C')! : undefined;
    assert.equal(c?.start, 2);
    assert.deepEqual(c?.transition, { kind: 'dissolve', dur: 1 });
    assert.deepEqual(codes(res.ok ? res.tl : tl), []);

    const moved = res.ok ? moveClip(res.tl, 'C', { start: 3 }) : res;     // a gap breaks the link
    assert.ok(moved.ok);
    assert.equal(moved.ok ? clipById(moved.tl, 'C')?.transition : 'x', null);
});

test('duplicateClip copies the clip settings to a new position', () => {
    const tl = base();
    const c = put(tl, { start: 0, dur: 2, srcIn: 1, id: 'A' });
    c.gainDb = -4; c.pan = 0.25; c.audioFadeIn = 0.5; c.speed = 2;
    const res = duplicateClip(tl, 'A', 4);
    assert.ok(res.ok);
    const copy = res.ok ? clipById(res.tl, res.tl.clips.find(x => x.id !== 'A')!.id) : undefined;
    assert.equal(copy?.start, 4);
    near(copy?.dur ?? -1, 2);
    near(copy?.srcIn ?? -1, 1);
    assert.equal(copy?.gainDb, -4);
    assert.equal(copy?.pan, 0.25);
    assert.equal(copy?.audioFadeIn, 0.5);
    assert.equal(copy?.speed, 2);
    assert.equal(copy?.transition, null);
    assert.ok(!duplicateClip(tl, 'nope', 4).ok);
});

/* ------------------------------------------------------------------ transitions and snapping */

test('setTransition validates the neighbours and stores the rounded window', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 2, dur: 2, srcIn: 4, id: 'B' });

    const first = setTransition(tl, 'B', 'wipeLeft', 0.7);
    assert.ok(first.ok);
    const tr = first.ok ? clipById(first.tl, 'B')?.transition : null;
    assert.deepEqual(tr, { kind: 'wipeLeft', dur: 0.72 });     // 18 frames

    const noLeft = setTransition(tl, 'A', 'dissolve');
    assert.ok(!noLeft.ok);
    if (!noLeft.ok) assert.match(noLeft.error, /needs a clip directly to its left/);

    const gap = base();
    put(gap, { start: 0, dur: 2, id: 'A' });
    put(gap, { start: 3, dur: 2, srcIn: 3, id: 'B' });
    const bridging = setTransition(gap, 'B', 'dissolve');
    assert.ok(!bridging.ok);
    if (!bridging.ok) assert.equal(bridging.error, 'Transition cannot bridge a gap.');

    // 101 frames cannot be taken out of two 2 s clips
    const tooLong = setTransition(tl, 'B', 'dissolve', 4.04);
    assert.ok(!tooLong.ok);
    if (!tooLong.ok) assert.match(tooLong.error, /does not fit its neighbours \(50 \/ 50 frames\)/);

    // the same transition length is fine when both sides are long enough
    const long = base();
    put(long, { start: 0, dur: 2, id: 'A' });
    put(long, { start: 2, dur: 2, srcIn: 4, id: 'B' });
    assert.ok(setTransition(long, 'B', 'dissolve', 2.5).ok);

    const shortMedia = base();
    put(shortMedia, { start: 0, dur: 1, srcIn: 9, id: 'A' });    // A's media ends at its out point
    put(shortMedia, { start: 1, dur: 2, srcIn: 0.2, id: 'B' });  // 0.48 s needed before B's in point
    const handles = setTransition(shortMedia, 'B', 'dissolve', 1);
    assert.ok(!handles.ok);
    if (!handles.ok) assert.match(handles.error, /needs media handles/);

    const cleared = setTransition(tl, 'B', null);
    assert.ok(cleared.ok);
    assert.equal(cleared.ok ? clipById(cleared.tl, 'B')?.transition : 'x', null);
    assert.ok(!setTransition(tl, 'B', 'zoom' as never, 1).ok);
});

test('markers, snap points and the snap tolerance', () => {
    const tl = base();
    put(tl, { start: 2, dur: 2, id: 'A' });
    let res = addMarker(tl, 3.234, 'start');
    assert.ok(res.ok);
    res = res.ok ? addMarker(res.tl, 1, 'cue') : res;
    const markers = res.ok ? res.tl.markers : [];
    assert.deepEqual(markers.map(m => [m.id, m.t, m.name]), [[2, 1, 'cue'], [1, 3.24, 'start']]);
    assert.equal(tlDur(res.ok ? res.tl : tl), 4);

    const points = snapPoints(res.ok ? res.tl : tl);
    assert.deepEqual(points, [0, 1, 2, 3.24, 4]);

    const hit = snapTime(tl, 1.9);                 // 2.5 frames from the clip start
    assert.deepEqual(hit, { t: 2, snapped: true });
    const miss = snapTime(tl, 1.5);                // 12.5 frames away, lands on the grid
    assert.deepEqual(miss, { t: 1.52, snapped: false });
    assert.deepEqual(snapTime(tl, 2.1, 3), { t: 2, snapped: true });
    assert.deepEqual(snapTime(tl, 2.1, 2), { t: 2.12, snapped: false });

    const removed = removeMarker(res.ok ? res.tl : tl, 1);
    assert.ok(removed.ok && removed.ok && removed.tl.markers.length === 1);
    assert.ok(!removeMarker(tl, 1).ok);
    const renamed = renameMarker(res.ok ? res.tl : tl, 2, 'intro');
    assert.equal(renamed.ok ? renamed.tl.markers[0].name : '', 'intro');
    assert.ok(!renameMarker(tl, 7, 'x').ok);
});

test('mute silences, hidden kills the picture only, solo silences everything else', () => {
    const tl = base();
    const v1 = trackById0(tl);
    assert.ok(trackAudible(tl, v1));
    assert.ok(trackVisible(v1));
    const mutedRes = patchTrack(tl, v1.id, { muted: true });
    assert.ok(!trackAudible(tl, mutedRes.ok ? mutedRes.tl.tracks[0] : v1));
    assert.ok(trackVisible(mutedRes.ok ? mutedRes.tl.tracks[0] : v1));

    const hiddenRes = patchTrack(tl, v1.id, { hidden: true });
    const hidden = hiddenRes.ok ? hiddenRes.tl.tracks[0] : v1;
    assert.ok(!trackVisible(hidden));
    assert.ok(trackAudible(tl, hidden));
    assert.ok(!trackVisible(tl.tracks[1]));        // an audio track never draws

    const soloRes = patchTrack(tl, A1(tl), { solo: true });
    const soloTl = soloRes.ok ? soloRes.tl : tl;
    assert.ok(!trackAudible(soloTl, soloTl.tracks[0]));
    assert.ok(trackAudible(soloTl, soloTl.tracks[1]));
});

function trackById0(tl: Timeline) { return tl.tracks[0]; }

/* ------------------------------------------------------------------ render plan */

test('buildPlan decodes exactly the frames a clip must show', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 1, id: 'A' });
    const plan = buildPlan(tl);
    assert.equal(plan.frameRate, F);
    assert.equal(plan.totalFrames, 100);
    assert.equal(plan.clips.length, 1);
    const p = plan.clips[0];
    assert.deepEqual([p.startF, p.endF, p.headF, p.tailF, p.streamFrames], [0, 100, 0, 0, 100]);
    near(p.decodeStart, 1); near(p.decodeEnd, 5);
    assert.equal(p.reversePrepass, false);
    assert.equal(p.audible, true);
    assert.equal(p.visible, true);
    assert.equal(plan.windows.length, 1);
    assert.deepEqual([plan.windows[0].startF, plan.windows[0].frames], [0, 100]);
    assert.equal(plan.windows[0].video.length, 1);
    assert.equal(plan.windows[0].video[0].role, 'single');
    assert.equal(plan.windows[0].video[0].z, 0);
    assert.equal(plan.windows[0].audio.length, 1);
});

test('a gap between clips becomes its own empty window', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 3, dur: 2, srcIn: 2, id: 'B' });
    const plan = buildPlan(tl);
    assert.equal(plan.totalFrames, 125);
    assert.deepEqual(plan.windows.map(w => [w.startF, w.frames]), [[0, 50], [50, 25], [75, 50]]);
    assert.deepEqual(plan.windows.map(w => w.video.length), [1, 0, 1]);
});

test('transition handles extend both media streams by exactly hL / hR', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 0, id: 'A' });
    put(tl, { start: 4, dur: 4, srcIn: 4, id: 'B' });
    clipById(tl, 'B')!.transition = { kind: 'dissolve', dur: 1 };
    const plan = buildPlan(tl);

    const a = plan.clips.find(c => c.clip.id === 'A')!;
    const b = plan.clips.find(c => c.clip.id === 'B')!;
    assert.deepEqual([a.headF, a.tailF, a.streamFrames], [0, 13, 113]);
    near(a.decodeStart, 0); near(a.decodeEnd, 4.52);
    assert.deepEqual([b.headF, b.tailF, b.streamFrames], [12, 0, 112]);
    near(b.decodeStart, 3.52); near(b.decodeEnd, 8);
    assert.equal(streamStartFrameOf(tl, clipById(tl, 'B')!), 88);

    // clip boundaries split the transition window too, so a stream only ever swaps at a window edge
    assert.deepEqual(plan.windows.map(w => [w.startF, w.endF, w.frames]),
        [[0, 88, 88], [88, 100, 12], [100, 113, 13], [113, 200, 87]]);
    assert.equal(plan.windows[0].video[0].role, 'single');
    for (const mid of [plan.windows[1], plan.windows[2]]) {
        assert.deepEqual(mid.video.map(l => [l.role, l.z, l.transitionKind, l.transStartF, l.transFrames]),
            [['a', 0, 'dissolve', 88, 25], ['b', 1, 'dissolve', 88, 25]]);
        assert.deepEqual(mid.audio.map(l => l.role), ['a', 'b']);
        assert.equal(mid.video[0].streamStartF, 0);
        assert.equal(mid.video[1].streamStartF, 88);
    }
    assert.equal(plan.windows[3].video[0].role, 'single');
    assert.equal(plan.windows[3].video[0].transitionKind, null);
});

test('the first track in the array draws on top of later ones', () => {
    let tl = base();
    const add = addTrack(tl, 'video');
    assert.ok(add.ok);
    tl = add.ok ? add.tl : tl;
    put(tl, { start: 0, dur: 2, id: 'UNDER', track: 1 });
    put(tl, { start: 0, dur: 2, id: 'OVER', track: 0 });
    const plan = buildPlan(tl);
    const layers = plan.windows[0].video.map(l => [l.clipId, l.z]);
    assert.equal(layers.length, 2);
    assert.ok(layers.find(l => l[0] === 'OVER')![1] > layers.find(l => l[0] === 'UNDER')![1],
        'the first track in the array must draw above the later one');
});

test('hidden tracks drop out of the video windows but keep their audio', () => {
    let tl = base();
    const hide = patchTrack(tl, V1(tl), { hidden: true });
    assert.ok(hide.ok);
    tl = hide.ok ? hide.tl : tl;
    put(tl, { start: 0, dur: 2, id: 'A' });
    const plan = buildPlan(tl);
    assert.equal(plan.clips[0].visible, false);
    assert.equal(plan.clips[0].audible, true);
    assert.equal(plan.windows[0].video.length, 0);
    assert.equal(plan.windows[0].audio.length, 1);
});

test('a hidden clip loses its picture layer but keeps its audio chain', () => {
    let tl = base();
    put(tl, { start: 0, dur: 2, id: 'A', track: nth(tl, 'video') });
    put(tl, { start: 0, dur: 2, srcIn: 4, id: 'B', track: nth(tl, 'audio') });
    const hide = patchClip(tl, 'A', { hidden: true });
    assert.ok(hide.ok);
    tl = hide.ok ? hide.tl : tl;

    const plan = buildPlan(tl);
    assert.ok(plan.windows.every(w => w.video.every(l => l.clipId !== 'A')));
    assert.ok(plan.windows.some(w => w.audio.some(l => l.clipId === 'A')));
    const a = plan.clips.find(p => p.clip.id === 'A')!;
    assert.equal(a.visible, false);
    assert.equal(a.audible, true);

    const graph = buildAudioGraph(tl, plan);
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(plan.clips.map(p => p.clip.id), ['A', 'B']);
    assert.deepEqual(graph.chains.map(c => c.plan.clip.id), ['A', 'B']);
    assert.equal(graph.chains[0].plan, a);
    assert.equal(graph.chains[0].stream, '0:a:0');
    assert.ok(graph.chains[0].filter.includes('asetpts=PTS-STARTPTS'), graph.chains[0].filter);
});

test('a clip whose source has no video stream draws nothing on a video track but still mixes', () => {
    const tl = base();
    tl.sources.push({ id: 'S2', path: 'M:/media/voice.wav', media: audioOnly('M:/media/voice.wav') });
    put(tl, { start: 0, dur: 2, id: 'A', track: nth(tl, 'video') }).sourceId = 'S2';

    const plan = buildPlan(tl);
    assert.ok(plan.issues.some(i => i.code === 'noVideo' && i.clipId === 'A'));
    assert.deepEqual(plan.clips.map(p => p.clip.id), ['A']);
    assert.equal(plan.clips[0].visible, false);
    assert.equal(plan.clips[0].audible, true);
    assert.equal(plan.windows.length, 1);
    assert.equal(plan.windows[0].video.length, 0);
    assert.deepEqual(plan.windows[0].audio.map(l => l.clipId), ['A']);

    const graph = buildAudioGraph(tl, plan);
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(graph.chains.map(c => c.plan.clip.id), ['A']);
    assert.deepEqual(graph.inputs, [{ path: 'M:/media/voice.wav', ss: 0, t: 2 }]);
    assert.ok(graph.chains[0].filter.includes('asetpts=PTS-STARTPTS'), graph.chains[0].filter);
});

test('a hidden clip does not consume a z slot', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 0, id: 'A', track: nth(tl, 'video') });
    put(tl, { start: 4, dur: 4, srcIn: 4, id: 'B', track: nth(tl, 'video') });
    clipById(tl, 'B')!.transition = { kind: 'dissolve', dur: 1 };
    clipById(tl, 'A')!.hidden = true;
    const plan = buildPlan(tl);
    assert.deepEqual(plan.windows.map(w => [w.startF, w.endF]), [[0, 88], [88, 100], [100, 113], [113, 200]]);
    assert.deepEqual(plan.windows[0].video, []);
    for (const mid of [plan.windows[1], plan.windows[2]]) {
        assert.deepEqual(mid.video.map(l => [l.clipId, l.role, l.z]), [['B', 'b', 0]]);
        assert.deepEqual(mid.audio.map(l => [l.clipId, l.role]), [['A', 'a'], ['B', 'b']]);
    }

    let stacked = base();
    const add = addTrack(stacked, 'video');
    assert.ok(add.ok);
    stacked = add.ok ? add.tl : stacked;
    put(stacked, { start: 0, dur: 2, id: 'OVER', track: nth(stacked, 'video', 0) });
    put(stacked, { start: 0, dur: 2, id: 'UNDER', track: nth(stacked, 'video', 1) }).hidden = true;
    assert.deepEqual(buildPlan(stacked).windows[0].video.map(l => [l.clipId, l.z]), [['OVER', 0]]);
});

test('buildPlan ranges a subrange and reports total frames for it', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 2, dur: 2, srcIn: 2, id: 'B' });
    const plan = buildPlan(tl, 1, 3);
    assert.equal(plan.totalFrames, 50);
    assert.deepEqual(plan.clips.map(p => [p.startF, p.endF]), [[0, 50], [50, 100]]);
    assert.deepEqual(plan.windows.map(w => [w.startF, w.endF]), [[25, 50], [50, 75]]);
    assert.deepEqual(buildPlan(tl, 3, 1).totalFrames, 0);
});

test('srcTimeAt maps timeline frames into the media, forwards and backwards', () => {
    const tl = base();
    const c = put(tl, { start: 2, dur: 4, srcIn: 1, id: 'A' });
    near(srcTimeAt({ clip: c, startF: 50 }, 50, F), 1);
    near(srcTimeAt({ clip: c, startF: 50 }, 75, F), 2);
    near(srcTimeAt({ clip: c, startF: 50 }, 149, F), 4.96);
    c.speed = 2;
    near(srcTimeAt({ clip: c, startF: 50 }, 75, F), 3);
    c.reverse = true;
    near(srcTimeAt({ clip: c, startF: 50 }, 50, F), srcOut(c));
    near(srcTimeAt({ clip: c, startF: 50 }, 75, F), srcOut(c) - 2);
});

test('reversed clips ask the export for a pre-reversed source', () => {
    const tl = base();
    const c = put(tl, { start: 0, dur: 3, srcIn: 2, id: 'A' });
    c.reverse = true;
    clipById(tl, 'A')!.reverse = true;
    const plan = buildPlan(tl);
    const p = plan.clips[0];
    assert.equal(p.reversePrepass, true);
    near(p.decodeStart, 2); near(p.decodeEnd, 5);
    assert.equal(p.streamFrames, 75);
});

test('buildPlan keeps reporting issues instead of refusing to plan', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, id: 'A' });
    put(tl, { start: 1, dur: 2, srcIn: 9, id: 'B' });
    const plan = buildPlan(tl);
    const got = plan.issues.map(i => i.code);
    assert.ok(got.includes('overlap'));
    assert.ok(got.includes('mediaShort'));
    assert.equal(plan.clips.length, 2);
});

/* ------------------------------------------------------------------ audio graph */

test('panLaw and audioWindowWeight are the laws the preview mixer uses too', () => {
    assert.deepEqual(panLaw(0), { left: 1, right: 1 });
    assert.deepEqual(panLaw(-1), { left: 1, right: 0 });
    assert.deepEqual(panLaw(1), { left: 0, right: 1 });
    assert.deepEqual(panLaw(9), { left: 0, right: 1 });
    near(audioWindowWeight('a', 0), 1); near(audioWindowWeight('b', 0), 0);
    near(audioWindowWeight('a', 0.25), 0.75); near(audioWindowWeight('b', 0.75), 0.75);
    near(dbToLin(0), 1); near(dbToLin(-6), 10 ** -0.3); near(dbToLin(6), 10 ** 0.3);
});

test('one audible clip becomes one chain, delayed to its timeline slot', () => {
    const tl = base();
    put(tl, { start: 2, dur: 2, srcIn: 1, id: 'A' });
    const plan = buildPlan(tl);
    const graph = buildAudioGraph(tl, plan);
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(graph.inputs, [{ path: 'M:/media/a.mp4', ss: 1, t: 2 }]);
    assert.equal(graph.chains.length, 1);
    assert.equal(graph.chains[0].stream, '0:a:0');
    assert.equal(graph.chains[0].streamStartF, 50);
    assert.equal(graph.outLabel, 'aout');
    assert.ok(graph.chains[0].filter.includes('asetpts=PTS-STARTPTS'));
    assert.ok(graph.chains[0].filter.includes('aformat=sample_fmts=fltp:sample_rates=48000'), graph.chains[0].filter);
    assert.ok(graph.chains[0].filter.includes(`volume=eval=frame:volume='1'`));
    assert.ok(graph.chains[0].filter.includes('adelay=delays=2000:all=1'));
    assert.ok(!graph.chains[0].filter.includes('atempo'), graph.chains[0].filter);
    assert.ok(graph.chains[0].filter.includes('pan=stereo|c0=1*c0|c1=1*c1'), graph.chains[0].filter);
    assert.ok(graph.filterComplex.includes('[ua0]anull[aout]'));
    assert.ok(graph.filterComplex.includes('[aout]apad=whole_dur=4,atrim=0:4,aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[aout]'));
});

test('clip, track and master gains multiply into one volume expression', () => {
    let tl = base();
    const gainTl = patchTrack(tl, V1(tl), { gainDb: -3 });
    assert.ok(gainTl.ok);
    tl = gainTl.ok ? gainTl.tl : tl;
    put(tl, { start: 0, dur: 2, srcIn: 0, id: 'A' });
    const withGain = patchClip(tl, 'A', { gainDb: 6, pan: 1 });
    assert.ok(withGain.ok);
    tl = withGain.ok ? withGain.tl : tl;
    tl.masterGainDb = 3;

    const plan = buildPlan(tl);
    const graph = buildAudioGraph(tl, plan);
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    const expectedGain = (10 ** ((6 - 3 + 3) / 20)).toFixed(6);
    assert.ok(graph.chains[0].filter.includes(`volume=eval=frame:volume='${expectedGain}'`), graph.chains[0].filter);
    assert.ok(graph.chains[0].filter.includes('pan=stereo|c0=0*c0|c1=1*c1'), graph.chains[0].filter);
});

test('the stereo fold maps mono, surround and exotic layouts the same way the preview mixer will', () => {
    const chainFor = (channels: number, layout: string, pan = 0): string => {
        let tl = base();
        tl.sources[0].media.audio[0].channels = channels;
        tl.sources[0].media.audio[0].layout = layout;
        const patched = patchClip(tl, put(tl, { start: 0, dur: 2, srcIn: 0, id: 'A' }).id, { pan });
        assert.ok(patched.ok);
        if (patched.ok) tl = patched.tl;
        const graph = buildAudioGraph(tl, buildPlan(tl));
        assert.ok('chains' in graph);
        return 'chains' in graph ? graph.chains[0].filter : '';
    };
    assert.ok(chainFor(1, 'mono').includes('pan=stereo|c0=1*c0|c1=1*c0'), chainFor(1, 'mono'));
    // panLaw clamps at unity: panning attenuates the other side, it never boosts
    assert.ok(chainFor(1, 'mono', 0.5).includes('pan=stereo|c0=0.5*c0|c1=1*c0'), chainFor(1, 'mono', 0.5));
    assert.ok(chainFor(1, 'mono', -0.5).includes('pan=stereo|c0=1*c0|c1=0.5*c0'), chainFor(1, 'mono', -0.5));
    assert.ok(chainFor(6, '5.1').includes('pan=stereo|c0=1*c0+0.707107*c2+0.707107*c4+0.707107*c5|c1=1*c1+0.707107*c2+0.707107*c4+0.707107*c5'),
        chainFor(6, '5.1'));
    const exotic = chainFor(8, 'octual');
    assert.ok(exotic.includes('aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo'), exotic);
    assert.deepEqual(channelFold({ ...astream(), channels: 1, layout: 'mono' }), { l: [1], r: [1] });
    assert.equal(channelFold({ ...astream(), channels: 8, layout: 'octual' }), null);
});

test('a range that starts inside a clip reads only the media it contributes and re-anchors the fades', () => {
    let tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 1, id: 'A' });            // timeline 0..4 over media 1..5
    const faded = patchClip(tl, 'A', { audioFadeIn: 2 });
    assert.ok(faded.ok);
    if (faded.ok) tl = faded.tl;
    const graph = buildAudioGraph(tl, buildPlan(tl, 1, 2));     // export range 1..2 s
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(graph.inputs, [{ path: 'M:/media/a.mp4', ss: 2, t: 1 }]);
    assert.ok(!graph.chains[0].filter.includes('adelay'), graph.chains[0].filter);
    // the 2 s fade started at the clip start, so the range begins half way through it
    assert.ok(graph.chains[0].filter.includes('min(1\\,max(0\\,((t+1)-0)/2))'), graph.chains[0].filter);
    assert.ok(graph.filterComplex.includes('apad=whole_dur=1,atrim=0:1'), graph.filterComplex);
});

test('a reversed clip reads the pre-reversed temp from its own start and needs no areverse', () => {
    const tl = base();
    put(tl, { start: 2, dur: 2, srcIn: 1, id: 'A' });            // media 1..3 played backwards at 2..4 s
    clipById(tl, 'A')!.reverse = true;
    const plan = buildPlan(tl);
    const graph = buildAudioGraph(tl, plan, { pathOverride: new Map([['A', 'M:/media/rev_A.mkv']]) });
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(graph.inputs, [{ path: 'M:/media/rev_A.mkv', ss: 0, t: 2 }]);
    assert.ok(!graph.chains[0].filter.includes('areverse'), graph.chains[0].filter);
    assert.ok(graph.chains[0].filter.includes('adelay=delays=2000:all=1'), graph.chains[0].filter);
});

test('audio fades, gain automation and crossfades are factors of one expression', () => {
    const tl = base();
    put(tl, { start: 0, dur: 4, srcIn: 0, id: 'A' });
    put(tl, { start: 4, dur: 4, srcIn: 4, id: 'B' });
    put(tl, { start: 8, dur: 2, srcIn: 6, id: 'C' });
    const a = clipById(tl, 'A')!; a.audioFadeIn = 1; a.audioFadeOut = 2;
    a.gainCurve = [{ t: 0, v: 1 }, { t: 2, v: 0.5 }];
    clipById(tl, 'B')!.transition = { kind: 'dissolve', dur: 1 };

    const graph = buildAudioGraph(tl, buildPlan(tl));
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    const fa = graph.chains.find(c => c.plan.clip.id === 'A')!.filter;
    assert.ok(fa.includes('min(1\\,max(0\\,(t-0)/1))'), fa);
    assert.ok(fa.includes('min(1\\,max(0\\,(4-t)/2))'), fa);
    assert.ok(fa.includes('(1-min(1\\,max(0\\,(t-3.52)/1)))'), fa);
    assert.ok(fa.includes('if(lt(t\\,0)\\,1\\,if(lt(t\\,2)\\,1+(0.5-1)*(t-0)/2\\,0.5))'), fa);

    const fb = graph.chains.find(c => c.plan.clip.id === 'B')!.filter;
    assert.ok(fb.includes('min(1\\,max(0\\,(t-0)/1))'), fb);   // ramps in over the transition window
    const fc = graph.chains.find(c => c.plan.clip.id === 'C')!.filter;
    assert.ok(!/max\(0/.test(fc), fc);

    assert.equal(graph.chains.length, 3);
    assert.ok(graph.filterComplex.includes('amix=inputs=3:duration=longest:normalize=0:dropout_transition=0:weights=1 1 1[aout]'));
    assert.deepEqual(graph.inputs.map(i => [i.ss, i.t]), [[0, 4.52], [3.52, 4.48], [6, 2]]);
});

test('muted clips, silent sources and silent ranges never reach the mixer', () => {
    const tl = base();
    put(tl, { start: 0, dur: 2, srcIn: 0, id: 'A' });
    put(tl, { start: 2, dur: 2, srcIn: 2, id: 'B' });
    clipById(tl, 'A')!.audioMuted = true;
    const graph = buildAudioGraph(tl, buildPlan(tl));
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.deepEqual(graph.chains.map(c => c.plan.clip.id), ['B']);
    assert.ok(graph.filterComplex.includes('[ua0]anull[aout]'));

    const skipped = buildAudioGraph(tl, buildPlan(tl), { skipClipIds: new Set(['B']) });
    assert.ok(!('chains' in skipped));
    assert.equal('chains' in skipped ? '' : skipped.error, 'No audio tracks in this range.');

    const silent = base();
    silent.sources = [{ id: 'S1', path: 'M:/media/a.mp4', media: media('M:/media/a.mp4', 10, { audio: [] }) }];
    put(silent, { start: 0, dur: 2, srcIn: 0, id: 'A' });
    const r = buildAudioGraph(silent, buildPlan(silent));
    assert.ok(!('chains' in r));

    const mutedTrack = patchTrack(tl, V1(tl), { muted: true });
    const rr = buildAudioGraph(mutedTrack.ok ? mutedTrack.tl : tl, buildPlan(mutedTrack.ok ? mutedTrack.tl : tl));
    assert.ok(!('chains' in rr));
});

test('speed and transition handles change the amount of source audio that is read', () => {
    const tl = base();
    const c = put(tl, { start: 0, dur: 2, srcIn: 1, id: 'A' });     // media 1 .. 5, 2x speed
    c.speed = 2;
    put(tl, { start: 2, dur: 2, srcIn: 6, id: 'B' });               // media 6 .. 8
    clipById(tl, 'B')!.transition = { kind: 'fadeBlack', dur: 0.5 };   // 13 frames = 6 + 7
    const graph = buildAudioGraph(tl, buildPlan(tl));
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;

    const a = graph.chains.find(x => x.plan.clip.id === 'A')!;
    const b = graph.chains.find(x => x.plan.clip.id === 'B')!;
    near(a.readStart, 1); near(a.readDur, 4.56);      // 57 output frames at 2x = 4.56 source s
    near(b.readStart, 5.76); near(b.readDur, 2.24);   // 6 handle frames before its in point
    assert.deepEqual(graph.inputs.map(i => [i.ss, i.t]), [[1, 4.56], [5.76, 2.24]]);
    assert.equal(a.streamStartF, 0);
    assert.equal(b.streamStartF, 44);

    // ramps stay in timeline seconds; tempo scaling happens in `atempo`
    assert.ok(a.filter.includes('(1-min(1\\,max(0\\,(t-1.76)/0.52)))'), a.filter);
    assert.ok(b.filter.includes('min(1\\,max(0\\,(t-0)/0.52))'), b.filter);
    assert.ok(a.filter.includes('atempo=2'), a.filter);
    assert.ok(!b.filter.includes('atempo'), b.filter);
});

test('speeds outside ffmpeg\'s 0.5..2 atempo range become factor chains', () => {
    const tl = base();
    const fast = put(tl, { start: 0, dur: 1, srcIn: 0, id: 'F' });
    fast.speed = 4;
    const slow = put(tl, { start: 1, dur: 1, srcIn: 2, id: 'S' });
    slow.speed = 0.25;
    const graph = buildAudioGraph(tl, buildPlan(tl));
    assert.ok('chains' in graph);
    if (!('chains' in graph)) return;
    assert.ok(graph.chains.find(c => c.plan.clip.id === 'F')!.filter.includes('atempo=2,atempo=2'),
        graph.chains.find(c => c.plan.clip.id === 'F')!.filter);
    assert.ok(graph.chains.find(c => c.plan.clip.id === 'S')!.filter.includes('atempo=0.5,atempo=0.5'),
        graph.chains.find(c => c.plan.clip.id === 'S')!.filter);
});

/* ------------------------------------------------------------------ migration, sanitizing, media helpers */

test('a v1 project migrates into one clip on the first video track', () => {
    const effect = fx();
    const v1: UveProjectV1 = {
        schema: 'uve-project', version: 1, app: 'UVE Manager 1.0.1', uveSource: UVE_SOURCE,
        savedAt: '2024-01-01T00:00:00.000Z',
        media: { path: 'M:/media/a.mp4', size: 1, mtimeMs: 0, videoStream: 0, audioStream: 1 },
        effect, inPoint: 2, outPoint: 5, export: DEFAULT_EXPORT, ui: { wipe: 0.3, compare: false }
    };
    const v2 = migrateProjectV1toV2(v1, media('M:/media/a.mp4', 10), F);
    assert.equal(v2.version, 2);
    assert.equal(v2.effect, effect);
    assert.equal(v2.export, DEFAULT_EXPORT);
    assert.deepEqual(v2.ui, { wipe: 0.3, compare: false, timelineZoom: 90 });
    assert.deepEqual(v2.timeline.tracks.map(t => t.kind), ['video', 'audio']);
    assert.equal(v2.timeline.sources.length, 1);
    assert.equal(v2.timeline.sources[0].path, 'M:/media/a.mp4');
    assert.equal(v2.timeline.clips.length, 1);
    const clip = v2.timeline.clips[0];
    assert.equal(clip.trackId, v2.timeline.tracks[0].id);
    assert.equal(clip.sourceId, v2.timeline.sources[0].id);
    assert.deepEqual([clip.start, clip.dur, clip.srcIn], [0, 3, 2]);
    assert.deepEqual(validateTimeline(v2.timeline), []);

    const whole = migrateProjectV1toV2({ ...v1, inPoint: null, outPoint: null }, media('M:/media/a.mp4', 10), F);
    assert.deepEqual([whole.timeline.clips[0].start, whole.timeline.clips[0].dur, whole.timeline.clips[0].srcIn], [0, 10, 0]);

    const noMedia = migrateProjectV1toV2({ ...v1, media: null }, emptyMediaInfo('x.mp4'), F);
    assert.equal(noMedia.timeline.clips.length, 0);
});

test('sanitizeTimeline defaults everything and drops what cannot work', () => {
    const tl = sanitizeTimeline({
        frameRate: 30, fps: '29.97', masterGainDb: -2,
        sources: [{ id: 'S9', path: '' }, { id: 'S8', path: 'M:/media/a.mp4' }],
        tracks: [{ id: 'T1', kind: 'audio', gainDb: 'oops' }],
        clips: [{
            id: 'C1', trackId: 'T1', sourceId: 'S8', start: -3, dur: 0.5, srcIn: -2, speed: 0,
            transition: { kind: 'nope', dur: 1 }, gainCurve: [{ t: 'x', v: 1 }, { t: 2, v: 2 }, { t: 1, v: 3 }],
            levelCurve: 'nonsense', effect: { settings: {} }
        }],
        markers: [{ id: 5, t: 4 }, { t: 1.5, name: 'cue' }]
    });
    assert.equal(tl.frameRate, 30);
    assert.equal(tl.fps, '29.97');
    near(tl.masterGainDb, -2);
    assert.deepEqual(tl.sources.map(s => s.id), ['S8']);
    assert.deepEqual(tl.sources[0].media.warnings, ['Media not probed yet.']);
    assert.deepEqual(tl.tracks.map(t => [t.id, t.kind, t.gainDb]), [['T1', 'audio', 0]]);
    assert.equal(tl.clips.length, 1);
    const clip = tl.clips[0];
    assert.deepEqual([clip.id, clip.start, clip.dur, clip.srcIn, clip.speed], ['C1', 0, 0.5, 0, 1]);
    assert.equal(clip.transition, null);
    assert.deepEqual(clip.gainCurve, [{ t: 1, v: 3 }, { t: 2, v: 2 }]);
    assert.deepEqual(clip.levelCurve, []);
    assert.equal(clip.effect, null);
    assert.deepEqual(tl.markers.map(m => [m.id, m.t, m.name]), [[6, 1.5, 'cue'], [5, 4, '']]);

    const empty = sanitizeTimeline(null, { frameRate: F });
    assert.deepEqual(empty.tracks.map(t => t.name), ['V1', 'A1']);
    assert.equal(empty.fps, '25');
    assert.equal(sanitizeTimeline({ frameRate: -1 }).frameRate, DEFAULT_FRAME_RATE);

    const withEffect = sanitizeTimeline({ clips: [{ id: 'C', trackId: 'T1', sourceId: 'S', start: 0, dur: 1, srcIn: 0, transition: { kind: 'circleClose', dur: 0.4 }, effect: { settings: { preset: 'x' } } }] },
        { parseEffect: raw => raw as EffectState });
    assert.deepEqual(withEffect.clips[0].transition, { kind: 'circleClose', dur: 0.4 });
    assert.ok(withEffect.clips[0].effect);
    assert.ok(isTransitionKind('smoothRight') && !isTransitionKind('zoom'));
});

test('project frame rate and the ffmpeg -r argument keep NTSC rational', () => {
    const ntsc = media('M:/media/a.mp4', 10, { fpsNum: 30000, fpsDen: 1001 });
    near(frameRateFromMedia(ntsc), 30000 / 1001);
    assert.equal(fpsArgument(ntsc, 29.97), '30000/1001');
    const exact = media('M:/media/a.mp4', 10, { fpsNum: 25, fpsDen: 1 });
    assert.equal(frameRateFromMedia(exact), 25);
    assert.equal(fpsArgument(exact, 25), '25');
    assert.equal(fpsArgument(emptyMediaInfo('x.mp4'), 29.97), '29.970000');
    assert.equal(frameRateFromMedia(emptyMediaInfo('x.mp4'), 30), 30);
    assert.equal(frameRateFromMedia(media('M:/media/a.mp4', 10, { audio: [] })), 25);
    const noVideo = media('M:/media/a.mp4', 10);
    noVideo.video = null;
    noVideo.videoStreams = [];
    assert.equal(frameRateFromMedia(noVideo, 24), 24);
});

test('timelineDuration counts clips and markers', () => {
    const tl = base();
    assert.equal(timelineDuration(tl), 0);
    put(tl, { start: 1, dur: 2, id: 'A' });
    const withMarker = addMarker(tl, 9, 'end');
    assert.ok(withMarker.ok);
    assert.equal(timelineDuration(withMarker.ok ? withMarker.tl : tl), 9);
    assert.equal(emptyTimeline().tracks.length, 0);
    assert.equal(defaultTimeline(30, '29.97').fps, '29.97');
});

test('pairTracks keeps the tracks in V1, A1, V2, A2 … order whatever order they were added in', async () => {
    const { addTrack, defaultTimeline, pairTracks } = await import('../shared/timeline');
    const add = (tl: Timeline, kind: 'video' | 'audio') => { const r = addTrack(tl, kind, tl.tracks.length); assert.ok(r.ok); return r.tl; };
    const names = (tl: Timeline) => tl.tracks.map(t => t.name).join(',');
    let tl = defaultTimeline(25);
    tl = add(tl, 'video');
    let r = pairTracks(tl); assert.ok(r.ok);
    assert.equal(names(r.tl), 'V1,A1,V2');
    tl = add(r.tl, 'audio');
    r = pairTracks(tl); assert.ok(r.ok);
    assert.equal(names(r.tl), 'V1,A1,V2,A2');
    // audio first, then video
    tl = add(add(defaultTimeline(25), 'audio'), 'video');
    r = pairTracks(tl); assert.ok(r.ok);
    assert.equal(names(r.tl), 'V1,A1,V2,A2');
    // extra tracks of one kind follow at the end; ids and settings are kept
    tl = add(add(defaultTimeline(25), 'video'), 'video');
    r = pairTracks(tl); assert.ok(r.ok);
    assert.equal(names(r.tl), 'V1,A1,V2,V3');
    assert.deepEqual(r.tl.tracks.map(t => t.id).sort(), tl.tracks.map(t => t.id).sort());
});
