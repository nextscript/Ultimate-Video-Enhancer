import { type ReactNode, useEffect, useState } from 'react';

const ICONS: Record<string, string> = {
    play: 'M8 5v14l11-7z',
    pause: 'M6 5h4v14H6zM14 5h4v14h-4z',
    start: 'M6 5h2v14H6zM20 5v14L9 12z',
    end: 'M16 5h2v14h-2zM4 5v14l11-7z',
    prev: 'M7 6h2v12H7zM19 6v12l-9-6z',
    next: 'M15 6h2v12h-2zM5 6v12l9-6z',
    editor: 'M4 5h16v14H4zM10 9v6l5-3z',
    presets: 'M5 4h10l4 4v12H5zM14 4v5h5M8 13h8M8 16h6',
    luts: 'M12 3a9 9 0 1 0 0 18c1.5 0 2-1 2-2s-1-1.5-1-2.5S14 15 15 15h2a4 4 0 0 0 4-4c0-4.4-4-8-9-8zM7.5 11.5h.01M10 7.5h.01M15 7.5h.01',
    filters: 'M4 6h16M7 12h10M10 18h4',
    scopes: 'M4 20V4M4 20h16M7 16l4-5 3 3 5-7',
    queue: 'M4 6h16M4 12h16M4 18h10M18 16l3 2-3 2z',
    settings: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-2.7 1.1V21a2 2 0 1 1-4 0v-.1A1.6 1.6 0 0 0 7.5 19.4a1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1A1.6 1.6 0 0 0 1.6 14H1.5a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1.1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H7.5A1.6 1.6 0 0 0 8.5 3V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1h.1a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z',
    open: 'M3 7h6l2 2h10v10H3zM3 7V5h6',
    save: 'M5 3h11l3 3v15H5zM8 3v5h7V3M8 21v-7h8v7',
    undo: 'M9 14L4 9l5-5M4 9h10a6 6 0 0 1 0 12h-3',
    redo: 'M15 14l5-5-5-5M20 9H10a6 6 0 0 0 0 12h3',
    render: 'M12 3l9 5v8l-9 5-9-5V8zM12 12l9-4M12 12v9M12 12L3 8',
    reset: 'M3 12a9 9 0 1 0 3-6.7L3 8M3 3v5h5',
    folder: 'M3 6h6l2 2h10v11H3z',
    plus: 'M12 5v14M5 12h14',
    trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13',
    up: 'M12 19V5M5 12l7-7 7 7',
    down: 'M12 5v14M19 12l-7 7-7-7',
    copy: 'M8 8h12v12H8zM4 16V4h12',
    eye: 'M1 12s4-7 11-7 11 7 11 7-4 7-11 7S1 12 1 12zM12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
    split: 'M12 3v18M8 8l-4 4 4 4M16 8l4 4-4 4',
    fullscreen: 'M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5',
    volume: 'M4 9h4l5-4v14l-5-4H4zM16 9a4 4 0 0 1 0 6M19 6a8 8 0 0 1 0 12',
    mute: 'M4 9h4l5-4v14l-5-4H4zM17 9l5 6M22 9l-5 6',
    zoomIn: 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM21 21l-5-5M11 8v6M8 11h6',
    zoomOut: 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM21 21l-5-5M8 11h6',
    download: 'M12 4v12M6 10l6 6 6-6M4 20h16',
    upload: 'M12 20V8M6 14l6-6 6 6M4 4h16',
    info: 'M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16v-4M12 8h.01',
    x: 'M6 6l12 12M18 6L6 18',
    check: 'M5 12l5 5L20 7',
    star: 'M12 3l2.8 5.9 6.2.9-4.5 4.4 1 6.3L12 17.6 6.5 20.5l1-6.3L3 9.8l6.2-.9z',
    search: 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM21 21l-5-5',
    inMark: 'M7 4v16M7 4h6M7 20h6',
    outMark: 'M17 4v16M17 4h-6M17 20h-6',
    bypass: 'M4 12h5l3-6 3 12 3-6h2'
};

export function Icon({ name, size = 18, className }: { name: string; size?: number; className?: string }) {
    return (
        <svg className={className} width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
            <path d={ICONS[name] || ICONS.info} fill={name === 'play' || name === 'pause' || name === 'start' || name === 'end' || name === 'prev' || name === 'next' ? 'currentColor' : 'none'} />
        </svg>
    );
}

export function Button({ children, onClick, kind = 'default', icon, disabled, title, small }: {
    children?: ReactNode; onClick?: () => void; kind?: 'default' | 'primary' | 'ghost' | 'danger'; icon?: string; disabled?: boolean; title?: string; small?: boolean;
}) {
    return (
        <button type="button" className={`btn btn-${kind}${small ? ' btn-sm' : ''}`} onClick={onClick} disabled={disabled} title={title} aria-label={title}>
            {icon && <Icon name={icon} size={small ? 14 : 16} />}
            {children && <span>{children}</span>}
        </button>
    );
}

export function Toggle({ checked, onChange, label, title, disabled }: { checked: boolean; onChange: (v: boolean) => void; label?: ReactNode; title?: string; disabled?: boolean }) {
    return (
        <label className={`toggle${disabled ? ' disabled' : ''}`} title={title}>
            <input type="checkbox" role="switch" checked={checked} disabled={disabled} onChange={e => onChange(e.target.checked)} />
            <span className="toggle-track"><span className="toggle-thumb" /></span>
            {label && <span className="toggle-label">{label}</span>}
        </label>
    );
}

export function SliderRow({ label, value, min, max, step, def, onChange, desc, digits, disabled, highlight, suffix }: {
    label: string; value: number; min: number; max: number; step: number; def?: number; onChange: (v: number) => void;
    desc?: string; digits?: number; disabled?: boolean; highlight?: boolean; suffix?: string;
}) {
    const d = digits ?? (step >= 1 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3);
    const [text, setText] = useState(value.toFixed(d));
    useEffect(() => { setText(value.toFixed(d)); }, [value, d]);
    const commit = (t: string) => {
        const n = Number(t.replace(',', '.'));
        if (Number.isFinite(n)) onChange(Math.min(max, Math.max(min, n)));
        else setText(value.toFixed(d));
    };
    const pct = ((value - min) / (max - min)) * 100;
    const changed = def !== undefined && Math.abs(value - def) > step / 2;
    return (
        <div className={`slider-row${disabled ? ' disabled' : ''}${highlight ? ' highlight' : ''}`} title={desc}>
            <span className={`slider-label${changed ? ' changed' : ''}`}>{label}</span>
            <input
                type="range" min={min} max={max} step={step} value={value} disabled={disabled} aria-label={label}
                style={{ ['--pct' as string]: `${pct}%` }}
                onChange={e => onChange(Number(e.target.value))}
                onDoubleClick={() => def !== undefined && onChange(def)}
            />
            <input className="slider-value" value={text} disabled={disabled} aria-label={`${label} value`}
                onChange={e => setText(e.target.value)} onBlur={e => commit(e.target.value)}
                onKeyDown={e => { if (e.key === 'Enter') commit((e.target as HTMLInputElement).value); if (e.key === 'Escape') setText(value.toFixed(d)); }} />
            {suffix && <span className="slider-suffix">{suffix}</span>}
            {def !== undefined && (
                <button type="button" className="icon-btn reset" title={`Reset to ${def}`} aria-label={`Reset ${label}`} disabled={disabled || !changed} onClick={() => onChange(def)}>
                    <Icon name="reset" size={13} />
                </button>
            )}
        </div>
    );
}

export function Select<T extends string | number>({ value, options, onChange, label, title, disabled }: {
    value: T; options: { value: T; label: string }[]; onChange: (v: T) => void; label?: string; title?: string; disabled?: boolean;
}) {
    return (
        <label className="select-row" title={title}>
            {label && <span className="select-label">{label}</span>}
            <select value={String(value)} disabled={disabled} onChange={e => {
                const o = options.find(x => String(x.value) === e.target.value);
                if (o) onChange(o.value);
            }}>
                {options.map(o => <option key={String(o.value)} value={String(o.value)}>{o.label}</option>)}
            </select>
        </label>
    );
}

export function Section({ title, children, right, collapsible = true, defaultOpen = true }: { title: string; children: ReactNode; right?: ReactNode; collapsible?: boolean; defaultOpen?: boolean }) {
    const [open, setOpen] = useState(defaultOpen);
    return (
        <section className="section">
            <header className="section-head" onClick={() => collapsible && setOpen(!open)}>
                {collapsible && <span className={`chev${open ? ' open' : ''}`}>›</span>}
                <h3>{title}</h3>
                <div className="section-right" onClick={e => e.stopPropagation()}>{right}</div>
            </header>
            {open && <div className="section-body">{children}</div>}
        </section>
    );
}

export function Modal({ title, children, onClose, footer, wide }: { title: string; children: ReactNode; onClose?: () => void; footer?: ReactNode; wide?: boolean }) {
    useEffect(() => {
        const k = (e: KeyboardEvent) => { if (e.key === 'Escape' && onClose) onClose(); };
        window.addEventListener('keydown', k);
        return () => window.removeEventListener('keydown', k);
    }, [onClose]);
    return (
        <div className="modal-backdrop" role="dialog" aria-modal="true" aria-label={title}>
            <div className={`modal${wide ? ' wide' : ''}`}>
                <header className="modal-head">
                    <h2>{title}</h2>
                    {onClose && <button className="icon-btn" onClick={onClose} aria-label="Close"><Icon name="x" /></button>}
                </header>
                <div className="modal-body">{children}</div>
                {footer && <footer className="modal-foot">{footer}</footer>}
            </div>
        </div>
    );
}

export function Progress({ value, label }: { value: number; label?: string }) {
    return (
        <div className="progress" role="progressbar" aria-valuenow={Math.round(value * 100)} aria-valuemin={0} aria-valuemax={100}>
            <div className="progress-bar" style={{ width: `${Math.max(0, Math.min(1, value)) * 100}%` }} />
            {label && <span className="progress-label">{label}</span>}
        </div>
    );
}

export const fmtTime = (t: number, frac = false) => {
    if (!Number.isFinite(t) || t < 0) t = 0;
    const h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = Math.floor(t % 60);
    const base = `${h ? h + ':' : ''}${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
    return frac ? `${base}.${String(Math.floor((t % 1) * 1000)).padStart(3, '0')}` : base;
};

export const fmtBytes = (b: number) => b >= 1073741824 ? `${(b / 1073741824).toFixed(2)} GB` : b >= 1048576 ? `${(b / 1048576).toFixed(1)} MB` : b >= 1024 ? `${(b / 1024).toFixed(0)} KB` : `${b} B`;
