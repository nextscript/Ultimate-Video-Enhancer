import { useEffect, useRef, useState } from 'react';
import { useStore } from '../state/store';
import { Select } from './controls';

type Mode = 'histogram' | 'parade' | 'waveform' | 'vectorscope';

function draw(c: HTMLCanvasElement, px: Uint8Array, w: number, h: number, mode: Mode) {
    const ctx = c.getContext('2d')!;
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060b14';
    ctx.fillRect(0, 0, W, H);
    ctx.strokeStyle = 'rgba(120,150,200,0.15)';
    for (let i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, (H * i) / 4); ctx.lineTo(W, (H * i) / 4); ctx.stroke(); }
    if (mode === 'histogram') {
        const bins = [new Uint32Array(256), new Uint32Array(256), new Uint32Array(256), new Uint32Array(256)];
        for (let i = 0; i < px.length; i += 4) {
            bins[0][px[i]]++; bins[1][px[i + 1]]++; bins[2][px[i + 2]]++;
            bins[3][Math.round(0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2])]++;
        }
        let max = 1;
        for (const b of bins) for (let i = 1; i < 255; i++) max = Math.max(max, b[i]);
        const colors = ['rgba(255,70,70,0.55)', 'rgba(70,230,110,0.55)', 'rgba(70,140,255,0.6)', 'rgba(235,235,235,0.5)'];
        ctx.globalCompositeOperation = 'lighter';
        bins.forEach((b, k) => {
            ctx.fillStyle = colors[k];
            ctx.beginPath();
            ctx.moveTo(0, H);
            for (let i = 0; i < 256; i++) ctx.lineTo((i / 255) * W, H - Math.min(1, b[i] / max) * H * 0.95);
            ctx.lineTo(W, H);
            ctx.fill();
        });
        ctx.globalCompositeOperation = 'source-over';
        // clipping indicators
        const total = px.length / 4;
        const clipLo = (bins[3][0] / total) * 100, clipHi = (bins[3][255] / total) * 100;
        ctx.fillStyle = '#9fb3d9';
        ctx.font = '11px Segoe UI';
        ctx.fillText(`crushed ${clipLo.toFixed(1)}%`, 6, 14);
        ctx.fillText(`clipped ${clipHi.toFixed(1)}%`, W - 90, 14);
        return;
    }
    const img = ctx.getImageData(0, 0, W, H);
    const acc = new Float32Array(W * H * 3);
    if (mode === 'parade' || mode === 'waveform') {
        for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
            const i = (y * w + x) * 4;
            if (mode === 'parade') {
                for (let ch = 0; ch < 3; ch++) {
                    const sx = Math.floor((ch * W) / 3 + (x / w) * (W / 3 - 2));
                    const sy = Math.floor((1 - px[i + ch] / 255) * (H - 1));
                    acc[(sy * W + sx) * 3 + ch] += 1;
                }
            } else {
                const l = 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
                const sx = Math.floor((x / w) * (W - 1)), sy = Math.floor((1 - l / 255) * (H - 1));
                const o = (sy * W + sx) * 3;
                acc[o] += 1; acc[o + 1] += 1; acc[o + 2] += 1;
            }
        }
    } else {
        const R = Math.min(W, H) / 2 - 4, cx = W / 2, cy = H / 2;
        for (let i = 0; i < px.length; i += 4) {
            const r = px[i] / 255, g = px[i + 1] / 255, b = px[i + 2] / 255;
            const yv = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const cb = (b - yv) / 1.8556, cr = (r - yv) / 1.5748;
            const sx = Math.floor(cx + cb * 2 * R), sy = Math.floor(cy - cr * 2 * R);
            if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
            const o = (sy * W + sx) * 3;
            acc[o] += r * 3; acc[o + 1] += g * 3; acc[o + 2] += b * 3;
        }
        ctx.strokeStyle = 'rgba(120,150,200,0.3)';
        ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.stroke();
    }
    const gain = mode === 'vectorscope' ? 0.6 : 0.35;
    for (let p = 0; p < W * H; p++) {
        const o = p * 4;
        img.data[o] = Math.min(255, img.data[o] + Math.sqrt(acc[p * 3]) * 255 * gain);
        img.data[o + 1] = Math.min(255, img.data[o + 1] + Math.sqrt(acc[p * 3 + 1]) * 255 * gain);
        img.data[o + 2] = Math.min(255, img.data[o + 2] + Math.sqrt(acc[p * 3 + 2]) * 255 * gain);
    }
    ctx.putImageData(img, 0, 0);
}

export function Scopes({ large = false }: { large?: boolean }) {
    const scopes = useStore(s => s.scopes);
    const media = useStore(s => s.media);
    const [mode, setMode] = useState<Mode>('histogram');
    const [src, setSrc] = useState<'enhanced' | 'original' | 'both'>(large ? 'both' : 'enhanced');
    const a = useRef<HTMLCanvasElement>(null);
    const b = useRef<HTMLCanvasElement>(null);
    useEffect(() => {
        if (!scopes) return;
        if (a.current) draw(a.current, src === 'original' ? scopes.original : scopes.enhanced, scopes.w, scopes.h, mode);
        if (b.current && src === 'both') draw(b.current, scopes.original, scopes.w, scopes.h, mode);
    }, [scopes, mode, src]);
    const W = large ? 640 : 300, H = large ? 300 : 150;
    return (
        <div className={`scopes${large ? ' large' : ''}`}>
            <div className="row gap">
                <Select value={mode} onChange={setMode} options={[{ value: 'histogram', label: 'Histogram' }, { value: 'parade', label: 'RGB Parade' }, { value: 'waveform', label: 'Luma Waveform' }, { value: 'vectorscope', label: 'Vectorscope' }]} />
                <Select value={src} onChange={setSrc} options={[{ value: 'enhanced', label: 'Enhanced' }, { value: 'original', label: 'Original' }, ...(large ? [{ value: 'both' as const, label: 'Enhanced vs Original' }] : [])]} />
            </div>
            {!media && <p className="hint">Open a video to see the scopes.</p>}
            <div className="row gap wrap">
                <figure><canvas ref={a} width={W} height={H} /><figcaption>{src === 'original' ? 'Original' : 'Enhanced'}</figcaption></figure>
                {src === 'both' && <figure><canvas ref={b} width={W} height={H} /><figcaption>Original</figcaption></figure>}
            </div>
        </div>
    );
}
